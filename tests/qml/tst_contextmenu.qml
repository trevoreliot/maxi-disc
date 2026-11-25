// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Right-click menus (Q1) through the real shell: album tiles, tracks (marked too), the queue,
// "Add to playlist", `,` and the menu's keys. Folders it would open are only recorded.
TestCase {
    id: tc
    name: "ContextMenu"
    when: windowShown

    Component {
        id: mainComp
        Main {}
    }

    SignalSpy {
        id: findArtSpy
        target: ContextMenus
        signalName: "findArtRequested"
    }

    function openMain() {
        const win = createTemporaryObject(mainComp, tc);
        tryVerify(() => {
            win.requestActivate();
            win.contentItem.forceActiveFocus();
            return win.active && win.activeFocusItem !== null;
        }, 5000, "app window never got keyboard focus");
        win.showScreen(Main.LibraryScreen);
        return win;
    }

    // The album's tile (the grid's count comes before its tiles: wait for the tile).
    function tile(win, title) {
        const grid = win.libraryView.grid;
        const find = () => {
            for (let i = 0; i < grid.count; ++i) {
                const t = grid.itemAtIndex(i);
                if (t && t.title === title)
                    return t;
            }
            return null;
        };
        tryVerify(() => find() !== null, 5000, "no tile for " + title);
        return find();
    }

    function menuTexts(menu) {
        const out = [];
        for (let i = 0; i < menu.count; ++i) {
            const it = menu.itemAt(i);
            if (it && it.text !== undefined && it.text !== "")
                out.push(it.text);
        }
        return out;
    }

    function menuItem(menu, text) {
        for (let i = 0; i < menu.count; ++i) {
            const it = menu.itemAt(i);
            if (it && it.text === text)
                return it;
        }
        return null;
    }

    // (A click right as a popup opens can get lost: let it settle, like a hand would.)
    function rightClick(item) {
        wait(50);
        mouseClick(item, item.width / 2, item.height / 2, Qt.RightButton);
    }

    function pick(menu, text) {
        tryCompare(menu, "opened", true);
        wait(100);
        const it = menuItem(menu, text);
        verify(it, "no menu item " + text);
        mouseClick(it);
    }

    function openAlbum(win, title) {
        const t = tile(win, title);
        mouseClick(t);
        tryCompare(win.libraryView, "content", LibraryView.Album);
        tryVerify(() => win.libraryView.albumPage.trackCount > 0);
        wait(100);
        return win.libraryView.albumPage;
    }

    property int playlistToDelete: 0

    function cleanup() {
        Playback.stop();
        Playback.clearQueue();
        if (playlistToDelete > 0)
            Library.deletePlaylist(playlistToDelete);
        playlistToDelete = 0;
    }

    function test_albumTile() {
        const win = openMain();
        const menu = win.contextMenu;
        rightClick(tile(win, "Abbey Road"));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Play", "Play next", "Add to queue", "Add to playlist", "Send to device",
                                  "Find cover…", "Edit tags…", "Open folder"]);
        verify(!menuItem(menu, "Send to device").enabled); // no device in the tests
        verify(win.libraryView.content === LibraryView.Grid); // right-click doesn't open it
        wait(100);
        compare(menu.currentIndex, -1); // opened by the mouse: no cursor yet
        keyClick("k"); // up from nothing: the last item
        compare(menu.itemAt(menu.currentIndex).text, "Open folder");
        keyClick("j"); // and round to the first
        compare(menu.itemAt(menu.currentIndex).text, "Play");
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/contextmenu-album.png");
        }
        pick(menu, "Add to queue");
        tryCompare(menu, "opened", false);
        tryCompare(Playback.queue, "count", 2); // Abbey Road's two tracks, read off the UI thread

        rightClick(tile(win, "Abbey Road"));
        tryCompare(menu, "opened", true);
        findArtSpy.clear();
        pick(menu, "Find cover…");
        tryCompare(findArtSpy, "count", 1);
        tryVerify(() => win.contentItem.parent !== null);
        keyClick(Qt.Key_Escape); // the finder dialog it opened
    }

    function test_commaAndTheMenusKeys() {
        const win = openMain();
        const menu = win.contextMenu;
        const lib = win.libraryView;
        lib.focusedPane = LibraryView.Content;
        lib.grid.setCurrent(tile(win, "Abbey Road").index);
        keyClick(",");
        tryCompare(menu, "opened", true);
        compare(menu.currentIndex, 0); // from the keyboard: the first item has the cursor
        keyClick("j");
        compare(menu.currentIndex, 1);
        keyClick(Qt.Key_Down);
        compare(menu.currentIndex, 2);
        keyClick("k");
        compare(menu.currentIndex, 1);
        keyClick(Qt.Key_Space); // nothing leaks behind the menu (space would play)
        verify(!Playback.playing);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
        verify(lib.content === LibraryView.Grid); // and Esc didn't go further
        keyClick(","); // straight away again
        tryCompare(menu, "opened", true);
        keyClick(Qt.Key_Return); // Play
        tryCompare(menu, "opened", false);
        tryVerify(() => Playback.queue.count > 0);
    }

    function test_addToAPlaylist() {
        const win = openMain();
        const menu = win.contextMenu;
        const id = Library.createPlaylist("Context test", []);
        verify(id > 0);
        tc.playlistToDelete = id;
        const lib = win.libraryView;
        lib.focusedPane = LibraryView.Content;
        lib.grid.setCurrent(tile(win, "Abbey Road").index);
        keyClick(",");
        tryCompare(menu, "opened", true);
        for (let i = 0; i < 3; ++i)
            keyClick("j"); // Play next, Add to queue, Add to playlist (the line is skipped)
        keyClick("l");
        tryVerify(() => menu.openSubmenu !== null);
        const sub = menu.openSubmenu;
        tryCompare(sub, "currentIndex", 0); // New playlist…
        tryVerify(() => menuTexts(sub).includes("Context test"));
        keyClick("j");
        keyClick(Qt.Key_Return);
        tryCompare(menu, "opened", false);
        tryCompare(Library, "statusMessage", "Added 2 tracks to “Context test”");
    }

    function test_markedTracks() {
        const win = openMain();
        const menu = win.contextMenu;
        const page = openAlbum(win, "The Wall");
        compare(page.trackCount, 3);
        const list = findChild(page, "trackList");
        const rowItem = i => list.itemAtIndex(i);
        // Two marked: their menu says so.
        mouseClick(rowItem(0), 10, 10, Qt.LeftButton, Qt.ControlModifier);
        mouseClick(rowItem(2), 10, 10, Qt.LeftButton, Qt.ControlModifier);
        compare(page.markedRows.length, 2);
        rightClick(rowItem(2));
        tryCompare(menu, "opened", true);
        const texts = menuTexts(menu);
        verify(texts.includes("Add 2 tracks to queue"), texts);
        verify(texts.includes("Unmark"));
        verify(texts.includes("Clear marks (2)"));
        verify(!texts.includes("Go to album")); // on its own page
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/contextmenu-marked.png");
        }
        pick(menu, "Add 2 tracks to queue");
        tryCompare(Playback.queue, "count", 2);
        compare(page.markedRows.length, 2); // marks stay
        // An unmarked row: just it, marks kept.
        rightClick(rowItem(1));
        tryCompare(menu, "opened", true);
        verify(menuTexts(menu).includes("Add to queue"));
        verify(menuTexts(menu).includes("Mark"));
        pick(menu, "Clear marks (2)");
        tryCompare(page.markedRows, "length", 0);
    }

    function test_openFolderAndGoToAlbum() {
        const win = openMain();
        const menu = win.contextMenu;
        const page = openAlbum(win, "Kid A");
        const list = findChild(page, "trackList");
        rightClick(list.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        const before = openedUrls.all().length;
        pick(menu, "Open containing folder");
        tryVerify(() => openedUrls.all().length === before + 1);
        verify(openedUrls.all()[before].endsWith("/Radiohead/Kid%20A") || openedUrls.all()[before].endsWith("/Radiohead/Kid A"),
               openedUrls.all()[before]);

        // From the queue: Go to album opens it in the library.
        Playback.playFromModel(page.tracksModel, 0);
        win.libraryView.closeAlbum();
        win.toggleQueue();
        const queueList = findChild(win.queuePanel, "queueList");
        tryVerify(() => queueList.count > 0 && queueList.itemAtIndex(0) !== null);
        rightClick(queueList.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu)[0], "Play now");
        pick(menu, "Go to album");
        tryCompare(win.libraryView, "content", LibraryView.Album);
        compare(win.libraryView.albumTitle, "Kid A");
    }

    function test_queue() {
        const win = openMain();
        const menu = win.contextMenu;
        const page = openAlbum(win, "The Wall");
        Playback.enqueueFromModel(page.tracksModel, false);
        tryCompare(Playback.queue, "count", 3);
        const titleAt = i => Playback.queue.get(i).title;
        const third = titleAt(2);
        win.toggleQueue();
        const queueList = findChild(win.queuePanel, "queueList");
        tryVerify(() => queueList.itemAtIndex(2) !== null);
        rightClick(queueList.itemAtIndex(2));
        tryCompare(menu, "opened", true);
        verify(!menuItem(menu, "Move down").enabled); // the last one
        const next = Playback.queue.currentIndex + 1; // (0 if nothing is current)
        pick(menu, "Move to play next");
        tryVerify(() => Playback.queue.get(next).title === third);
        // The empty space under the rows: the queue's own menu.
        mouseClick(queueList, queueList.width / 2, queueList.height - 10, Qt.RightButton);
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Save queue as playlist…", "Send to device", "Clear queue"]); // (no device in the tests)
        pick(menu, "Clear queue");
        tryCompare(Playback.queue, "count", 0);
    }

    Component {
        id: playlistTracksComp
        TrackListModel {}
    }

    function test_removeFromPlaylistAndRequestOrder() {
        // A file twice and one that's gone: removing the second copy leaves the first.
        const kid = fixtureDir + "/Radiohead/Kid A/01.flac";
        const id = Library.createPlaylist("Twice", [kid, fixtureDir + "/missing.flac", kid]);
        verify(id > 0);
        tc.playlistToDelete = id;
        const tracks = createTemporaryObject(playlistTracksComp, tc, { playlistId: id });
        tryCompare(tracks, "count", 2);
        verify(Library.removeFromPlaylist(id, kid, 1));
        tryCompare(tracks, "count", 1);
        verify(!Library.removeFromPlaylist(id, kid, 1)); // there's no second one now
        verify(Library.removeFromPlaylist(id, kid, 0));
        tryCompare(tracks, "count", 0);

        // Play, then Add: the add lands in the new queue, after it (in the order asked).
        const win = openMain();
        const wall = tile(win, "The Wall").albumId;
        const abbey = tile(win, "Abbey Road").albumId;
        Playback.playItem("album", wall);
        Playback.enqueueItem("album", abbey, false);
        tryCompare(Playback.queue, "count", 5);
        compare(Playback.queue.get(0).album, "The Wall");
        compare(Playback.queue.get(4).album, "Abbey Road");
        // A playlist with nothing in it doesn't wipe the queue.
        Playback.playItem("playlist", id);
        wait(200);
        compare(Playback.queue.count, 5);
    }

    function test_browseRows() {
        const win = openMain();
        const menu = win.contextMenu;
        const browse = win.libraryView.browse;
        const artists = findChild(browse, "artistList");
        tryVerify(() => artists.count > 0 && artists.itemAtIndex(0) !== null);
        const shownBefore = win.libraryView.filterLabel;
        rightClick(artists.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Play", "Play next", "Add to queue", "Add to playlist", "Send to device"]);
        compare(browse.contextTarget, 0); // outlined, not selected
        compare(win.libraryView.filterLabel, shownBefore);
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
        compare(browse.contextTarget, -1);
    }
}
