// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Browse by album artist, genre, or playlist. Id 0 means "all" / none.
Pane {
    id: root

    property int artistId: 0
    property int genreId: 0
    property int playlistId: 0

    // Emitted when the user picks an entry; name is "" for "all".
    signal artistSelected(int artistId, string name)
    signal genreSelected(int genreId, string name)
    signal playlistSelected(int playlistId, string name, int itemCount)

    padding: 0

    FileDialog {
        id: importDialog
        title: qsTr("Import playlist")
        nameFilters: [qsTr("Playlists (*.m3u *.m3u8)")]
        onAccepted: {
            const id = Library.importPlaylist(selectedFile);
            if (id > 0) {
                // Same name Library gives it: the file's base name.
                const file = decodeURIComponent(selectedFile.toString().split("/").pop());
                root.playlistSelected(id, file.replace(/\.m3u8?$/i, ""), -1);
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton {
                text: qsTr("Artists")
            }
            TabButton {
                text: qsTr("Genres")
            }
            TabButton {
                text: qsTr("Playlists")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ListView {
                id: artistList
                objectName: "artistList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: ArtistListModel {}
                ScrollBar.vertical: ScrollBar {}

                header: ItemDelegate {
                    width: artistList.width
                    text: qsTr("All artists")
                    font.bold: true
                    highlighted: root.artistId === 0
                    onClicked: root.artistSelected(0, "")
                }

                delegate: ItemDelegate {
                    id: artistRow
                    required property int artistId
                    required property string name
                    required property int albumCount

                    width: artistList.width
                    highlighted: root.artistId === artistId
                    onClicked: root.artistSelected(artistId, name)

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: artistRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: artistRow.albumCount
                            opacity: 0.5
                        }
                    }
                }
            }

            ListView {
                id: genreList
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: GenreListModel {}
                ScrollBar.vertical: ScrollBar {}

                header: ItemDelegate {
                    width: genreList.width
                    text: qsTr("All genres")
                    font.bold: true
                    highlighted: root.genreId === 0
                    onClicked: root.genreSelected(0, "")
                }

                delegate: ItemDelegate {
                    id: genreRow
                    required property int genreId
                    required property string name
                    required property int albumCount

                    width: genreList.width
                    highlighted: root.genreId === genreId
                    onClicked: root.genreSelected(genreId, name)

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: genreRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: genreRow.albumCount
                            opacity: 0.5
                        }
                    }
                }
            }

            ListView {
                id: playlistList
                objectName: "playlistList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: PlaylistListModel {}
                ScrollBar.vertical: ScrollBar {}

                header: ItemDelegate {
                    width: playlistList.width
                    text: qsTr("Import .m3u…")
                    font.bold: true
                    onClicked: importDialog.open()
                }

                delegate: ItemDelegate {
                    id: playlistRow
                    required property int playlistId
                    required property string name
                    required property int itemCount
                    required property int availableCount

                    width: playlistList.width
                    highlighted: root.playlistId === playlistId
                    onClicked: root.playlistSelected(playlistId, name, itemCount)

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: playlistRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: playlistRow.availableCount
                            opacity: 0.5
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: playlistList.count === 0
                    width: parent.width - 24
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No playlists yet. Save the queue as one, or import an .m3u file.")
                    opacity: 0.6
                }
            }
        }
    }
}
