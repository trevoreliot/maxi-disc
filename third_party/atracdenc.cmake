# SPDX-License-Identifier: GPL-3.0-or-later
# atracdenc (vendored, LGPL-2.1; third_party/atracdenc/UPSTREAM.md): the ATRAC3 encoder for
# MiniDisc LP2 / LP4 (M12). Built as its own CMake project from the local source (no download,
# none of our flags, its unit tests off) and run by the app as a separate program
# (AtracEncoder). The binary lands next to the app: ${MMM_ATRACDENC_PATH}.
include(ExternalProject)

set(MMM_ATRACDENC_PATH "${CMAKE_BINARY_DIR}/src/app/atracdenc")
ExternalProject_Add(atracdenc_build
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/atracdenc"
    BINARY_DIR "${CMAKE_BINARY_DIR}/third_party/atracdenc-build"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON
        # GCC 15+ no longer includes <cstdint> through other headers; 0.2.3 relies on that.
        "-DCMAKE_CXX_FLAGS=-include cstdint"
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target atracdenc
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/src/app"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different <BINARY_DIR>/src/atracdenc "${MMM_ATRACDENC_PATH}"
    BUILD_BYPRODUCTS "${MMM_ATRACDENC_PATH}"
    BUILD_ALWAYS OFF
    UPDATE_COMMAND ""
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON)
