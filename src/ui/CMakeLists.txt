# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_library(mmm_ui STATIC)

qt_add_qml_module(mmm_ui
    URI MusicMdManager
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/TopChrome.qml
        qml/MainMenuView.qml
        qml/MenuTile.qml
        qml/MenuIcon.qml
        qml/LibraryView.qml
        qml/ExportView.qml
        qml/BrowsePane.qml
        qml/AlbumGrid.qml
        qml/AlbumPage.qml
        qml/SearchResults.qml
        qml/CoverArt.qml
        qml/EmptyLibrary.qml
        qml/TransportBar.qml
        qml/TransportButton.qml
        qml/Glyph.qml
        qml/VolumeWedge.qml
        qml/AudioAnalyzerStub.qml
        qml/PillToggle.qml
        qml/NavRow.qml
        qml/FocusBar.qml
        qml/QueuePanel.qml
        qml/PlaylistPage.qml
        qml/TextPrompt.qml
        qml/TagEditorDialog.qml
        qml/ArtworkFinderDialog.qml
    SOURCES
        appbridge.h appbridge.cpp
        library.h library.cpp
        librarymodels.h librarymodels.cpp
        coverimageprovider.h coverimageprovider.cpp
        playback.h playback.cpp
        keyrouter.h keyrouter.cpp
        tageditor.h tageditor.cpp
        artfinder.h artfinder.cpp
        theme.h theme.cpp)

target_link_libraries(mmm_ui
    PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent mmm_core
    PRIVATE mmm_build_flags)
target_include_directories(mmm_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
