cmake_minimum_required(VERSION 3.20)

project(pacseek
  VERSION 0.1.0
  DESCRIPTION "Tech-brutalist TUI package manager for pacman + AUR"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- FTXUI: fetched and pinned for reproducible builds ----------------------
include(FetchContent)
set(FTXUI_PINNED_TAG "v6.1.9")
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG ${FTXUI_PINNED_TAG}
  GIT_SHALLOW TRUE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# FTXUI is statically linked into pacseek, so don't install its artifacts when
# we install pacseek itself.
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

# --- System libraries via pkg-config ----------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALPM REQUIRED IMPORTED_TARGET libalpm)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(nlohmann_json QUIET)

# --- Sources ----------------------------------------------------------------
add_executable(pacseek
  src/main.cpp
  src/model/package.cpp
  src/model/catalog.cpp
  src/model/collection.cpp
  src/data/mock_source.cpp
  src/data/alpm_source.cpp
  src/data/flatpak_source.cpp
  src/data/composite_source.cpp
  src/system/transaction.cpp
  src/system/aur_client.cpp
  src/config/config.cpp
  src/config/collections.cpp
  src/ui/components.cpp
  src/app/app.cpp)

target_include_directories(pacseek PRIVATE src)

target_link_libraries(pacseek PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  PkgConfig::ALPM
  PkgConfig::CURL)

if(nlohmann_json_FOUND)
  target_link_libraries(pacseek PRIVATE nlohmann_json::nlohmann_json)
  # Gates the real AUR RPC parser in system/aur_client.cpp; without JSON the
  # client compiles to a stub that reports the feature as unavailable.
  target_compile_definitions(pacseek PRIVATE PACSEEK_HAVE_JSON)
endif()

target_compile_options(pacseek PRIVATE -Wall -Wextra)

# --- Install ----------------------------------------------------------------
# `cmake --install build` drops the binary in <prefix>/bin. Default prefix is
# /usr/local (system-wide, needs root); pass --prefix ~/.local for a user install.
include(GNUInstallDirs)
install(TARGETS pacseek RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
