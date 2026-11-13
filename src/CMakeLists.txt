# SPDX-License-Identifier: GPL-3.0-or-later
if(MMM_SANITIZE)
    add_subdirectory(sanitizer)
endif()
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)
