// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Screen 1, Music Library (docs/ui-draft/music-library.jpeg): browse pane on the left,
// and on the right an album grid, an album, a playlist, or search results.
// Owns the browse state (kept while other screens are shown) and which of its two panes
// has keyboard focus. The window routes keyboard actions here via handleAction().
Item {
    id: root

    enum Pane { Browse, Content }
    enum Content { Grid, Album, Playlist, Search, Hint }

    property string searchText
    property bool active: false // this screen has keyboard focus
    property int focusedPane: LibraryView.Browse

    // Grid filter (0 = all).
    property int artistId: 0
    property int genreId: 0
    property string filterLabel
    // Open album (0 = none). albumFromGrid: opened from the grid, so Esc goes back to it.
    property int albumId: 0
    property string albumTitle
    property string albumArtist
    property int albumYear: 0
    property string albumCoverUrl
    property bool albumFromGrid: false
    // Open playlist (0 = none).
    property int playlistId: 0
    property string playlistName
    property int playlistItemCount: 0

    readonly property bool hasLibrary: Library.folders.length > 0
    readonly property int content: searchText !== "" ? LibraryView.Search
        : albumId !== 0 ? LibraryView.Album
        : browsePane.mode === BrowsePane.Playlists ? (playlistId !== 0 ? LibraryView.Playlist : LibraryView.Hint)
        : browsePane.mode === BrowsePane.Albums ? LibraryView.Hint
        : LibraryView.Grid

    readonly property alias browse: browsePane
    readonly property alias grid: albumGrid
    readonly property alias albumPage: albumView
    readonly property alias playlistPage: playlistView
    readonly property alias searchResults: searchView

    signal addFolderRequested()
    // Open the tag editor for these files (Main owns the dialog).
    signal editTagsRequested(list<string> paths, string label)
    // The user clicked inside this screen (the window takes keyboard focus off the queue).
    signal interacted()
    // Open the artwork finder (Main owns the dialog).
    signal findArtRequested(int albumId)
    signal findMissingArtRequested()

    function openAlbum(id: int, title: string, artist: string, year: int, coverUrl: string, fromGrid: bool) {
        albumId = id;
        albumTitle = title;
        albumArtist = artist;
        albumYear = year;
        albumCoverUrl = coverUrl;
        albumFromGrid = fromGrid;
    }

    // Tag editing (`e` / `E`): on a playlist or search results, the cursor track or all
    // listed tracks; the album page also has marked tracks (see AlbumPage.editTags).
    function editTags(all: bool) {
        if (content === LibraryView.Album) {
            albumView.editTags(all);
            return;
        }
        let model = null;
        let cursor = -1;
        let label = "";
        if (content === LibraryView.Playlist) {
            model = playlistView.tracksModel;
            cursor = playlistView.currentIndex;
            label = playlistName;
        } else if (content === LibraryView.Search) {
            model = searchView.tracksModel;
            cursor = searchView.currentIndex;
            label = qsTr("Search: %1").arg(searchText);
        } else {
            return;
        }
        const paths = [];
        for (let i = all ? 0 : cursor; i >= 0 && i < (all ? model.count : cursor + 1); ++i)
            paths.push(model.get(i).path);
        if (paths.length > 0)
            editTagsRequested(paths, paths.length === 1 ? paths[0] : qsTr("%1 — %2 tracks").arg(label).arg(paths.length));
    }

    // After a save: if every track of the open album was edited into another album (e.g. it
    // was renamed), follow it there.
    function tagsSaved(newAlbumId: int, editedCount: int) {
        albumView.clearMarks();
        if (content === LibraryView.Album && newAlbumId !== 0 && newAlbumId !== albumId
                && editedCount === albumView.trackCount)
            albumId = newAlbumId;
    }

    // The album the cursor is on (`a` finds its cover): the open album, the grid's or browse
    // list's current album, or the cursor track's album. 0 if none.
    function currentAlbumId(): int {
        if (content === LibraryView.Album)
            return albumId;
        if (focusedPane === LibraryView.Browse)
            return browsePane.currentAlbumId();
        if (content === LibraryView.Grid)
            return albumGrid.currentAlbumId();
        let model = null;
        let cursor = -1;
        if (content === LibraryView.Playlist) {
            model = playlistView.tracksModel;
            cursor = playlistView.currentIndex;
        } else if (content === LibraryView.Search) {
            model = searchView.tracksModel;
            cursor = searchView.currentIndex;
        }
        return model && cursor >= 0 && cursor < model.count ? model.get(cursor).albumId : 0;
    }

    function closeAlbum() {
        albumId = 0;
        albumFromGrid = false;
    }

    function focusContent() {
        if (content !== LibraryView.Hint)
            focusedPane = LibraryView.Content;
    }

    function applySelection(mode: int, entry: var) {
        switch (mode) {
        case BrowsePane.Artists:
            artistId = entry ? entry.artistId : 0;
            genreId = 0;
            filterLabel = entry ? entry.name : "";
            closeAlbum();
            break;
        case BrowsePane.Genres:
            genreId = entry ? entry.genreId : 0;
            artistId = 0;
            filterLabel = entry ? entry.name : "";
            closeAlbum();
            break;
        case BrowsePane.Albums:
            if (entry)
                openAlbum(entry.albumId, entry.title, entry.artist, entry.year, entry.coverUrl, false);
            break;
        case BrowsePane.Playlists:
            closeAlbum();
            if (browsePane.mode !== BrowsePane.Playlists)
                browsePane.setMode(BrowsePane.Playlists); // e.g. right after an import
            playlistId = entry ? entry.playlistId : 0;
            playlistName = entry ? entry.name : "";
            playlistItemCount = entry ? entry.itemCount : 0;
            break;
        }
    }

    // h/j/k/l, Enter. Returns false when "move right" runs off the right edge (the window
    // then moves focus to the queue, if it's open).
    function handleAction(action: int): bool {
        if (!hasLibrary) {
            if (action === KeyRouter.Activate)
                root.addFolderRequested();
            return action !== KeyRouter.MoveRight;
        }
        const inBrowse = focusedPane === LibraryView.Browse;
        switch (action) {
        case KeyRouter.MoveLeft:
            if (!inBrowse && !(content === LibraryView.Grid && albumGrid.moveHorizontal(-1)))
                focusedPane = LibraryView.Browse;
            return true;
        case KeyRouter.MoveRight:
            if (inBrowse) {
                browsePane.commit();
                focusContent();
                return true;
            }
            return content === LibraryView.Grid && albumGrid.moveHorizontal(1);
        case KeyRouter.MoveDown:
        case KeyRouter.MoveUp: {
            const delta = action === KeyRouter.MoveDown ? 1 : -1;
            if (inBrowse)
                browsePane.moveSelection(delta);
            else if (content === LibraryView.Grid)
                albumGrid.moveVertical(delta);
            else if (content === LibraryView.Album)
                albumView.moveSelection(delta);
            else if (content === LibraryView.Playlist)
                playlistView.moveSelection(delta);
            else if (content === LibraryView.Search)
                searchView.moveSelection(delta);
            return true;
        }
        case KeyRouter.EditTags:
        case KeyRouter.EditAllTags:
            if (!inBrowse)
                editTags(action === KeyRouter.EditAllTags);
            return true;
        case KeyRouter.ToggleMark:
            if (!inBrowse && content === LibraryView.Album)
                albumView.toggleMarked();
            return true;
        case KeyRouter.Activate:
            if (inBrowse) {
                browsePane.commit();
                focusContent();
            } else if (content === LibraryView.Grid) {
                albumGrid.activateCurrent();
            } else if (content === LibraryView.Album) {
                albumView.activateCurrent();
            } else if (content === LibraryView.Playlist) {
                playlistView.activateCurrent();
            } else if (content === LibraryView.Search) {
                searchView.activateCurrent();
            }
            return true;
        }
        return false;
    }

    // Esc inside the screen: album opened from the grid → back to the grid (same position).
    // Returns false when there's nothing to back out of here.
    function back(): bool {
        if (content === LibraryView.Album && albumView.markedRows.length > 0) {
            albumView.clearMarks();
            return true;
        }
        if (content === LibraryView.Album && albumFromGrid) {
            closeAlbum();
            return true;
        }
        return false;
    }

    // Keep the cursor's pane valid when the right side changes under it.
    onContentChanged: if (content === LibraryView.Hint) focusedPane = LibraryView.Browse

    EmptyLibrary {
        anchors.fill: parent
        visible: !root.hasLibrary
        onAddFolderRequested: root.addFolderRequested()
    }

    SplitView {
        anchors.fill: parent
        visible: root.hasLibrary

        handle: Rectangle {
            implicitWidth: 1
            color: Theme.border
        }

        BrowsePane {
            id: browsePane
            SplitView.preferredWidth: Math.round(root.width * 0.25)
            SplitView.minimumWidth: 190
            SplitView.maximumWidth: 380
            paneActive: root.active && root.focusedPane === LibraryView.Browse
            onSelected: (mode, entry) => root.applySelection(mode, entry)
            onModeChanged: {
                root.closeAlbum();
                root.playlistId = 0;
            }
            onInteracted: {
                root.focusedPane = LibraryView.Browse;
                root.interacted();
            }
        }

        Item {
            id: contentArea
            SplitView.fillWidth: true
            readonly property bool contentActive: root.active && root.focusedPane === LibraryView.Content

            ColumnLayout {
                anchors.fill: parent
                visible: root.content === LibraryView.Grid
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.spaceMD
                    spacing: Theme.spaceMD

                    Label {
                        Layout.fillWidth: true
                        text: root.filterLabel !== "" ? root.filterLabel
                            : root.genreId === 0 && root.artistId === 0 ? qsTr("All albums") : ""
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Button {
                        objectName: "findMissingArtButton"
                        visible: Library.missingArtCount > 0
                        text: qsTr("Find missing covers (%1)…").arg(Library.missingArtCount)
                        flat: true
                        focusPolicy: Qt.NoFocus
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Look up covers for albums without art on MusicBrainz (A)")
                        onClicked: root.findMissingArtRequested()
                    }
                }

                AlbumGrid {
                    id: albumGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.spaceSM
                    Layout.rightMargin: Theme.spaceSM
                    artistId: root.artistId
                    genreId: root.genreId
                    paneActive: contentArea.contentActive
                    onAlbumActivated: (id, title, artist, year, coverUrl) => {
                        root.openAlbum(id, title, artist, year, coverUrl, true);
                        root.focusedPane = LibraryView.Content;
                        root.interacted();
                    }
                }
            }

            FocusBar {
                active: contentArea.contentActive && root.content === LibraryView.Grid
            }

            AlbumPage {
                id: albumView
                anchors.fill: parent
                visible: root.content === LibraryView.Album
                albumId: root.albumId
                albumTitle: root.albumTitle
                albumArtist: root.albumArtist
                year: root.albumYear
                coverUrl: root.albumCoverUrl
                showBack: root.albumFromGrid
                paneActive: contentArea.contentActive
                onBack: root.closeAlbum()
                onEditRequested: (paths, label) => root.editTagsRequested(paths, label)
                onFindArtRequested: id => root.findArtRequested(id)
                onInteracted: {
                    root.focusedPane = LibraryView.Content;
                    root.interacted();
                }
            }

            PlaylistPage {
                id: playlistView
                anchors.fill: parent
                visible: root.content === LibraryView.Playlist
                playlistId: root.playlistId
                playlistName: root.playlistName
                itemCount: root.playlistItemCount
                paneActive: contentArea.contentActive
                onClosed: root.playlistId = 0
                onInteracted: {
                    root.focusedPane = LibraryView.Content;
                    root.interacted();
                }
            }

            SearchResults {
                id: searchView
                anchors.fill: parent
                visible: root.content === LibraryView.Search
                searchText: root.searchText
                paneActive: contentArea.contentActive
                onInteracted: {
                    root.focusedPane = LibraryView.Content;
                    root.interacted();
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.content === LibraryView.Hint
                text: root.browse.mode === BrowsePane.Playlists ? qsTr("Pick a playlist on the left")
                                                                 : qsTr("Pick an album on the left")
                color: Theme.textMuted
            }
        }
    }
}
