// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// V2, taking a case from the pile (docs/plans/v2-visualizer-case.md) on the fixture library: the
// case out (turned to its front, the cartridge part-way out), flipped to its track list and back,
// put back into its gap; pressing again during an animation; the panes and swapping cases; Enter
// on the back plays; multi-disc, no-art and mix-disc cases; the case's menu; a pile left meanwhile;
// a hidden window. Most steps call Main.handleAction() (what KeyRouter triggers).
TestCase {
    id: tc
    name: "MdCase"
    when: windowShown
    width: 400
    height: 400
    visible: true

    property int playlistId: 0 // deleted in cleanup(), even when a test fails half-way

    Component {
        id: mainComp
        Main {}
    }
    SignalSpy {
        id: menuSpy
        target: ContextMenus
        signalName: "requested"
    }

    function cleanup() {
        Playback.windowVisible = true;
        Playback.stop();
        Playback.clearQueue();
        if (playlistId > 0)
            Library.deletePlaylist(playlistId);
        playlistId = 0;
    }

    function act(win, action, times) {
        for (let i = 0; i < (times === undefined ? 1 : times); ++i)
            win.handleAction(action);
    }

    function indexIn(model, role, value) {
        for (let i = 0; i < model.count; ++i) {
            if (model.get(i)[role] === value)
                return i;
        }
        return -1;
    }

    // The Visualizer in Albums mode (every album in the pile).
    function openAlbums() {
        const win = createTemporaryObject(mainComp, tc);
        win.showScreen(Main.VisualizerScreen);
        const v = win.visualizerView;
        v.browse.setMode(BrowsePane.Albums);
        tryCompare(v.pile, "count", 5);
        return win;
    }

    // The pile's cursor onto `title`; returns its index.
    function cursorTo(win, title) {
        const v = win.visualizerView;
        const i = indexIn(v.pile.model, "title", title);
        verify(i >= 0, title + " in the pile");
        act(win, KeyRouter.MoveDown, i - v.pile.currentIndex);
        act(win, KeyRouter.MoveUp, v.pile.currentIndex - i);
        compare(v.pile.currentIndex, i);
        return i;
    }

    function takeOutSettled(win, title) {
        const v = win.visualizerView;
        cursorTo(win, title);
        act(win, KeyRouter.Activate);
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        return v.caseArea;
    }

    function test_lifeCycle() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = v.caseArea;
        const i = cursorTo(win, "OK Computer");
        compare(stage.phase, MdCaseStage.Idle);
        verify(findChild(stage, "casePlaceholder").visible);

        // Out of the pile: a gap stays, the keyboard goes to the case.
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.TakingOut);
        verify(v.caseFocused);
        verify(stage.paneActive);
        verify(!v.pile.paneActive);
        compare(stage.takenKey, "album:" + v.pile.model.get(i).albumId);
        verify(v.pile.view.itemAtIndex(i).taken);
        verify(!findChild(stage, "casePlaceholder").visible);
        tryCompare(stage, "phase", MdCaseStage.CartridgeOut, 2000);
        tryCompare(stage, "phase", MdCaseStage.Front, 2000);
        compare(stage.take, 1);
        compare(stage.cartridge, 1);
        verify(stage.cartridgeItem.visible);
        compare(stage.cartridgeItem.y, 0); // its top edge `peek` above the case
        compare(findChild(stage.front, "frontTitle").text, "OK Computer");
        verify(stage.front.visible && !stage.back.visible);
        tryCompare(stage.trackModel, "count", 3);

        // Flip: the track list, the cursor on its first row, j/k within it.
        act(win, KeyRouter.FlipCase);
        compare(stage.phase, MdCaseStage.FlippingToBack);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        verify(stage.back.visible && !stage.front.visible);
        verify(stage.cartridgeItem.showingBack);
        tryCompare(stage.back.tracksView, "count", 3);
        const row0 = stage.back.tracksView.itemAtIndex(0);
        compare(findChild(row0, "trackTitle").text, stage.trackModel.get(0).title);
        compare(findChild(row0, "trackDuration").text, stage.trackModel.get(0).duration);
        compare(stage.trackIndex, 0);
        act(win, KeyRouter.MoveDown, 5);
        compare(stage.trackIndex, 2);
        act(win, KeyRouter.MoveUp, 5);
        compare(stage.trackIndex, 0);

        // Back to the front, then back into the pile.
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Front, 2000);
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.PuttingBack);
        verify(!v.caseFocused);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        compare(stage.takenKey, "");
        verify(!v.pile.view.itemAtIndex(i).taken);
        compare(stage.trackModel.albumId, 0);
        compare(v.level, VisualizerView.Pile);
        compare(win.currentScreen, Main.VisualizerScreen);
        compare(v.pile.currentIndex, i);
    }

    // Pressing again during an animation: the same key finishes it (and that's all); another one
    // finishes it and goes ahead.
    function test_finishAtOnce() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = v.caseArea;
        cursorTo(win, "OK Computer");

        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.TakingOut);
        act(win, KeyRouter.Activate); // again: lands at once, doesn't flip
        compare(stage.phase, MdCaseStage.Front);
        compare(stage.take, 1);
        compare(stage.cartridge, 1);
        tryCompare(stage.trackModel, "count", 3);

        act(win, KeyRouter.FlipCase);
        compare(stage.phase, MdCaseStage.FlippingToBack);
        act(win, KeyRouter.FlipCase); // again: turned at once
        compare(stage.phase, MdCaseStage.Back);
        compare(stage.flipProgress, 1);

        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.PuttingBack);
        act(win, KeyRouter.Back); // again: back at once, and still in the pile
        compare(stage.phase, MdCaseStage.Idle);
        compare(v.level, VisualizerView.Pile);
        compare(win.currentScreen, Main.VisualizerScreen);

        // Another key: `f` while it comes out lands it, then flips; j mid-flip turns it, then moves.
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.TakingOut);
        act(win, KeyRouter.FlipCase);
        compare(stage.phase, MdCaseStage.FlippingToBack);
        tryCompare(stage.trackModel, "count", 3);
        act(win, KeyRouter.MoveDown);
        compare(stage.phase, MdCaseStage.Back);
        compare(stage.trackIndex, 1);
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
    }

    function test_panesAndSwap() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = takeOutSettled(win, "OK Computer");
        const ok = indexIn(v.pile.model, "title", "OK Computer");

        // h to the pile (the case stays out), l back to the case.
        act(win, KeyRouter.MoveLeft);
        verify(!v.caseFocused);
        verify(v.pile.paneActive);
        compare(stage.phase, MdCaseStage.Front);
        act(win, KeyRouter.MoveRight);
        verify(v.caseFocused);

        // Another case: this one goes back, then that one comes out.
        act(win, KeyRouter.MoveLeft);
        const kidA = cursorTo(win, "Kid A");
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.PuttingBack);
        tryVerify(() => stage.phase === MdCaseStage.Front && stage.item.title === "Kid A", 5000, "Kid A is out");
        verify(!v.pile.view.itemAtIndex(ok).taken);
        verify(v.pile.view.itemAtIndex(kidA).taken);
        verify(v.caseFocused);

        // Enter on the case that's out: just the keyboard back to it.
        act(win, KeyRouter.MoveLeft);
        act(win, KeyRouter.Activate);
        verify(v.caseFocused);
        compare(stage.phase, MdCaseStage.Front);
        compare(stage.item.title, "Kid A");
    }

    // Neon's review: swaps and clicks while a case is on its way back.
    function test_swapEdgeCases() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = takeOutSettled(win, "OK Computer");
        const ok = indexIn(v.pile.model, "title", "OK Computer");

        // Esc during a swap cancels it: OK Computer goes back, Kid A stays in the pile.
        act(win, KeyRouter.MoveLeft);
        cursorTo(win, "Kid A");
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.PuttingBack);
        verify(v.pending !== null);
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.Idle);
        verify(v.pending === null);
        verify(!v.caseFocused);
        wait(100);
        compare(stage.phase, MdCaseStage.Idle);

        // A key during a swap's put-back: the swap lands and starts the next case; the key is used up.
        cursorTo(win, "OK Computer");
        act(win, KeyRouter.Activate);
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);
        act(win, KeyRouter.MoveLeft);
        const kidA = cursorTo(win, "Kid A");
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.PuttingBack);
        act(win, KeyRouter.MoveDown);
        compare(stage.phase, MdCaseStage.TakingOut);
        compare(stage.item.title, "Kid A");
        compare(v.pile.currentIndex, kidA);
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);

        // Enter meant for the pile while a case comes out isn't swallowed by it.
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        act(win, KeyRouter.Activate); // Kid A out again
        compare(stage.phase, MdCaseStage.TakingOut);
        mouseClick(v.pile.view.itemAtIndex(ok)); // the pile takes the keyboard, cursor on OK Computer
        verify(!v.caseFocused);
        compare(v.pile.currentIndex, ok);
        act(win, KeyRouter.Activate); // finishes Kid A, then swaps for OK Computer
        compare(stage.phase, MdCaseStage.PuttingBack);
        compare(v.pending.item.title, "OK Computer");
        tryVerify(() => stage.phase === MdCaseStage.Front && stage.item.title === "OK Computer", 5000);

        // A click on the gap of a case going back takes it out again.
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.PuttingBack);
        mouseClick(v.pile.view.itemAtIndex(ok));
        compare(stage.phase, MdCaseStage.TakingOut);
        compare(stage.item.title, "OK Computer");
        verify(v.caseFocused);
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);

        // A case going back takes no clicks and opens no menu.
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.PuttingBack);
        menuSpy.clear();
        mouseClick(stage.caseItem, 20, 20, Qt.RightButton);
        compare(menuSpy.count, 0);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        verify(!v.caseFocused);
        verify(v.pile.paneActive);
    }

    // Enter on the front flips it; on the back it plays from the cursor's track.
    function test_enterFlipsThenPlays() {
        const win = openAlbums();
        const stage = takeOutSettled(win, "Kid A");
        const played = signalSpyComp.createObject(tc, { target: stage, signalName: "playTrackRequested" });
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.FlippingToBack);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        tryCompare(stage.trackModel, "count", 2);
        act(win, KeyRouter.Activate);
        compare(played.count, 1);
        compare(played.signalArguments[0][1], 0);
        tryCompare(Playback, "playing", true, 5000);
        verify(Playback.currentPath.endsWith("Kid A/01.flac"), Playback.currentPath);
        played.destroy();
    }
    Component {
        id: signalSpyComp
        SignalSpy {}
    }

    function test_multiDiscNoArtAndMixDisc() {
        const win = openAlbums();
        const v = win.visualizerView;

        // The Wall: two discs, two sections.
        let stage = takeOutSettled(win, "The Wall");
        tryCompare(stage.trackModel, "count", 3);
        compare(stage.trackModel.discCount, 2);
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        compare(stage.back.tracksView.section.property, "discNumber");
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);

        // Abbey Road: no art, the case colour behind the caption.
        stage = takeOutSettled(win, "Abbey Road");
        verify(!findChild(stage.front, "frontCover").visible);
        compare(findChild(stage.front, "frontTitle").text, "Abbey Road");
        verify(Qt.colorEqual(stage.item.color, Theme.spineDefault));
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);

        // A mix disc: paper on the front, the cartridge and the back.
        playlistId = Library.createPlaylist("Mix test", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        v.browse.setMode(BrowsePane.Playlists);
        tryVerify(() => indexIn(v.pile.model, "name", "Mix test") >= 0, 5000);
        const i = indexIn(v.pile.model, "name", "Mix test");
        act(win, KeyRouter.MoveDown, i - v.pile.currentIndex);
        act(win, KeyRouter.Activate);
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);
        verify(stage.mix);
        compare(findChild(stage.front, "frontMixTitle").text, "Mix test");
        verify(stage.cartridgeItem.mix);
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        tryCompare(stage.back.tracksView, "count", 1);
        verify(findChild(stage.back.tracksView.itemAtIndex(0), "trackTitle").text.endsWith("– Radiohead"));

        // Its menu is the playlist's.
        menuSpy.clear();
        act(win, KeyRouter.ContextMenu);
        compare(menuSpy.count, 1);
        verify(menuSpy.signalArguments[0][3].some(e => e.text === "Rename…"));
        win.contextMenu.close();
        tryCompare(win.contextMenu, "opened", false);
    }

    function test_rightClick() {
        const win = openAlbums();
        const stage = takeOutSettled(win, "OK Computer");
        menuSpy.clear();
        mouseClick(stage.caseItem, 30, 30, Qt.RightButton);
        compare(menuSpy.count, 1);
        const entries = menuSpy.signalArguments[0][3];
        compare(entries[0].text, "Play");
        verify(entries.some(e => e.text === "Find cover…"));
        win.contextMenu.close();
        tryCompare(win.contextMenu, "opened", false);

        // A left click on the front flips it.
        mouseClick(stage.caseItem, 30, 30);
        compare(stage.phase, MdCaseStage.FlippingToBack);
        // The buttons: Flip turns it (finishing the flip first), Put back puts it back.
        mouseClick(findChild(stage, "flipButton"));
        compare(stage.phase, MdCaseStage.Back);
        mouseClick(findChild(stage, "putBackButton"));
        compare(stage.phase, MdCaseStage.PuttingBack);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
    }

    // The pile it came from isn't showing any more: Esc still puts it back (fading at the edge).
    function test_pileChangedWhileOut() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = takeOutSettled(win, "OK Computer");
        v.browse.setMode(BrowsePane.Artists);
        compare(v.level, VisualizerView.List);
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.PuttingBack);
        verify(stage.fadeOut);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        compare(win.currentScreen, Main.VisualizerScreen);
        act(win, KeyRouter.Back);
        compare(win.currentScreen, Main.MenuScreen);
    }

    // Window hidden: nothing animates; every step lands at once.
    function test_hiddenWindow() {
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = v.caseArea;
        cursorTo(win, "OK Computer");
        Playback.windowVisible = false;
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.Front);
        act(win, KeyRouter.FlipCase);
        compare(stage.phase, MdCaseStage.Back);
        act(win, KeyRouter.Back);
        compare(stage.phase, MdCaseStage.Idle);

        // Hidden mid-flight: it lands.
        Playback.windowVisible = true;
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.TakingOut);
        Playback.windowVisible = false;
        compare(stage.phase, MdCaseStage.Front);
    }

    // MMM_SCREENSHOT_DIR: the front, the back, a mix disc's back; the default and smallest sizes.
    function test_screenshots() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        const win = openAlbums();
        const v = win.visualizerView;
        const stage = takeOutSettled(win, "OK Computer");
        tryVerify(() => findChild(stage.front, "frontCover").ready, 5000);
        tryVerify(() => stage.cartridgeItem.disc.showingCover, 5000);
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-front.png");
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        tryCompare(stage.back.tracksView, "count", 3);
        act(win, KeyRouter.MoveDown);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-back.png");

        win.width = win.minimumWidth;
        win.height = win.minimumHeight;
        wait(300);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-back-small.png");
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Front, 2000);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-front-small.png");
        win.width = 1280;
        win.height = 800;

        // Mid-turn and mid-flip: the flat 2D box as the software renderer draws it.
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        act(win, KeyRouter.Activate);
        tryVerify(() => stage.take > 0.5, 2000);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-turning.png");
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);
        act(win, KeyRouter.FlipCase);
        tryVerify(() => stage.flipProgress > 0.25, 2000);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-flipping.png");
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);

        playlistId = Library.createPlaylist("Road trip", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 2000);
        v.browse.setMode(BrowsePane.Playlists);
        tryVerify(() => v.pile.count > 0, 5000);
        act(win, KeyRouter.Activate);
        tryCompare(stage, "phase", MdCaseStage.Front, 3000);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-mix-front.png");
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-case-mix-back.png");
    }
}
