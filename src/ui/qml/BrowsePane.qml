// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Left pane of the Library and Export screens (docs/ui-draft): a dropdown picks what to
// browse (artists, genres, albums, playlists) and the list below drives the right side.
// Keyboard: moveSelection() moves the cursor and selects after a short pause, so holding
// j/k doesn't reload the right side for every row; commit() selects right away.
// Artists and genres have an "All" row at index -1.
Pane {
    id: root

    enum Mode { Artists, Genres, Albums, Playlists }

    property int mode: BrowsePane.Artists
    property bool paneActive: false
    property bool showPlaylistTools: true

    readonly property ListView currentList: [artistList, genreList, albumList, playlistList][mode]
    readonly property bool hasAllRow: mode === BrowsePane.Artists || mode === BrowsePane.Genres
    // The cursor is on the first row ("All", if there is one): "up" would leave the list.
    readonly property bool atTop: currentList.count === 0 || currentList.currentIndex <= (hasAllRow ? -1 : 0)

    // `entry` is the row's roles (LibraryListModel.get), or null for the "All" row.
    signal selected(int mode, var entry)
    // The user clicked in this pane (the window gives it keyboard focus).
    signal interacted()
    // A row (not "All") was double-clicked; it's the current row now (Export adds it).
    signal rowDoubleClicked()

    // The row under the cursor as {kind, id, name} (a device sync entry: kind is "artist",
    // "genre", "album" or "playlist"), or null on an "All" row or an empty list.
    function currentItem(): var {
        const i = currentList.currentIndex;
        if (i < 0)
            return null;
        const e = [artistModel, genreModel, albumModel, playlistModel][mode].get(i);
        switch (mode) {
        case BrowsePane.Artists:
            return { kind: "artist", id: e.artistId, name: e.name };
        case BrowsePane.Genres:
            return { kind: "genre", id: e.genreId, name: e.name };
        case BrowsePane.Albums:
            return { kind: "album", id: e.albumId, name: e.artist ? e.title + " – " + e.artist : e.title };
        default:
            return { kind: "playlist", id: e.playlistId, name: e.name };
        }
    }

    function moveSelection(delta: int) {
        const list = currentList;
        if (list.count === 0)
            return;
        const i = Math.max(hasAllRow ? -1 : 0, Math.min(list.count - 1, list.currentIndex + delta));
        if (i === list.currentIndex)
            return;
        list.currentIndex = i;
        if (i >= 0)
            list.positionViewAtIndex(i, ListView.Contain);
        else
            list.positionViewAtBeginning();
        commitTimer.restart();
    }

    // The album under the cursor in Albums mode, else 0.
    function currentAlbumId(): int {
        const i = albumList.currentIndex;
        return mode === BrowsePane.Albums && i >= 0 ? albumModel.get(i).albumId : 0;
    }

    function commit() {
        commitTimer.stop();
        const i = currentList.currentIndex;
        if (i < 0) {
            if (hasAllRow)
                root.selected(mode, null);
            return;
        }
        const models = [artistModel, genreModel, albumModel, playlistModel];
        root.selected(mode, models[mode].get(i));
    }

    function setMode(m: int) {
        if (m === mode)
            return;
        mode = m;
        modeBox.currentIndex = m;
        commit();
    }

    // Right-click menus (Q1; the library's browse pane, not Export's yet). The row with a menu
    // open is outlined; it isn't selected (what's on the right stays).
    property bool contextMenus: false
    property int contextTarget: -1

    function contextEntries(index: int): var {
        const list = currentList;
        if (index < 0 || index >= list.count)
            return []; // "All artists" / "All genres": not the whole library from one click
        const r = list.model.get(index);
        switch (mode) {
        case BrowsePane.Artists:
            return ContextMenus.itemEntries("artist", r.artistId, r.name);
        case BrowsePane.Genres:
            return ContextMenus.itemEntries("genre", r.genreId, r.name);
        case BrowsePane.Albums:
            return ContextMenus.albumEntries(r.albumId, r.title);
        case BrowsePane.Playlists:
            return ContextMenus.playlistEntries(r.playlistId, r.name, r.availableCount);
        }
        return [];
    }

    function openContextMenu(index: int, item: Item, x: real, y: real) {
        if (!contextMenus)
            return;
        const entries = contextEntries(index);
        if (entries.length === 0)
            return;
        contextTarget = index;
        ContextMenus.open(item, x, y, entries, () => { root.contextTarget = -1; });
    }

    // `,` / Menu: the cursor's row.
    function openContextMenuForCurrent() {
        const list = currentList;
        const item = list.currentItem;
        if (item)
            openContextMenu(list.currentIndex, item, Math.min(item.width * 0.35, 220), item.height);
    }

    function clickRow(list: ListView, index: int) {
        list.currentIndex = index;
        root.interacted();
        commit();
    }

    padding: 0
    background: Rectangle {
        color: Theme.surface
    }

    ArtistListModel {
        id: artistModel
    }
    GenreListModel {
        id: genreModel
    }
    AlbumListModel {
        id: albumModel
    }
    PlaylistListModel {
        id: playlistModel
    }

    Timer {
        id: commitTimer
        interval: 80
        onTriggered: root.commit()
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import playlists")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Playlists (*.m3u *.m3u8)")]
        onAccepted: {
            const id = Library.importPlaylists(selectedFiles);
            if (id > 0) {
                // The last one imported, named like Library names it: the file's base name.
                const file = decodeURIComponent(selectedFiles[selectedFiles.length - 1].toString().split("/").pop());
                root.selected(BrowsePane.Playlists, { playlistId: id, name: file.replace(/\.m3u8?$/i, ""), itemCount: -1 });
            }
        }
    }

    FocusBar {
        active: root.paneActive
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ComboBox {
            id: modeBox
            objectName: "browseModeBox"
            Layout.fillWidth: true
            Layout.margins: Theme.spaceSM
            model: [qsTr("Artists"), qsTr("Genres"), qsTr("Albums"), qsTr("Playlists")]
            currentIndex: root.mode
            onActivated: (index) => {
                root.interacted();
                root.setMode(index);
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceSM
            Layout.rightMargin: Theme.spaceSM
            visible: root.showPlaylistTools && root.mode === BrowsePane.Playlists

            Button {
                Layout.fillWidth: true
                text: qsTr("Import .m3u…")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: importDialog.open()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.mode

            ListView {
                id: artistList
                objectName: "artistList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                model: artistModel
                ScrollBar.vertical: ScrollBar {}

                header: NavRow {
                    width: artistList.width
                    text: qsTr("All artists")
                    font.bold: true
                    current: artistList.currentIndex === -1
                    paneActive: root.paneActive
                    onClicked: root.clickRow(artistList, -1)
                }

                delegate: NavRow {
                    id: artistRow
                    required property int index
                    required property int artistId
                    required property string name
                    required property int albumCount

                    width: artistList.width
                    current: ListView.isCurrentItem
                    paneActive: root.paneActive
                    onClicked: root.clickRow(artistList, index)
                    onDoubleClicked: {
                        root.clickRow(artistList, index);
                        root.rowDoubleClicked();
                    }
                    menuTarget: root.contextTarget === index && root.currentList === artistList
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => root.openContextMenu(artistRow.index, artistRow, point.position.x, point.position.y)
                    }

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: artistRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: artistRow.albumCount
                            color: Theme.textMuted
                        }
                    }
                }
            }

            ListView {
                id: genreList
                objectName: "genreList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                model: genreModel
                ScrollBar.vertical: ScrollBar {}

                header: NavRow {
                    width: genreList.width
                    text: qsTr("All genres")
                    font.bold: true
                    current: genreList.currentIndex === -1
                    paneActive: root.paneActive
                    onClicked: root.clickRow(genreList, -1)
                }

                delegate: NavRow {
                    id: genreRow
                    required property int index
                    required property int genreId
                    required property string name
                    required property int albumCount

                    width: genreList.width
                    current: ListView.isCurrentItem
                    paneActive: root.paneActive
                    onClicked: root.clickRow(genreList, index)
                    onDoubleClicked: {
                        root.clickRow(genreList, index);
                        root.rowDoubleClicked();
                    }
                    menuTarget: root.contextTarget === index && root.currentList === genreList
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => root.openContextMenu(genreRow.index, genreRow, point.position.x, point.position.y)
                    }

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: genreRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: genreRow.albumCount
                            color: Theme.textMuted
                        }
                    }
                }
            }

            ListView {
                id: albumList
                objectName: "albumList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                model: albumModel
                ScrollBar.vertical: ScrollBar {}

                delegate: NavRow {
                    id: albumRow
                    required property int index
                    required property int albumId
                    required property string title
                    required property string artist

                    width: albumList.width
                    current: ListView.isCurrentItem
                    paneActive: root.paneActive
                    onClicked: root.clickRow(albumList, index)
                    onDoubleClicked: {
                        root.clickRow(albumList, index);
                        root.rowDoubleClicked();
                    }
                    menuTarget: root.contextTarget === index && root.currentList === albumList
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => root.openContextMenu(albumRow.index, albumRow, point.position.x, point.position.y)
                    }

                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: albumRow.title
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: albumRow.artist
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                        }
                    }
                }
            }

            ListView {
                id: playlistList
                objectName: "playlistList"
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: -1
                model: playlistModel
                ScrollBar.vertical: ScrollBar {}

                delegate: NavRow {
                    id: playlistRow
                    required property int index
                    required property int playlistId
                    required property string name
                    required property int availableCount

                    width: playlistList.width
                    current: ListView.isCurrentItem
                    paneActive: root.paneActive
                    onClicked: root.clickRow(playlistList, index)
                    onDoubleClicked: {
                        root.clickRow(playlistList, index);
                        root.rowDoubleClicked();
                    }
                    menuTarget: root.contextTarget === index && root.currentList === playlistList
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => root.openContextMenu(playlistRow.index, playlistRow, point.position.x, point.position.y)
                    }

                    contentItem: RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: playlistRow.name
                            elide: Text.ElideRight
                        }
                        Label {
                            text: playlistRow.availableCount
                            color: Theme.textMuted
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
                    color: Theme.textMuted
                }
            }
        }
    }
}
