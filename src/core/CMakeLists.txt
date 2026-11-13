# SPDX-License-Identifier: GPL-3.0-or-later
# Core: C++ and Qt Core only. No QML / GUI dependencies — must be testable headless.
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/mmm/version.h" @ONLY)

add_library(mmm_core STATIC
    appinfo.h appinfo.cpp)
target_include_directories(mmm_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mmm_core
    PUBLIC Qt6::Core
    PRIVATE mmm_build_flags)
