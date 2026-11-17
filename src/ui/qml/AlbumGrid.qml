// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Grid of album covers, optionally filtered by album artist and/or genre.
GridView {
    id: grid

    property alias artistId: albumModel.artistId
    property alias genreId: albumModel.genreId
    readonly property alias loading: albumModel.loading
    property int tileSize: 170

    signal albumActivated(int albumId, string title, string artist, int year, string coverUrl)

    readonly property int columns: Math.max(1, Math.floor(width / (tileSize + 24)))
    cellWidth: Math.floor(width / columns)
    cellHeight: tileSize + 60
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // Cache rows just outside the viewport so covers are ready when scrolled into view.
    cacheBuffer: cellHeight * 2
    keyNavigationEnabled: true
    focus: true

    model: AlbumListModel {
        id: albumModel
    }

    ScrollBar.vertical: ScrollBar {}

    delegate: ItemDelegate {
        id: tile

        required property int index
        required property int albumId
        required property string title
        required property string artist
        required property int year
        required property string coverUrl

        width: grid.cellWidth
        height: grid.cellHeight
        padding: 8
        highlighted: GridView.isCurrentItem && grid.activeFocus

        onClicked: {
            grid.currentIndex = index;
            grid.albumActivated(albumId, title, artist, year, coverUrl);
        }

        contentItem: Column {
            spacing: 4

            CoverArt {
                anchors.horizontalCenter: parent.horizontalCenter
                width: grid.tileSize
                height: grid.tileSize
                source: tile.coverUrl
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.title
                elide: Text.ElideRight
                font.bold: true
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.year > 0 ? tile.artist + " · " + tile.year : tile.artist
                elide: Text.ElideRight
                opacity: 0.7
            }
        }
    }

    Keys.onReturnPressed: {
        if (currentItem)
            (currentItem as ItemDelegate).clicked();
    }

    Label {
        anchors.centerIn: parent
        visible: grid.count === 0 && !grid.loading
        text: qsTr("No albums")
        opacity: 0.6
    }
}
