// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Grid of album covers, optionally filtered by album artist and/or genre.
// Keyboard (driven by the window's KeyRouter): moveHorizontal / moveVertical move the
// cursor; activateCurrent() opens the album.
GridView {
    id: grid

    property alias artistId: albumModel.artistId
    property alias genreId: albumModel.genreId
    readonly property alias loading: albumModel.loading
    property int tileSize: 170
    property bool paneActive: false

    signal albumActivated(int albumId, string title, string artist, int year, string coverUrl)

    readonly property int columns: Math.max(1, Math.floor(width / (tileSize + 24)))

    // Returns false at the left/right edge, so the caller can move focus to the next pane.
    function moveHorizontal(delta: int): bool {
        if (count === 0)
            return false;
        const i = Math.max(0, currentIndex);
        const col = i % columns;
        if ((delta < 0 && col === 0) || (delta > 0 && (col === columns - 1 || i === count - 1)))
            return false;
        setCurrent(i + delta);
        return true;
    }

    function moveVertical(delta: int) {
        if (count === 0)
            return;
        const i = Math.max(0, currentIndex);
        const target = i + delta * columns;
        if (target < 0)
            return;
        if (target >= count) {
            // Moving down into a shorter last row: land on its last album.
            if (Math.floor(i / columns) < Math.floor((count - 1) / columns))
                setCurrent(count - 1);
            return;
        }
        setCurrent(target);
    }

    function setCurrent(i: int) {
        currentIndex = i;
        positionViewAtIndex(i, GridView.Contain);
    }

    function activateCurrent() {
        if (currentItem)
            (currentItem as AlbumTile).open();
    }

    cellWidth: Math.floor(width / columns)
    cellHeight: tileSize + 60
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // Cache rows just outside the viewport so covers are ready when scrolled into view.
    cacheBuffer: cellHeight * 2
    keyNavigationEnabled: false // keys come from KeyRouter
    highlightFollowsCurrentItem: false

    model: AlbumListModel {
        id: albumModel
    }

    ScrollBar.vertical: ScrollBar {}

    component AlbumTile: ItemDelegate {
        id: tile

        required property int index
        required property int albumId
        required property string title
        required property string artist
        required property int year
        required property string coverUrl

        readonly property bool current: GridView.isCurrentItem

        function open() {
            grid.albumActivated(albumId, title, artist, year, coverUrl);
        }

        width: grid.cellWidth
        height: grid.cellHeight
        padding: 8
        focusPolicy: Qt.NoFocus

        onClicked: {
            grid.currentIndex = index;
            open();
        }

        background: Rectangle {
            radius: Theme.radiusMedium
            color: tile.current ? (grid.paneActive ? Theme.rowActiveFocused : Theme.rowSelected)
                 : tile.hovered ? Theme.rowHover : "transparent"
            border.width: tile.current && grid.paneActive ? 2 : 0
            border.color: Theme.focusRing
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
                color: Theme.textSecondary
            }
        }
    }

    delegate: AlbumTile {}

    Label {
        anchors.centerIn: parent
        visible: grid.count === 0 && !grid.loading
        text: qsTr("No albums")
        color: Theme.textMuted
    }
}
