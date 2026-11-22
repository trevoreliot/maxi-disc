# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)

project(music-md-manager
    VERSION 0.0.1
    DESCRIPTION "Music library manager with USB and NetMD MiniDisc sync"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MMM_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(MMM_WERROR "Treat compiler warnings as errors" ON)
option(BUILD_TESTING "Build tests" ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent DBus Network Sql Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.8)
qt_policy(SET QTP0004 NEW)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

# Warning / sanitizer flags applied to our own targets only (not Qt-generated code deps).
add_library(mmm_build_flags INTERFACE)
target_compile_options(mmm_build_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
    $<$<BOOL:${MMM_WERROR}>:-Werror>)
target_compile_definitions(mmm_build_flags INTERFACE
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
    QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS QT_NO_KEYWORDS)
if(MMM_SANITIZE)
    target_compile_options(mmm_build_flags INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(mmm_build_flags INTERFACE -fsanitize=address,undefined)
endif()

# Executables call this to pick up built-in sanitizer defaults (no-op unless MMM_SANITIZE).
function(mmm_add_sanitizer_defaults target)
    if(MMM_SANITIZE)
        target_link_libraries(${target} PRIVATE mmm_sanitizer_defaults)
    endif()
endfunction()

add_subdirectory(src)
add_subdirectory(tools/mmm-play)
add_subdirectory(tools/mmm-tags)
add_subdirectory(tools/mmm-scan)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
