# SPDX-License-Identifier: GPL-3.0-or-later
qt_add_library(mmm_ui STATIC)

# Q1: the right-click menus' hub (views describe their menu; Main shows it).
set_source_files_properties(qml/ContextMenus.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        qml/AudioAnalyzer.qml
        qml/SpinningDisc.qml
        qml/PillToggle.qml
        qml/NavRow.qml
        qml/FocusBar.qml
        qml/QueuePanel.qml
        qml/PlaylistPage.qml
        qml/TextPrompt.qml
        qml/TagEditorDialog.qml
        qml/ArtworkFinderDialog.qml
        qml/ProfileEditorDialog.qml
        qml/SyncPane.qml
        qml/OnDevicePane.qml
        qml/MiniDiscPane.qml
        qml/RenameMdDialog.qml
        qml/ConfirmDialog.qml
        qml/ListenBrainzDialog.qml
        qml/DiscordDialog.qml
        qml/Toast.qml
        qml/SendToDeviceButton.qml
        qml/AppContextMenu.qml
        qml/ContextMenus.qml
    SOURCES
        appbridge.h appbridge.cpp
        library.h library.cpp
        librarymodels.h librarymodels.cpp
        coverimageprovider.h coverimageprovider.cpp
        playback.h playback.cpp
        spectrummeter.h spectrummeter.cpp
        listenbrainz.h listenbrainz.cpp
        desktop.h desktop.cpp
        discord.h discord.cpp
        keyrouter.h keyrouter.cpp
        tageditor.h tageditor.cpp
        artfinder.h artfinder.cpp
        devices.h devices.cpp
        profileeditor.h profileeditor.cpp
        sync.h sync.cpp
        ondevice.h ondevice.cpp
        minidisc.h minidisc.cpp
        sendtodevice.h sendtodevice.cpp
        theme.h theme.cpp)

target_link_libraries(mmm_ui
    PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent mmm_core
    PRIVATE mmm_build_flags)
target_include_directories(mmm_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
