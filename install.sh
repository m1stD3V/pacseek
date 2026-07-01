#!/usr/bin/env bash
#
# install.sh - build and install pacseek.
#
# Function over form: this verifies the toolchain, configures a Release build,
# compiles, installs to a prefix, and tells you exactly what to do if the binary
# isn't on your PATH. The only "branding" is a restrained header and status
# ticks; nothing here is decorative at the expense of doing the job.
#
# Usage:
#   ./install.sh                 # user install to ~/.local (no root)
#   ./install.sh --system        # system install to /usr/local (uses sudo)
#   ./install.sh --prefix DIR    # install under a custom prefix
#   ./install.sh --build-dir DIR # use a build directory other than ./build
#   ./install.sh --jobs N        # parallel build jobs (default: nproc)
#   ./install.sh --help

set -euo pipefail

# --- Location: run from the repo root regardless of the caller's cwd ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- Defaults -----------------------------------------------------------------
PREFIX="${HOME}/.local"
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || echo 1)"
USE_SUDO=""

# --- Presentation: TTY- and NO_COLOR-aware, brutalist and minimal -------------
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
  BOLD=$'\e[1m'; DIM=$'\e[2m'; ACCENT=$'\e[36m'; GOOD=$'\e[32m'; BAD=$'\e[31m'
  WARNC=$'\e[33m'; RESET=$'\e[0m'
else
  BOLD=""; DIM=""; ACCENT=""; GOOD=""; BAD=""; WARNC=""; RESET=""
fi

banner() {
  printf '%s\n' "${BOLD}${ACCENT}█ PACSEEK${RESET}${DIM} · installer${RESET}"
  printf '%s\n' "${DIM}────────────────────────────────────${RESET}"
}
step() { printf '%s\n' "${BOLD}▸ $*${RESET}"; }
ok()   { printf '  %s %s\n' "${GOOD}✓${RESET}" "$*"; }
warn() { printf '  %s %s\n' "${WARNC}!${RESET}" "$*"; }
die()  { printf '  %s %s\n' "${BAD}✗${RESET}" "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
pacseek installer - build and install pacseek.

Usage:
  ./install.sh                 user install to ~/.local (no root)
  ./install.sh --system        system install to /usr/local (uses sudo)
  ./install.sh --prefix DIR    install under a custom prefix
  ./install.sh --build-dir DIR use a build directory other than ./build
  ./install.sh --jobs N        parallel build jobs (default: nproc)
  ./install.sh --help          show this help
EOF
  exit 0
}

# --- Arguments ----------------------------------------------------------------
while [ $# -gt 0 ]; do
  case "$1" in
    --system)        PREFIX="/usr/local"; shift ;;
    --prefix)        PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
    --prefix=*)      PREFIX="${1#*=}"; shift ;;
    --build-dir)     BUILD_DIR="${2:?--build-dir needs a directory}"; shift 2 ;;
    --build-dir=*)   BUILD_DIR="${1#*=}"; shift ;;
    --jobs|-j)       JOBS="${2:?--jobs needs a number}"; shift 2 ;;
    --jobs=*)        JOBS="${1#*=}"; shift ;;
    -h|--help)       usage ;;
    *)               die "unknown option: $1 (try --help)" ;;
  esac
done

# --- Prerequisite checks ------------------------------------------------------
# A hard dependency missing here means the CMake configure or build would fail
# later with a noisier message, so we catch them up front with a clear fix.
have() { command -v "$1" >/dev/null 2>&1; }

# Returns 0 if `cmake` is at least 3.20, the version CMakeLists requires.
cmake_recent_enough() {
  local v major minor
  v="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
  major="${v%%.*}"; minor="${v#*.}"
  [ "$major" -gt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -ge 20 ]; }
}

banner
step "Checking prerequisites"

# C++ compiler: honour $CXX, else prefer g++, else clang++.
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  if have g++; then CXX_BIN="g++"; elif have clang++; then CXX_BIN="clang++"; fi
fi
[ -n "$CXX_BIN" ] && have "$CXX_BIN" \
  || die "no C++17 compiler found - install 'gcc' or 'clang'"
