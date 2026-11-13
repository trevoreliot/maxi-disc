# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(music-md-manager main.cpp)

target_link_libraries(music-md-manager PRIVATE
    mmm_ui mmm_uiplugin mmm_core mmm_build_flags)
mmm_add_sanitizer_defaults(music-md-manager)
