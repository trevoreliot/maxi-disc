# SPDX-License-Identifier: LGPL-2.1-or-later
# libnetmd from linux-minidisc (see UPSTREAM.md), built as a C static library. It isn't our
# code: no mmm_build_flags, warnings off; its headers are SYSTEM so our -Werror code can
# include them.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(LIBGCRYPT REQUIRED IMPORTED_TARGET libgcrypt)

add_library(netmd STATIC
    common.c
    error.c
    libnetmd.c
    log.c
    netmd_dev.c
    playercontrol.c
    secure.c
    trackinformation.c
    utils.c)
set_target_properties(netmd PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_compile_options(netmd PRIVATE -w)
target_compile_definitions(netmd PRIVATE CONFIG_WITH_GCRYPT)
target_include_directories(netmd SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(netmd PUBLIC PkgConfig::LIBUSB PRIVATE PkgConfig::LIBGCRYPT)
