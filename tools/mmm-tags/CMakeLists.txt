# SPDX-License-Identifier: GPL-3.0-or-later
# Prints what TagReader sees in audio files; optionally extracts covers / loads a DB.
qt_add_executable(mmm-tags main.cpp)
target_link_libraries(mmm-tags PRIVATE mmm_core mmm_build_flags)
mmm_add_sanitizer_defaults(mmm-tags)