ok "compiler: $CXX_BIN"

have cmake || die "cmake not found - install 'cmake' (need ≥ 3.20)"
cmake_recent_enough || die "cmake $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9.]+' | head -1) is too old - need ≥ 3.20"
ok "cmake: $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9.]+' | head -1)"

# CMake fetches FTXUI over git during configure.
have git || die "git not found - needed to fetch FTXUI ('pacman -S git')"
ok "git: present"

# CMakeLists resolves libalpm and libcurl via pkg-config.
have pkg-config || die "pkg-config not found - install 'pkgconf'"
pkg-config --exists libalpm || die "libalpm not found - install 'pacman' (it ships libalpm)"
ok "libalpm: $(pkg-config --modversion libalpm)"
pkg-config --exists libcurl || die "libcurl not found - install 'curl'"
ok "libcurl: $(pkg-config --modversion libcurl)"

# A generator's build tool must exist; CMake prefers ninja, falls back to make.
if have ninja || have make; then
  ok "build tool: $(have ninja && echo ninja || echo make)"
else
  die "no build tool found - install 'ninja' or 'make'"
fi

# nlohmann-json is optional: without it the AUR RPC parser compiles to a stub.
if pkg-config --exists nlohmann_json 2>/dev/null || [ -f /usr/include/nlohmann/json.hpp ]; then
  ok "nlohmann-json: present (live AUR search enabled)"
else
  warn "nlohmann-json missing - AUR search will be disabled (install 'nlohmann-json' to enable)"
fi

# --- Decide whether the install step needs elevation --------------------------
# Walk up to the nearest existing ancestor of the prefix; if it isn't writable,
# the install has to go through sudo.
writable_target() {
  local dir="$1"
  while [ ! -e "$dir" ]; do dir="$(dirname "$dir")"; done
  [ -w "$dir" ]
}
if ! writable_target "$PREFIX"; then
  have sudo || die "prefix '$PREFIX' is not writable and sudo is unavailable"
  USE_SUDO="sudo"
fi

# --- Configure, build, install ------------------------------------------------
step "Configuring ($BUILD_DIR, Release)"
# Let CMake choose the compiler (it already honours an exported $CXX); forcing
# -DCMAKE_CXX_COMPILER would invalidate an existing cache and print noise. The
# detection above is only for a clear "no compiler" error, not to override CMake.
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
ok "configured"

step "Building (-j$JOBS)"
cmake --build "$BUILD_DIR" -j"$JOBS"
ok "built"

step "Installing to $PREFIX"
[ -n "$USE_SUDO" ] && warn "prefix needs elevation - using sudo for install"
$USE_SUDO cmake --install "$BUILD_DIR" --prefix "$PREFIX" >/dev/null
BIN="$PREFIX/bin/pacseek"
ok "installed: $BIN"

# --- PATH guidance: tailored to the caller's shell ----------------------------
BINDIR="$PREFIX/bin"
case ":$PATH:" in
  *":$BINDIR:"*)
    printf '\n%s\n' "${GOOD}${BOLD}Done.${RESET} Run ${BOLD}pacseek${RESET} from anywhere."
    ;;
  *)
    printf '\n%s\n' "${WARNC}${BOLD}Done, but $BINDIR is not on your PATH.${RESET}"
    printf '%s\n' "Run ${BOLD}$BIN${RESET} directly, or add it to your PATH:"
    case "$(basename "${SHELL:-}")" in
      fish) printf '  %s\n' "${DIM}fish_add_path $BINDIR${RESET}" ;;
      zsh)  printf '  %s\n' "${DIM}echo 'export PATH=\"$BINDIR:\$PATH\"' >> ~/.zshrc${RESET}" ;;
      *)    printf '  %s\n' "${DIM}echo 'export PATH=\"$BINDIR:\$PATH\"' >> ~/.bashrc${RESET}" ;;
    esac
    ;;
esac
