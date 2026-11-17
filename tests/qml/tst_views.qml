// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Library views against the fixture library built in tst_qml.cpp.
// Set MMM_SCREENSHOT_DIR to save a PNG of each view.
TestCase {
    id: tc
    name: "Views"
    when: windowShown
    width: 1000
    height: 700
    visible: true // TestCase is hidden by default; hidden items get no mouse events

    Component {
        id: gridComp
        AlbumGrid {
            width: 900
            height: 600
        }
    }
    Component {
        id: pageComp
        AlbumPage {
            width: 900
            height: 600
        }
    }
    Component {
        id: searchComp
        SearchResults {
            width: 900
            height: 600
        }
    }
    Component {
        id: sidebarComp
        Sidebar {
            width: 260
            height: 600
        }
    }
    Component {
        id: mainComp
        Main {}
    }
    SignalSpy {
        id: spy
    }

    function shot(item, name) {
        if (screenshotDir === "")
            return;
        wait(300); // let async covers land
        grabImage(item).save(screenshotDir + "/" + name + ".png");
    }

    // Album id by title, via a grid's delegates.
    function albumIdOf(grid, title) {
        for (let i = 0; i < grid.count; ++i) {
            const tile = grid.itemAtIndex(i);
            if (tile && tile.title === title)
                return tile.albumId;
        }
        return 0;
    }

    function test_albumGrid() {
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        tryCompare(grid, "loading", false);
        verify(albumIdOf(grid, "OK Computer") > 0);

        spy.target = grid;
        spy.signalName = "albumActivated";
        spy.clear();
        mouseClick(grid.itemAtIndex(0));
        compare(spy.count, 1);
        compare(spy.signalArguments[0][1], "Abbey Road"); // "The Beatles" sorts first (under B)
        shot(grid, "album-grid");
    }

    function test_sidebarFiltersGrid() {
        const sidebar = createTemporaryObject(sidebarComp, tc);
        const artists = findChild(sidebar, "artistList");
        verify(artists !== null);
        tryCompare(artists, "count", 4);

        let radiohead = null;
        for (let i = 0; i < artists.count; ++i) {
            const row = artists.itemAtIndex(i);
            if (row && row.name === "Radiohead")
                radiohead = row;
        }
        verify(radiohead !== null);

        spy.target = sidebar;
        spy.signalName = "artistSelected";
        spy.clear();
        mouseClick(radiohead);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][1], "Radiohead");

        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        grid.artistId = spy.signalArguments[0][0];
        tryCompare(grid, "count", 2);
        tryCompare(grid, "loading", false);
        compare(grid.itemAtIndex(0).title, "OK Computer"); // chronological within an artist
        compare(grid.itemAtIndex(1).title, "Kid A");
        grid.artistId = 0;
        tryCompare(grid, "count", 5);
    }

    function coverOf(grid, title) {
        for (let i = 0; i < grid.count; ++i) {
            const tile = grid.itemAtIndex(i);
            if (tile && tile.title === title)
                return tile.coverUrl;
        }
        return "";
    }

    function test_albumPage() {
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        const page = createTemporaryObject(pageComp, tc, {
            albumId: albumIdOf(grid, "The Wall"),
            albumTitle: "The Wall",
            albumArtist: "Pink Floyd",
            year: 1979
        });
        tryCompare(page, "trackCount", 3);
        verify(page.multiDisc);
        shot(page, "album-page-multidisc");

        const ok = createTemporaryObject(pageComp, tc, {
            albumId: albumIdOf(grid, "OK Computer"),
            albumTitle: "OK Computer",
            albumArtist: "Radiohead",
            year: 1997,
            coverUrl: coverOf(grid, "OK Computer")
        });
        tryCompare(ok, "trackCount", 3);
        verify(!ok.multiDisc);
        shot(ok, "album-page");
    }

    function test_search() {
        const results = createTemporaryObject(searchComp, tc);
        results.searchText = "radiohead";
        tryCompare(results, "count", 5);
        results.searchText = "paranoid";
        tryCompare(results, "count", 1);
        results.searchText = "radio";
        tryCompare(results, "count", 5); // prefix match
        shot(results, "search");
    }

    function test_mainWindow() {
        const win = createTemporaryObject(mainComp, tc);
        verify(win !== null);
        verify(win.hasLibrary);
        tryCompare(Library, "trackCount", 12);
        shot(win.contentItem, "main-grid");

        const search = findChild(win, "searchField"); // lives in the header, not contentItem
        verify(search !== null);
        search.text = "abbey";
        verify(win.searching);
        wait(400);
        shot(win.contentItem, "main-search");
        search.text = "";
        verify(!win.searching);

        win.openAlbum(1, "Some Album", "Someone", 2001, "");
        compare(win.albumId, 1);
        win.closeAlbum();
        compare(win.albumId, 0);
    }
}
