# SPDX-License-Identifier: GPL-3.0-or-later
# Core: C++ and Qt Core only (plus non-GUI libs like GStreamer). No QML / GUI — testable headless.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.22)
find_package(TagLib 2.0 REQUIRED)

configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/mmm/version.h" @ONLY)

add_library(mmm_core STATIC
    appinfo.h appinfo.cpp
    gstinit.h gstinit.cpp
    player.h player.cpp
    tracktags.h
    tagreader.h tagreader.cpp
    database.h database.cpp
    apppaths.h apppaths.cpp
    libraryscanner.h libraryscanner.cpp)
target_include_directories(mmm_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mmm_core
    PUBLIC Qt6::Core Qt6::Sql
    PRIVATE Qt6::Concurrent
    PRIVATE PkgConfig::GST TagLib::tag mmm_build_flags)
