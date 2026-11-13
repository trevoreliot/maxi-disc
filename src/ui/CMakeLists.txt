# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_library(mmm_ui STATIC)

qt_add_qml_module(mmm_ui
    URI MusicMdManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
    SOURCES
        appbridge.h appbridge.cpp)

target_link_libraries(mmm_ui
    PUBLIC Qt6::Quick Qt6::QuickControls2 mmm_core
    PRIVATE mmm_build_flags)
