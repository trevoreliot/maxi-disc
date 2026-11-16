# SPDX-License-Identifier: GPL-3.0-or-later
# Manage library folders and scan them into the database (M3 user test, debugging).
qt_add_executable(mmm-scan main.cpp)
target_link_libraries(mmm-scan PRIVATE mmm_core mmm_build_flags)
mmm_add_sanitizer_defaults(mmm-scan)
