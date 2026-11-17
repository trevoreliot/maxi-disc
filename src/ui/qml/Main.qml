// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1280
    height: 800
    visible: true
    title: Playback.hasTrack ? Playback.title + " — " + AppBridge.displayName : AppBridge.displayName

    // Browse state.
    property int artistId: 0
    property int genreId: 0
    property string filterLabel: ""
    // Open album (0 = none).
    property int albumId: 0
    property string albumTitle
    property string albumArtist
    property int albumYear: 0
    property string albumCoverUrl
    // Open playlist (0 = none).
    property int playlistId: 0
    property string playlistName
    property int playlistItemCount: 0

    readonly property bool hasLibrary: Library.folders.length > 0
    readonly property bool searching: searchField.text.trim() !== ""

    function openAlbum(id: int, title: string, artist: string, year: int, coverUrl: string) {
        playlistId = 0;
        albumId = id;
        albumTitle = title;
        albumArtist = artist;
        albumYear = year;
        albumCoverUrl = coverUrl;
    }

    function closeAlbum() {
        albumId = 0;
    }

    function openPlaylist(id: int, name: string, itemCount: int) {
        albumId = 0;
        playlistId = id;
        playlistName = name;
        playlistItemCount = itemCount;
        searchField.text = "";
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            ToolButton {
                text: "☰"
                onClicked: appMenu.open()

                Menu {
                    id: appMenu
                    y: parent.height

                    MenuItem {
                        text: qsTr("Add music folder…")
                        onTriggered: folderDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Rescan library")
                        enabled: root.hasLibrary
                        onTriggered: Library.rescan()
                    }
                    MenuSeparator {}
                    Menu {
                        title: qsTr("ReplayGain")

                        MenuItem {
                            text: qsTr("Album (keeps an album's dynamics)")
                            checkable: true
                            checked: Playback.replayGainMode === 2
                            onTriggered: Playback.replayGainMode = 2
                        }
                        MenuItem {
                            text: qsTr("Track")
                            checkable: true
                            checked: Playback.replayGainMode === 1
                            onTriggered: Playback.replayGainMode = 1
                        }
                        MenuItem {
                            text: qsTr("Off")
                            checkable: true
                            checked: Playback.replayGainMode === 0
                            onTriggered: Playback.replayGainMode = 0
                        }
                    }
                }
            }

            TextField {
                id: searchField
                objectName: "searchField"
                Layout.preferredWidth: 360
                placeholderText: qsTr("Search title, artist, album, genre…  (Ctrl+F)")
                enabled: root.hasLibrary
                selectByMouse: true
                Keys.onEscapePressed: text = ""
                onTextChanged: searchDebounce.restart()
            }

            Label {
                visible: root.filterLabel !== "" && !root.searching && root.playlistId === 0
                text: root.filterLabel
                font.bold: true
                elide: Text.ElideRight
                Layout.maximumWidth: 300
            }

            Item {
                Layout.fillWidth: true
            }

            BusyIndicator {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                running: Library.scanning
                visible: running
            }
            Label {
                opacity: 0.7
                text: Library.scanning
                    ? (Library.scanTotal > 0 ? qsTr("Scanning %1 / %2").arg(Library.scanProcessed).arg(Library.scanTotal)
                                             : qsTr("Checking for changes…"))
                    : qsTr("%1 tracks · %2 albums").arg(Library.trackCount).arg(Library.albumCount)
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: Library.errorString !== ""
            padding: 6

            RowLayout {
                anchors.fill: parent

                Label {
                    Layout.fillWidth: true
                    text: Library.errorString
                    elide: Text.ElideRight
                    color: "#d9534f"
                }
                ToolButton {
                    text: "✕"
                    onClicked: Library.clearError()
                }
            }
        }

        Pane {
            id: statusBanner
            Layout.fillWidth: true
            visible: false
            padding: 6

            Label {
                text: Library.statusMessage
                elide: Text.ElideRight
            }

            Connections {
                target: Library
                function onStatusMessageChanged() {
                    statusBanner.visible = Library.statusMessage !== "";
                    statusTimer.restart();
                }
            }
            Timer {
                id: statusTimer
                interval: 6000
                onTriggered: statusBanner.visible = false
            }
        }

        NowPlayingBar {
            id: nowPlaying
            Layout.fillWidth: true
        }
    }

    Timer {
        id: searchDebounce
        interval: 150
        onTriggered: searchResults.searchText = searchField.text.trim()
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose your music folder")
        onAccepted: Library.addFolder(selectedFolder)
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !root.searching && (root.albumId !== 0 || root.playlistId !== 0)
        onActivated: {
            root.albumId = 0;
            root.playlistId = 0;
        }
    }
    // Playback keys, disabled while typing so they don't eat spaces/arrows.
    Shortcut {
        sequence: "Space"
        enabled: !searchField.activeFocus
        onActivated: Playback.playPause()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: !searchField.activeFocus
        onActivated: Playback.next()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: !searchField.activeFocus
        onActivated: Playback.previous()
    }

    EmptyLibrary {
        anchors.fill: parent
        visible: !root.hasLibrary
        onAddFolderRequested: folderDialog.open()
    }

    SplitView {
        anchors.fill: parent
        visible: root.hasLibrary

        Sidebar {
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 160
            artistId: root.artistId
            genreId: root.genreId
            playlistId: root.playlistId

            onArtistSelected: (id, name) => {
                root.artistId = id;
                root.genreId = 0;
                root.filterLabel = name;
                root.albumId = 0;
                root.playlistId = 0;
                searchField.text = "";
            }
            onGenreSelected: (id, name) => {
                root.genreId = id;
                root.artistId = 0;
                root.filterLabel = name;
                root.albumId = 0;
                root.playlistId = 0;
                searchField.text = "";
            }
            onPlaylistSelected: (id, name, itemCount) => root.openPlaylist(id, name, itemCount)
        }

        Item {
            SplitView.fillWidth: true

            AlbumGrid {
                anchors.fill: parent
                anchors.margins: 8
                visible: !root.searching && root.albumId === 0 && root.playlistId === 0
                artistId: root.artistId
                genreId: root.genreId
                onAlbumActivated: (id, title, artist, year, coverUrl) => root.openAlbum(id, title, artist, year, coverUrl)
            }

            AlbumPage {
                anchors.fill: parent
                visible: !root.searching && root.albumId !== 0
                albumId: root.albumId
                albumTitle: root.albumTitle
                albumArtist: root.albumArtist
                year: root.albumYear
                coverUrl: root.albumCoverUrl
                onBack: root.closeAlbum()
            }

            PlaylistPage {
                anchors.fill: parent
                visible: !root.searching && root.playlistId !== 0
                playlistId: root.playlistId
                playlistName: root.playlistName
                itemCount: root.playlistItemCount
                onClosed: root.playlistId = 0
            }

            SearchResults {
                id: searchResults
                anchors.fill: parent
                visible: root.searching
            }
        }

        QueuePanel {
            visible: nowPlaying.queueVisible
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 220
        }
    }
}
