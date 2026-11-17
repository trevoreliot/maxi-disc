# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_executable(music-md-manager main.cpp)

target_link_libraries(music-md-manager PRIVATE
    mmm_ui mmm_uiplugin mmm_core Qt6::QuickControls2 mmm_build_flags)
mmm_add_sanitizer_defaults(music-md-manager)

# Lets the QML Profiler attach (qmlprofiler / -qmljsdebugger=...). Inert unless that
# command-line argument is passed.
option(MMM_QML_DEBUG "Enable the QML debugging/profiling server" ON)
if(MMM_QML_DEBUG)
    target_compile_definitions(music-md-manager PRIVATE QT_QML_DEBUG)
endif()
