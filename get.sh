#!/bin/sh
#
# get.sh - one-line bootstrap installer for pacseek.
#
#   curl -fsSL https://codeberg.org/m1stD3V/pacseek/raw/branch/main/get.sh | sh
#
# This script does the smallest job that matters: work out which release to
# build, clone it into a scratch directory, and hand off to the repo's own
# install.sh. Every flag it doesn't recognise is passed straight through, so
# anything install.sh accepts works here too:
#
#   curl -fsSL <url> | sh                          # user install to ~/.local
#   curl -fsSL <url> | sh -s -- --system           # system install to /usr/local
#   curl -fsSL <url> | sh -s -- --prefix ~/opt     # custom prefix
#   curl -fsSL <url> | sh -s -- --ref main         # build a branch instead
#   curl -fsSL <url> | sh -s -- --ref v0.2.0       # build a specific tag
#   curl -fsSL <url> | sh -s -- --help
#
# The whole thing lives inside a function that is only called on the last line.
# If the download is truncated mid-flight, sh reaches EOF without ever calling
# main, so a partial script does nothing instead of running half an install.

# POSIX sh: no pipefail, so pipelines are written to avoid needing it.
set -eu

PACSEEK_REPO="${PACSEEK_REPO:-https://codeberg.org/m1stD3V/pacseek.git}"

show_help() {
  cat <<'EOF'
pacseek bootstrap - fetch and install pacseek with one command.

Usage:
  curl -fsSL <url>/get.sh | sh                    install the latest release
  curl -fsSL <url>/get.sh | sh -s -- [options]    pass options through

Options:
  --ref REF        git tag or branch to build (default: latest vX.Y.Z tag)

  Everything else is forwarded to the repo's install.sh:
  --system         install to /usr/local (uses sudo)
  --prefix DIR     install under a custom prefix (default: ~/.local)
  --build-dir DIR  build directory to use
  --jobs N         parallel build jobs (default: nproc)
  -h, --help       show this help

Environment:
  PACSEEK_REF      same as --ref
  PACSEEK_REPO     clone URL to install from
  NO_COLOR         disable colored output
EOF
}

main() {
  # --- Presentation: matches install.sh, TTY- and NO_COLOR-aware -------------
  if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    BOLD=$(printf '\033[1m');  DIM=$(printf '\033[2m')
    ACCENT=$(printf '\033[36m'); GOOD=$(printf '\033[32m')
    BAD=$(printf '\033[31m');  WARNC=$(printf '\033[33m')
    RESET=$(printf '\033[0m')
  else
    BOLD=""; DIM=""; ACCENT=""; GOOD=""; BAD=""; WARNC=""; RESET=""
  fi

  step() { printf '%s\n' "${BOLD}▸ $*${RESET}"; }
  ok()   { printf '  %s %s\n' "${GOOD}✓${RESET}" "$*"; }
  warn() { printf '  %s %s\n' "${WARNC}!${RESET}" "$*"; }
  die()  { printf '  %s %s\n' "${BAD}✗${RESET}" "$*" >&2; exit 1; }

  printf '%s\n' "${BOLD}${ACCENT}█ PACSEEK${RESET}${DIM} · bootstrap${RESET}"
  printf '%s\n' "${DIM}────────────────────────────────────${RESET}"

  # --- Arguments -------------------------------------------------------------
  # Only --ref is ours. Everything else is collected verbatim and forwarded to
  # install.sh, which owns --system / --prefix / --build-dir / --jobs / --help.
  REF="${PACSEEK_REF:-}"
  # Re-quoting into "$@" is awkward in POSIX sh; shifting the consumed args off
  # the front and keeping the rest in place avoids an eval-based rebuild.
  ARGC=$#
  I=0
  while [ "$I" -lt "$ARGC" ]; do
    case "$1" in
      --ref)     [ $# -ge 2 ] || die "--ref needs a git ref"
                 REF="$2"; shift 2; ARGC=$((ARGC - 2)); continue ;;
      --ref=*)   REF="${1#*=}"; shift; ARGC=$((ARGC - 1)); continue ;;
      -h|--help) show_help; exit 0 ;;
      *)         set -- "$@" "$1"; shift; I=$((I + 1)) ;;
    esac
  done

  # --- Prerequisites for the bootstrap itself --------------------------------
  # install.sh runs the full toolchain check; here we only need what it takes to
  # get the source onto disk and run that script.
  command -v git >/dev/null 2>&1 \
    || die "git not found - install it first ('sudo pacman -S git')"
  command -v bash >/dev/null 2>&1 \
    || die "bash not found - install.sh needs it ('sudo pacman -S bash')"

  # --- Resolve which revision to build ---------------------------------------
  if [ -n "$REF" ]; then
    step "Using requested ref"
    ok "ref: $REF"
  else
    step "Resolving latest release"
    # ls-remote keeps this to one lightweight round-trip - no full clone and no
    # forge-specific API. --refs drops the ^{} peeled duplicates that annotated
    # tags produce; the grep keeps it to vX.Y.Z so stray tags can't win.
    REF=$(git ls-remote --tags --refs "$PACSEEK_REPO" 2>/dev/null \
            | awk '{print $2}' \
            | sed 's|refs/tags/||' \
            | grep -E '^v?[0-9]+\.[0-9]+\.[0-9]+$' \
            | sort -V \
            | tail -n 1) || true

    if [ -z "$REF" ]; then
      # No tags published yet (or the remote is unreachable). Falling back to
      # main keeps the one-liner working rather than dead-ending the user.
      warn "no release tags found - falling back to main"
      REF="main"
    else
      ok "latest release: $REF"
    fi
  fi

  # --- Clone into a scratch directory ----------------------------------------
  SRC=$(mktemp -d "${TMPDIR:-/tmp}/pacseek.XXXXXXXX") \
    || die "could not create a temporary directory"
  # Cleanup on every exit path, including Ctrl-C mid-build.
  trap 'rm -rf "$SRC"' EXIT
  trap 'rm -rf "$SRC"; exit 130' INT
  trap 'rm -rf "$SRC"; exit 143' TERM

  step "Fetching source"
  # --depth 1 against a tag or branch: one commit, no history. CMake still does
  # its own FTXUI fetch during configure, which is the slow part.
  git clone --quiet --depth 1 --branch "$REF" "$PACSEEK_REPO" "$SRC" 2>/dev/null \
    || die "could not clone $PACSEEK_REPO at ref '$REF' - check the ref exists"
  ok "cloned $REF"

  [ -x "$SRC/install.sh" ] || chmod +x "$SRC/install.sh" 2>/dev/null || true
  [ -f "$SRC/install.sh" ] || die "install.sh missing from $REF - bad ref?"

  # --- Hand off ---------------------------------------------------------------
  # install.sh prints its own banner; suppress it so the run reads as one
  # continuous sequence of steps rather than two stacked headers.
  PACSEEK_NO_BANNER=1 bash "$SRC/install.sh" "$@"
}

main "$@"
