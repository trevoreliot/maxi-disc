// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// V1, the MD Visualizer (docs/plans/v1-visualizer-pile.md) on the fixture library: tile 3, the
// drill-down from the browse list into a pile of MiniDisc cases and back, each mode's pile, the
// cursor's case, the cases' colours, mouse and menus, and a 1,000-case pile. Most steps call
// Main.handleAction() (what KeyRouter triggers for h/j/k/l, Enter, Esc, `,`).
TestCase {
    id: tc
    name: "Visualizer"
    when: windowShown
    width: 400
    height: 500
    visible: true

    property int playlistId: 0 // deleted in cleanup(), even when a test fails half-way

    Component {
        id: mainComp
        Main {}
    }
    Component {
        id: pileComp
        MdPile {
            width: 320
            height: 400
        }
    }
    ListModel {
        id: bigModel
    }
    SignalSpy {
        id: menuSpy
        target: ContextMenus
        signalName: "requested"
    }

    function cleanup() {
        if (playlistId > 0)
            Library.deletePlaylist(playlistId);
        playlistId = 0;
    }

    function act(win, action, times) {
        for (let i = 0; i < (times === undefined ? 1 : times); ++i)
            win.handleAction(action);
    }

    function openVisualizer() {
        const win = createTemporaryObject(mainComp, tc);
        win.showScreen(Main.VisualizerScreen);
        const v = win.visualizerView;
        tryVerify(() => v.browse.currentList.count > 0, 5000, "artists loaded");
        return win;
    }

    function indexIn(model, role, value) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i)[role] === value)
                return i;
        }
        return -1;
    }

    // The browse list's row for `name` (artists / genres), from the "All" row (-1).
    function moveTo(win, name) {
        const v = win.visualizerView;
        const i = indexIn(v.browse.currentList.model, "name", name);
        verify(i >= 0, name + " listed");
        act(win, KeyRouter.MoveDown, i - v.browse.currentList.currentIndex);
        compare(v.browse.currentList.currentIndex, i);
        return i;
    }

    function titles(v) {
        const out = [];
        for (let i = 0; i < v.pile.count; ++i)
            out.push(v.pile.model.get(i).title);
        return out;
    }

    function caseAt(v, i) {
        return v.pile.view.itemAtIndex(i);
    }

    function caseTitled(v, title) {
        const i = indexIn(v.pile.model, "title", title);
        verify(i >= 0, title + " in the pile");
        return caseAt(v, i);
    }

    function test_menuTile() {
        const win = createTemporaryObject(mainComp, tc);
        // Alive but hidden from startup: no cases built, no colours asked for, until it's opened.
        wait(300);
        compare(win.visualizerView.pile.count, 0);
        act(win, KeyRouter.MoveRight, 2);
        compare(win.mainMenu.currentTile, 2);
        act(win, KeyRouter.Activate);
        compare(win.currentScreen, Main.VisualizerScreen);
        compare(win.chrome.title, "3  MD Visualizer");
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);

        const tile = findChild(win.mainMenu, "visualizerTile");
        verify(tile.interactive);
        mouseClick(tile);
        compare(win.currentScreen, Main.VisualizerScreen);
    }

    function test_drillDownByKeys() {
        const win = openVisualizer();
        const v = win.visualizerView;
        compare(v.level, VisualizerView.List);
        verify(v.browse.paneActive);
        verify(!v.pile.visible);

        // j/k only move the cursor: no pile opens until Enter / l.
        const artistRow = moveTo(win, "Radiohead");
        compare(v.level, VisualizerView.List);
        act(win, KeyRouter.MoveRight);
        compare(v.level, VisualizerView.Pile);
        compare(v.crumb, "Radiohead");
        verify(v.crumbRow.visible);
        verify(v.pile.paneActive);
        verify(!v.browse.paneActive);
        tryCompare(v.pile, "count", 2);
        compare(titles(v).slice().sort(), ["Kid A", "OK Computer"]);
        // One artist's pile: no artist printed on the spines.
        verify(!v.pile.showArtist);
        verify(!findChild(caseAt(v, 0), "caseDetail").visible);

        // The cursor's case slides out; j moves it down, and stops at the bottom.
        compare(v.pile.currentIndex, 0);
        tryCompare(caseAt(v, 0), "slide", 1);
        compare(caseAt(v, 1).slide, 0);
        verify(caseAt(v, 0).body.x > caseAt(v, 1).body.x + 10);
        act(win, KeyRouter.MoveDown, 2);
        compare(v.pile.currentIndex, 1);
        tryCompare(caseAt(v, 1), "slide", 1);
        tryCompare(caseAt(v, 0), "slide", 0);

        // Enter takes it out (V2: tst_mdcase.qml); Esc puts it back.
        act(win, KeyRouter.Activate);
        verify(v.chosen);
        compare(v.chosen.kind, "album");
        compare(v.chosen.title, titles(v)[1]);
        compare(v.chosen.artist, "Radiohead");
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        compare(findChild(v.caseArea.front, "frontTitle").text, titles(v)[1]);
        act(win, KeyRouter.Back);
        tryCompare(v.caseArea, "phase", MdCaseStage.Idle, 3000);
        compare(v.level, VisualizerView.Pile);

        // h back to the list, cursor still on Radiohead; in again: the same pile, cursor kept.
        act(win, KeyRouter.MoveLeft);
        compare(v.level, VisualizerView.List);
        compare(v.browse.currentList.currentIndex, artistRow);
        act(win, KeyRouter.Activate);
        compare(v.level, VisualizerView.Pile);
        compare(v.pile.currentIndex, 1);

        // Esc: the pile → the list → the main menu.
        act(win, KeyRouter.Back);
        compare(v.level, VisualizerView.List);
        compare(win.currentScreen, Main.VisualizerScreen);
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);
    }

    function test_allArtistsAndGenres() {
        const win = openVisualizer();
        const v = win.visualizerView;

        // "All artists" (the cursor starts there): every album, artists printed.
        compare(v.browse.currentList.currentIndex, -1);
        act(win, KeyRouter.Activate);
        compare(v.crumb, "All artists");
        tryCompare(v.pile, "count", 5);
        verify(v.pile.showArtist);
        const wall = caseTitled(v, "The Wall");
        verify(findChild(wall, "caseDetail").visible);
        compare(findChild(wall, "caseDetail").text, "Pink Floyd");

        // Another artist's pile starts with the cursor on top.
        act(win, KeyRouter.MoveDown, 3);
        act(win, KeyRouter.MoveLeft);
        moveTo(win, "Pink Floyd");
        act(win, KeyRouter.MoveRight);
        tryCompare(v.pile, "count", 1);
        compare(v.pile.currentIndex, 0);
        compare(titles(v), ["The Wall"]);

        // Genres: the dropdown goes back to the list level; Pop → Now 42, its artist printed.
        v.browse.setMode(BrowsePane.Genres);
        compare(v.level, VisualizerView.List);
        tryVerify(() => v.browse.currentList.count > 0);
        moveTo(win, "Pop");
        act(win, KeyRouter.MoveRight);
        compare(v.crumb, "Pop");
        tryVerify(() => titles(v).join() === "Now 42", 5000, "the Pop pile: " + titles(v)); // also 1 case
        verify(v.pile.showArtist);
    }

    function test_albumsAndMixDiscs() {
        const win = openVisualizer();
        const v = win.visualizerView;

        // Albums: the pile of every album straight away; there's no list to go back to.
        v.browse.setMode(BrowsePane.Albums);
        compare(v.level, VisualizerView.Pile);
        verify(!v.crumbRow.visible);
        tryCompare(v.pile, "count", 5);
        verify(v.pile.showArtist);
        act(win, KeyRouter.MoveLeft);
        compare(v.level, VisualizerView.Pile);
        act(win, KeyRouter.MoveDown, 3);
        compare(v.pile.currentIndex, 3);
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);
        win.showScreen(Main.VisualizerScreen);

        // "All artists" is the same albums, but another pile: the cursor starts on top.
        v.browse.setMode(BrowsePane.Artists);
        compare(v.level, VisualizerView.List);
        act(win, KeyRouter.Activate);
        compare(v.crumb, "All artists");
        tryCompare(v.pile, "currentIndex", 0);

        // Playlists: a mix disc per playlist (a blank-MD colour, the name on its label).
        playlistId = Library.createPlaylist("Road trip", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        verify(playlistId > 0);
        v.browse.setMode(BrowsePane.Playlists);
        compare(v.pile.kind, "playlist");
        tryVerify(() => indexIn(v.pile.model, "name", "Road trip") >= 0, 5000, "the playlist's mix disc");
        const i = indexIn(v.pile.model, "name", "Road trip");
        const disc = caseAt(v, i);
        verify(disc.mix);
        compare(findChild(disc, "caseTitle").text, "Road trip");
        compare(findChild(disc, "caseDetail").text, "1 track");
        verify(Qt.colorEqual(disc.caseColor, Theme.mdBlankColor(playlistId)));
        act(win, KeyRouter.MoveDown, i - v.pile.currentIndex);
        act(win, KeyRouter.Activate);
        compare(v.chosen.kind, "playlist");
        compare(v.chosen.id, playlistId);
        compare(v.chosen.title, "Road trip");
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        compare(findChild(v.caseArea.front, "frontMixTitle").text, "Road trip");
    }

    function test_coversColourTheCases() {
        const win = openVisualizer();
        const v = win.visualizerView;
        v.browse.setMode(BrowsePane.Albums);
        tryCompare(v.pile, "count", 5);
        const ok = caseTitled(v, "OK Computer"); // red folder art
        const kidA = caseTitled(v, "Kid A");     // blue embedded art
        tryVerify(() => ok.caseColor.r > ok.caseColor.b + 0.2, 5000, "OK Computer's case is red");
        tryVerify(() => kidA.caseColor.b > kidA.caseColor.r + 0.2, 5000, "Kid A's case is blue");
        // Light ink on these dark-ish cases.
        verify(Qt.colorEqual(findChild(ok, "caseTitle").color, SpineColors.prefersDarkText(ok.caseColor) ? Theme.spineInkDark : Theme.spineInkLight));
        // No art: the default case.
        wait(200);
        verify(Qt.colorEqual(caseTitled(v, "Abbey Road").caseColor, Theme.spineDefault));
    }

    function test_mouseAndMenus() {
        const win = openVisualizer();
        const v = win.visualizerView;
        menuSpy.clear();

        // A click on an artist opens its pile.
        const row = indexIn(v.browse.currentList.model, "name", "Radiohead");
        mouseClick(v.browse.currentList.itemAtIndex(row));
        compare(v.level, VisualizerView.Pile);
        compare(v.crumb, "Radiohead");
        tryCompare(v.pile, "count", 2);
        tryVerify(() => caseAt(v, 1) !== null);

        // A click moves the cursor; a click on the cursor's case chooses it.
        mouseClick(caseAt(v, 1));
        compare(v.pile.currentIndex, 1);
        verify(v.chosen === null);
        mouseClick(caseAt(v, 1));
        verify(v.chosen);
        compare(v.chosen.title, titles(v)[1]);
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);

        // Right-click: the album's menu, for that case (outlined while open).
        mouseClick(caseAt(v, 0), 20, 10, Qt.RightButton);
        compare(menuSpy.count, 1);
        compare(menuSpy.signalArguments[0][3][0].text, "Play");
        compare(v.pile.contextTarget, 0);
        win.contextMenu.close();
        tryCompare(v.pile, "contextTarget", -1);

        // `,`: the cursor's case.
        act(win, KeyRouter.ContextMenu);
        compare(menuSpy.count, 2);
        verify(menuSpy.signalArguments[1][3].some(e => e.text === "Find cover…"));
        win.contextMenu.close();
        tryCompare(win.contextMenu, "opened", false);

        // The crumb goes back to the list.
        mouseClick(v.crumbRow);
        compare(v.level, VisualizerView.List);
    }

    // Holding j through a long pile: the cursor's case stays in view, and only the cases near it
    // exist (the rest are created as they scroll in).
    function test_thousandCases() {
        bigModel.clear();
        for (let i = 0; i < 1000; ++i)
            bigModel.append({ albumId: i + 1, title: "Album " + i, artist: "Artist " + (i % 37), year: 2000, coverUrl: "" });
        const p = createTemporaryObject(pileComp, tc, { model: bigModel, paneActive: true });
        tryCompare(p, "count", 1000);
        compare(p.currentIndex, 0);
        const inView = i => {
            const d = p.view.itemAtIndex(i);
            return d !== null && d.y - p.view.contentY >= 0 && d.y + d.height - p.view.contentY <= p.view.height;
        };
        for (let i = 0; i < 999; ++i) {
            p.moveSelection(1);
            if (i % 97 === 0)
                verify(inView(p.currentIndex), "case " + p.currentIndex + " in view");
        }
        compare(p.currentIndex, 999);
        verify(inView(999));
        verify(p.view.contentItem.children.length < 80, "delegates: " + p.view.contentItem.children.length);
        for (let i = 0; i < 999; ++i)
            p.moveSelection(-1);
        compare(p.currentIndex, 0);
        verify(inView(0));
        verify(p.view.contentItem.children.length < 80, "delegates: " + p.view.contentItem.children.length);
        compare(p.currentItem().title, "Album 0");
    }

    // MMM_SCREENSHOT_DIR: the menu's new tile, the pile at the default and the smallest size (queue
    // open), and the mix discs.
    function test_screenshots() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        const win = createTemporaryObject(mainComp, tc);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-menu.png");

        win.showScreen(Main.VisualizerScreen);
        const v = win.visualizerView;
        tryVerify(() => v.browse.currentList.count > 0);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-list.png");
        act(win, KeyRouter.Activate); // All artists
        tryCompare(v.pile, "count", 5);
        act(win, KeyRouter.MoveDown, 2);
        tryVerify(() => caseTitled(v, "OK Computer").caseColor.r > 0.4);
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-pile.png");

        win.width = win.minimumWidth;
        win.height = win.minimumHeight;
        win.queueOpen = true;
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-pile-small.png");
        win.queueOpen = false;

        playlistId = Library.createPlaylist("Road trip", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        v.browse.setMode(BrowsePane.Playlists);
        tryVerify(() => v.pile.count > 0);
        act(win, KeyRouter.Activate);
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-mixdiscs.png");
    }
}
