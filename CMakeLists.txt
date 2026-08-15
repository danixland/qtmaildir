cmake_minimum_required(VERSION 3.21)
project(qtmaildir VERSION 0.22.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Off for a packaging build, which has no use for the test binaries and would
# otherwise pull in Qt6::Test and compile the whole suite to produce nothing
# that ships.
option(QTMAILDIR_BUILD_TESTS "Build the test suite" ON)

set(QTMAILDIR_QT_COMPONENTS Widgets Svg WebEngineWidgets)
if(QTMAILDIR_BUILD_TESTS)
    list(APPEND QTMAILDIR_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${QTMAILDIR_QT_COMPONENTS})

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

# The version lives only in the project() call above; version.h is generated
# from it so no source file repeats the literal.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/qtmaildir/version.h
    @ONLY)

add_subdirectory(src)
if(QTMAILDIR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
