// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Keyboard navigation through the real app shell on the fixture library: the M5b user
// test path (menu → browse → album → play → queue) plus Esc and search. Most steps call
// Main.handleAction() (what KeyRouter triggers); test_realKeys sends real key presses.
TestCase {
    id: tc
    name: "Navigation"
    when: windowShown

    Component {
        id: mainComp
        Main {}
    }

    function cleanup() {
        Playback.stop(); // clearing keeps a playing track
        Playback.clearQueue();
    }

    function act(win, action, times) {
        for (let i = 0; i < (times === undefined ? 1 : times); ++i)
            win.handleAction(action);
    }

    function indexOfArtist(win, name) {
        const list = findChild(win.libraryView.browse, "artistList");
        for (let i = 0; i < list.count; ++i) {
            if (win.libraryView.browse.currentList.model.get(i).name === name)
                return i;
        }
        return -1;
    }

    function test_menuTiles() {
        const win = createTemporaryObject(mainComp, tc);
        compare(win.currentScreen, Main.MenuScreen);
        compare(win.mainMenu.currentTile, 0);
        act(win, KeyRouter.MoveRight);
        compare(win.mainMenu.currentTile, 1);
        act(win, KeyRouter.MoveRight); // tile 3 (MD Visualizer) is skipped
        compare(win.mainMenu.currentTile, 1);
        act(win, KeyRouter.MoveLeft, 3);
        compare(win.mainMenu.currentTile, 0);
        act(win, KeyRouter.Back); // Esc on the menu does nothing (never quits)
        compare(win.currentScreen, Main.MenuScreen);

        act(win, KeyRouter.MoveRight);
        act(win, KeyRouter.Activate);
        compare(win.currentScreen, Main.ExportScreen);
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);
    }

    function test_userTestPath() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = win.libraryView;
        tryCompare(Library, "trackCount", 12);

        // Menu → Music Library; focus starts in the browse pane.
        act(win, KeyRouter.Activate);
        compare(win.currentScreen, Main.LibraryScreen);
        compare(lib.focusedPane, LibraryView.Browse);
        verify(lib.browse.paneActive);
        tryCompare(lib.browse.currentList, "count", 4);

        // j down to Radiohead, l into the grid (selects right away).
        const radiohead = indexOfArtist(win, "Radiohead");
        verify(radiohead >= 0);
        act(win, KeyRouter.MoveDown, radiohead + 1); // from "All artists" at -1
        act(win, KeyRouter.MoveRight);
        compare(lib.focusedPane, LibraryView.Content);
        compare(lib.filterLabel, "Radiohead");
        tryCompare(lib.grid, "count", 2);
        tryCompare(lib.grid, "loading", false);
        verify(lib.grid.paneActive);

        // Grid: l to Kid A, h back, h at the left edge returns to the browse pane.
        lib.grid.currentIndex = 0;
        act(win, KeyRouter.MoveRight);
        compare(lib.grid.currentIndex, 1);
        act(win, KeyRouter.MoveRight); // last album: stays (queue closed)
        compare(lib.grid.currentIndex, 1);
        compare(lib.focusedPane, LibraryView.Content);
        act(win, KeyRouter.MoveLeft, 2);
        compare(lib.focusedPane, LibraryView.Browse);
        act(win, KeyRouter.MoveRight);
        compare(lib.focusedPane, LibraryView.Content);
        act(win, KeyRouter.MoveRight);
        compare(lib.grid.currentIndex, 1);

        // Enter opens Kid A; Enter again plays from the cursor (track 1).
        act(win, KeyRouter.Activate);
        compare(lib.content, LibraryView.Album);
        compare(lib.albumTitle, "Kid A");
        tryCompare(lib.albumPage, "trackCount", 2);
        compare(lib.albumPage.currentIndex, 0);
        act(win, KeyRouter.MoveDown);
        compare(lib.albumPage.currentIndex, 1);
        act(win, KeyRouter.MoveUp, 3);
        compare(lib.albumPage.currentIndex, 0);
        act(win, KeyRouter.Activate);
        compare(Playback.queue.count, 2);
        compare(Playback.title, "Everything in Its Right Place");
        // Another copy of the (only real) audio file at the end, to jump to from the queue.
        Playback.enqueueFromModel(lib.albumPage.tracksModel, false, 0);
        compare(Playback.queue.count, 3);

        // q opens and focuses the queue, cursor on the playing track; j, Enter jumps.
        act(win, KeyRouter.ToggleQueue);
        verify(win.queueOpen && win.queueFocused);
        verify(win.queuePanel.paneActive);
        verify(!lib.active);
        compare(win.queuePanel.currentIndex, 0);
        act(win, KeyRouter.MoveDown, 2);
        compare(win.queuePanel.currentIndex, 2);
        act(win, KeyRouter.Activate);
        tryCompare(Playback.queue, "currentIndex", 2);

        // h back to the album, l from the album into the queue again.
        act(win, KeyRouter.MoveLeft);
        verify(!win.queueFocused && lib.active);
        act(win, KeyRouter.MoveRight);
        verify(win.queueFocused);

        // Esc: closes the queue, then album → grid (cursor kept), then → menu.
        act(win, KeyRouter.Back);
        verify(!win.queueOpen && !win.queueFocused);
        act(win, KeyRouter.Back);
        compare(lib.content, LibraryView.Grid);
        compare(lib.grid.currentIndex, 1);
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);
        // Browse state survives the trip back to the menu.
        act(win, KeyRouter.Activate);
        compare(lib.filterLabel, "Radiohead");
        compare(lib.grid.count, 2);
    }

    function test_albumsModeOpensAlbumDirectly() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = win.libraryView;
        win.showScreen(Main.LibraryScreen);
        lib.browse.setMode(BrowsePane.Albums);
        compare(lib.content, LibraryView.Hint);
        tryCompare(lib.browse.currentList, "count", 5);
        act(win, KeyRouter.MoveDown);
        act(win, KeyRouter.Activate);
        compare(lib.content, LibraryView.Album);
        compare(lib.focusedPane, LibraryView.Content);
        verify(!lib.albumFromGrid);
        act(win, KeyRouter.Back); // not from the grid: straight back to the menu
        compare(win.currentScreen, Main.MenuScreen);
    }

    function test_search() {
        const win = createTemporaryObject(mainComp, tc);
        const lib = win.libraryView;
        const search = findChild(win, "searchField");
        act(win, KeyRouter.FocusSearch);
        verify(search.activeFocus);
        verify(!win.mainMenu.active); // no pane shows focus while typing
        search.text = "radio"; // typing on the menu switches to the library
        compare(win.currentScreen, Main.LibraryScreen);
        tryCompare(lib.searchResults, "count", 5);
        compare(lib.content, LibraryView.Search);

        act(win, KeyRouter.LeaveTextInput); // Esc in the field: leave it, keep the results
        verify(!search.activeFocus);
        compare(lib.focusedPane, LibraryView.Content);
        verify(lib.searchResults.paneActive);
        act(win, KeyRouter.MoveDown);
        compare(lib.searchResults.currentIndex, 1);
        // Clicking a result while typing takes the keyboard out of the search field.
        act(win, KeyRouter.FocusSearch);
        verify(search.activeFocus);
        mouseClick(findChild(lib.searchResults, "resultList").itemAtIndex(2));
        verify(!search.activeFocus);
        compare(lib.searchResults.currentIndex, 2);
        verify(lib.searchResults.paneActive);
        act(win, KeyRouter.Back); // Esc: clear the search
        compare(search.text, "");
        tryCompare(lib, "content", LibraryView.Grid);
        compare(win.currentScreen, Main.LibraryScreen);
    }

    // Artwork finder (M7): A opens it for every album without art, a for the current album.
    // Keys go to the dialog while it's open (real key presses: the popup opts in to routing).
    // ctest points the finder at a closed local port, so searches fail fast and offline.
    function test_artworkFinder() {
        const win = createTemporaryObject(mainComp, tc);
        win.requestActivate();
        tryVerify(() => win.active, 5000, "app window never became active");
        const dlg = win.artworkFinderDialog;
        tryCompare(Library, "missingArtCount", 3); // Abbey Road, The Wall, Now 42

        keyClick("A");
        tryCompare(dlg, "opened", true);
        verify(dlg.batch);
        tryCompare(dlg.artFinder, "count", 3);
        compare(dlg.artFinder.currentIndex, 0);
        keyClick("j");
        compare(dlg.artFinder.currentIndex, 1);
        keyClick("k");
        compare(dlg.artFinder.currentIndex, 0);
        keyClick("q"); // not the queue: the dialog has the keys
        verify(!win.queueOpen);
        tryVerify(() => dlg.artFinder.current.status === ArtFinder.Failed, 10000, "search should fail offline");
        if (screenshotDir !== "") {
            wait(100);
            const shot = grabImage(win.contentItem.parent);
            shot.save(screenshotDir + "/artwork-finder-app.png");
        }
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
        compare(dlg.artFinder.count, 0);
        compare(win.currentScreen, Main.MenuScreen); // Esc closed the dialog only
        keyClick("q"); // the window has the keys again
        verify(win.queueOpen);
        keyClick(Qt.Key_Escape);

        // `a` on an album page: that album only.
        win.showScreen(Main.LibraryScreen);
        const lib = win.libraryView;
        tryCompare(lib, "content", LibraryView.Grid);
        tryVerify(() => lib.grid.count > 0);
        lib.grid.setCurrent(0); // Abbey Road (The Beatles sort under B)
        lib.focusedPane = LibraryView.Content;
        const albumId = lib.grid.currentAlbumId();
        verify(albumId > 0);
        keyClick("a");
        tryCompare(dlg, "opened", true);
        verify(!dlg.batch);
        tryCompare(dlg.artFinder, "count", 1);
        compare(dlg.artFinder.current.albumId, albumId);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
        compare(lib.content, LibraryView.Grid);
    }

    // The same journey with real key presses, so KeyRouter → Main wiring is covered.
    function test_realKeys() {
        const win = createTemporaryObject(mainComp, tc);
        win.requestActivate();
        tryVerify(() => win.active, 5000, "app window never became active");
        const search = findChild(win, "searchField");

        keyClick(Qt.Key_Return);
        compare(win.currentScreen, Main.LibraryScreen);
        keyClick("q");
        verify(win.queueOpen);
        keyClick(Qt.Key_Escape);
        verify(!win.queueOpen);

        keyClick("/");
        verify(search.activeFocus);
        keyClick("j"); // typed, not "move down"
        keyClick("k");
        compare(search.text, "jk");
        keyClick(Qt.Key_Escape); // leave the field
        verify(!search.activeFocus);
        keyClick(Qt.Key_Escape); // clear the search
        compare(search.text, "");
        keyClick(Qt.Key_Escape); // back to the menu
        compare(win.currentScreen, Main.MenuScreen);
    }
}
