# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_library(mmm_ui STATIC)

qt_add_qml_module(mmm_ui
    URI MusicMdManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Sidebar.qml
        qml/AlbumGrid.qml
        qml/AlbumPage.qml
        qml/SearchResults.qml
        qml/CoverArt.qml
        qml/EmptyLibrary.qml
    SOURCES
        appbridge.h appbridge.cpp
        library.h library.cpp
        librarymodels.h librarymodels.cpp
        coverimageprovider.h coverimageprovider.cpp)

target_link_libraries(mmm_ui
    PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent mmm_core
    PRIVATE mmm_build_flags)
target_include_directories(mmm_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
