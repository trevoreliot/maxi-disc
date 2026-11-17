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
    title: AppBridge.displayName

    // Browse state.
    property int artistId: 0
    property int genreId: 0
    property string filterLabel: ""
    // Open album (0 = album grid).
    property int albumId: 0
    property string albumTitle
    property string albumArtist
    property int albumYear: 0
    property string albumCoverUrl

    readonly property bool hasLibrary: Library.folders.length > 0
    readonly property bool searching: searchField.text.trim() !== ""

    function openAlbum(id: int, title: string, artist: string, year: int, coverUrl: string) {
        albumId = id;
        albumTitle = title;
        albumArtist = artist;
        albumYear = year;
        albumCoverUrl = coverUrl;
    }

    function closeAlbum() {
        albumId = 0;
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
                visible: root.filterLabel !== "" && !root.searching
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

    footer: ToolBar {
        visible: Library.errorString !== ""

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8

            Label {
                Layout.fillWidth: true
                text: Library.errorString
                elide: Text.ElideRight
                color: "#d9534f"
            }
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
        enabled: !root.searching && root.albumId !== 0
        onActivated: root.closeAlbum()
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

            onArtistSelected: (id, name) => {
                root.artistId = id;
                root.genreId = 0;
                root.filterLabel = name;
                root.closeAlbum();
                searchField.text = "";
            }
            onGenreSelected: (id, name) => {
                root.genreId = id;
                root.artistId = 0;
                root.filterLabel = name;
                root.closeAlbum();
                searchField.text = "";
            }
        }

        Item {
            SplitView.fillWidth: true

            AlbumGrid {
                anchors.fill: parent
                anchors.margins: 8
                visible: !root.searching && root.albumId === 0
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

            SearchResults {
                id: searchResults
                anchors.fill: parent
                visible: root.searching
            }
        }
    }
}
