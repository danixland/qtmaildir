cmake_minimum_required(VERSION 3.21)
project(qtmaildir VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets WebEngineWidgets Test)

# notmuch ships no pkg-config file; locate it by hand.
find_path(NOTMUCH_INCLUDE_DIR notmuch.h)
find_library(NOTMUCH_LIBRARY NAMES notmuch)
if(NOT NOTMUCH_INCLUDE_DIR OR NOT NOTMUCH_LIBRARY)
    message(FATAL_ERROR
        "libnotmuch not found. Need notmuch.h and libnotmuch on the system.")
endif()
message(STATUS "Found notmuch: ${NOTMUCH_LIBRARY}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GMIME REQUIRED IMPORTED_TARGET gmime-3.0)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
