// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Browse by album artist or by genre. Id 0 means "all".
Pane {
    id: root

    property int artistId: 0
    property int genreId: 0

    // Emitted when the user picks an entry; name is "" for "all".
    signal artistSelected(int artistId, string name)
    signal genreSelected(int genreId, string name)

    padding: 0

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
        }
    }
}
