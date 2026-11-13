# SPDX-License-Identifier: GPL-3.0-or-later
# Object library with built-in ASan/LSan defaults; link into executables only.
add_library(mmm_sanitizer_defaults OBJECT sanitizer_defaults.cpp)
target_link_libraries(mmm_sanitizer_defaults PRIVATE mmm_build_flags)
