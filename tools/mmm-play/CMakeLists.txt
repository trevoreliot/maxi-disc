# SPDX-License-Identifier: GPL-3.0-or-later
# Command-line player for exercising mmm::Player (M1 user test, debugging).
qt_add_executable(mmm-play main.cpp)
target_link_libraries(mmm-play PRIVATE mmm_core mmm_build_flags)
mmm_add_sanitizer_defaults(mmm-play)
