// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// V3, the MiniDisc player (docs/plans/v3-visualizer-player.md) on the fixture library (only
// Kid A/01.flac plays; fakesink): insert from the case's front and back, the player's keys, the
// pile locked until an Eject (the OPEN slide, `O`), playback started elsewhere, a mix disc's
// identity, the side LCD, a hidden window, the layouts. Most steps call Main.handleAction().
TestCase {
    id: tc
    name: "MdPlayer"
    when: windowShown
    width: 400
    height: 400
    visible: true

    property int playlistId: 0 // deleted in cleanup(), even when a test fails half-way

    Component {
        id: mainComp
        Main {}
    }
    Component {
        id: lcdComp
        MdSideLcd {
            width: 40
            height: 200
        }
    }

    function cleanup() {
        Playback.windowVisible = true;
        Playback.analyzerEnabled = false;
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

    function openAlbums() {
        const win = createTemporaryObject(mainComp, tc);
        win.showScreen(Main.VisualizerScreen);
        const v = win.visualizerView;
        v.browse.setMode(BrowsePane.Albums);
        tryCompare(v.pile, "count", 5);
        return win;
    }

    function cursorTo(win, title) {
        const v = win.visualizerView;
        const i = indexIn(v.pile.model, "title", title);
        verify(i >= 0, title + " in the pile");
        act(win, KeyRouter.MoveDown, i - v.pile.currentIndex);
        act(win, KeyRouter.MoveUp, v.pile.currentIndex - i);
        return i;
    }

    function takeOutSettled(win, title) {
        const v = win.visualizerView;
        cursorTo(win, title);
        act(win, KeyRouter.Activate);
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        tryVerify(() => v.caseArea.trackModel.count > 0, 3000);
        return v.caseArea;
    }

    // Kid A out and in the player, playing.
    function loadKidA(win) {
        const v = win.visualizerView;
        takeOutSettled(win, "Kid A");
        act(win, KeyRouter.PlayCase);
        tryCompare(v.deckArea, "phase", MdDeck.Loaded, 4000);
        tryCompare(Playback, "playing", true, 5000);
        return v.deckArea;
    }

    function test_emptyAtStart() {
        const win = openAlbums();
        const deck = win.visualizerView.deckArea;
        compare(deck.phase, MdDeck.Empty);
        verify(!deck.animating);
        compare(deck.player.lidOpen, 0);
        verify(!deck.flyingCartridge.visible);
        verify(!deck.player.loadedCartridge.visible);
    }

    function test_insertFromTheFront() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = v.deckArea;
        const stage = takeOutSettled(win, "Kid A");
        verify(findChild(stage, "playButton").visible);

        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.PullingOut);
        verify(deck.animating);
        tryCompare(deck, "phase", MdDeck.FlyingIn, 2000);
        verify(deck.flyingCartridge.visible);
        verify(stage.cartridgeRemoved);
        tryVerify(() => deck.lid > 0.3, 2000, "the lid opens on the way");
        act(win, KeyRouter.PlayCase); // again: in at once
        compare(deck.phase, MdDeck.Loaded);
        compare(deck.lid, 0);
        verify(!deck.flyingCartridge.visible);
        verify(deck.player.loadedCartridge.visible);
        compare(deck.disc.source, "visualizer");
        compare(deck.disc.title, "Kid A");
        tryCompare(Playback, "playing", true, 5000);
        verify(Playback.currentPath.endsWith("Kid A/01.flac"), Playback.currentPath);
        tryVerify(() => deck.player.loadedCartridge.disc.animating, 3000, "the disc turns");

        // The empty case: Flip and Eject, no Play / Put back.
        verify(!findChild(stage, "playButton").visible);
        verify(!findChild(stage, "putBackButton").visible);
        verify(findChild(stage, "ejectButton").visible);
    }

    function test_insertFromTheBack() {
        const win = openAlbums();
        const deck = win.visualizerView.deckArea;
        const stage = takeOutSettled(win, "OK Computer");
        act(win, KeyRouter.FlipCase);
        tryCompare(stage, "phase", MdCaseStage.Back, 2000);
        act(win, KeyRouter.MoveDown, 2);
        compare(stage.trackIndex, 2);
        act(win, KeyRouter.Activate);
        compare(deck.phase, MdDeck.PullingOut);
        compare(deck.flipMode, 1); // from the back: label-up on the way
        act(win, KeyRouter.Activate);
        compare(deck.phase, MdDeck.Loaded);
        compare(deck.playRow, 2);
        compare(deck.disc.title, "OK Computer");
        // (OK Computer's files aren't on disk: what was asked for is checked, not the sound.)
    }

    function test_playerKeys() {
        const win = openAlbums();
        const deck = loadKidA(win);
        const p = deck.player;

        mouseClick(findChild(p, "keyPause"));
        tryCompare(Playback, "paused", true, 3000);
        tryVerify(() => !p.loadedCartridge.disc.animating, 3000, "it coasts to a stop");
        const jog = findChild(p, "jogLever");
        mouseClick(jog, jog.width / 2, jog.height / 2); // the middle: play / pause
        tryCompare(Playback, "playing", true, 3000);

        Playback.volume = 0.5;
        mouseClick(findChild(p, "keyVolDown"));
        compare(Playback.volume.toFixed(2), "0.45");
        mouseClick(findChild(p, "keyVolUp"));
        compare(Playback.volume.toFixed(2), "0.50");
        Playback.volume = 0.98;
        mouseClick(findChild(p, "keyVolUp"));
        compare(Playback.volume, 1);

        compare(p.displayMode, 0);
        const display = findChild(p, "keyDisplay");
        mouseClick(display);
        compare(p.displayMode, 1);
        mouseClick(display);
        compare(p.displayMode, 2);
        mouseClick(display);
        compare(p.displayMode, 0);

        mouseClick(findChild(p, "keyStop"));
        tryCompare(Playback, "playing", false, 3000);
        compare(deck.phase, MdDeck.Loaded); // stopped: the disc stays in
        verify(p.loadedCartridge.visible);

        // The volume keys work anywhere.
        Playback.volume = 0.5;
        act(win, KeyRouter.VolumeDown);
        compare(Playback.volume.toFixed(2), "0.45");
        act(win, KeyRouter.VolumeUp);
        compare(Playback.volume.toFixed(2), "0.50");
    }

    function test_lockedPileAndEject() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = loadKidA(win);
        const stage = v.caseArea;

        // Esc on the empty case: the keyboard to the pile, the case stays out.
        verify(v.caseFocused);
        act(win, KeyRouter.Back);
        verify(!v.caseFocused);
        compare(stage.phase, MdCaseStage.Front);
        // Another case doesn't come out: a hint instead.
        cursorTo(win, "OK Computer");
        act(win, KeyRouter.Activate);
        compare(stage.item.title, "Kid A");
        verify(deck.hinting);
        verify(findChild(deck, "ejectHint").visible);

        // The OPEN slide: stop, the cartridge back in its case, the case back in the pile.
        mouseClick(findChild(deck.player, "openSlide"));
        compare(deck.phase, MdDeck.OpeningLid);
        tryCompare(Playback, "playing", false, 3000);
        tryCompare(deck, "phase", MdDeck.FlyingBack, 3000);
        verify(deck.flyingCartridge.visible);
        tryCompare(deck, "phase", MdDeck.Empty, 3000);
        verify(!stage.cartridgeRemoved);
        tryCompare(stage, "phase", MdCaseStage.Idle, 3000);

        // Now another case comes out.
        act(win, KeyRouter.Activate);
        compare(stage.phase, MdCaseStage.TakingOut);
        compare(stage.item.title, "OK Computer");
        act(win, KeyRouter.Back);
        tryCompare(stage, "phase", MdCaseStage.Idle, 3000);

        // `O`: eject; `O` again: done at once.
        loadKidA(win);
        act(win, KeyRouter.Eject);
        compare(deck.phase, MdDeck.OpeningLid);
        act(win, KeyRouter.Eject);
        compare(deck.phase, MdDeck.Empty);
        verify(stage.phase === MdCaseStage.PuttingBack || stage.phase === MdCaseStage.Idle);
        tryCompare(stage, "phase", MdCaseStage.Idle, 3000);
    }

    // The visualizer mirrors what the library plays.
    function test_playbackStartedElsewhere() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = v.deckArea;
        const stage = takeOutSettled(win, "OK Computer");
        const kidA = v.pile.model.get(indexIn(v.pile.model, "title", "Kid A")).albumId;

        Playback.playItem("album", kidA);
        tryCompare(deck, "phase", MdDeck.Loaded, 5000);
        verify(!deck.animating);
        compare(deck.disc.source, "external");
        compare(deck.disc.id, kidA);
        verify(deck.player.loadedCartridge.visible);
        compare(stage.phase, MdCaseStage.Idle); // the case that was out went back
        verify(!v.caseFocused);

        // Its eject: stop, the disc lifts out and fades (no case to go back to).
        act(win, KeyRouter.Eject);
        tryCompare(deck, "phase", MdDeck.FlyingBack, 3000);
        verify(deck.fadingOut);
        tryCompare(deck, "phase", MdDeck.Empty, 3000);
        tryCompare(Playback, "playing", false, 3000);
    }

    // A mix disc keeps its own shell while one of its tracks plays (they come from many albums,
    // so the playing album can't tell).
    function test_mixDiscIdentity() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = v.deckArea;
        playlistId = Library.createPlaylist("Mix in", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        v.browse.setMode(BrowsePane.Playlists);
        tryVerify(() => indexIn(v.pile.model, "name", "Mix in") >= 0, 5000);
        act(win, KeyRouter.MoveDown, indexIn(v.pile.model, "name", "Mix in") - v.pile.currentIndex);
        act(win, KeyRouter.Activate);
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        tryVerify(() => v.caseArea.trackModel.count === 1, 3000);
        act(win, KeyRouter.PlayCase);
        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.Loaded);
        tryCompare(Playback, "playing", true, 5000);
        wait(300);
        compare(deck.disc.kind, "playlist");
        compare(deck.disc.source, "visualizer");
        verify(deck.player.loadedCartridge.mix);
    }

    // Neon's V3 review: an Eject during the insert doesn't start the music it then stops.
    function test_ejectCutsInsertShort() {
        // (An earlier test file may have left a track playing: clearQueue() keeps a playing one.)
        Playback.stop();
        tryCompare(Playback, "playing", false, 3000);
        Playback.clearQueue();
        tryCompare(Playback, "hasTrack", false, 3000);
        const win = openAlbums();
        const deck = win.visualizerView.deckArea;
        const stage = takeOutSettled(win, "Kid A");
        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.PullingOut);
        act(win, KeyRouter.Eject);
        compare(deck.phase, MdDeck.OpeningLid);
        verify(!deck.awaitingPlay);
        verify(!Playback.hasTrack, "nothing was queued");
        tryCompare(deck, "phase", MdDeck.Empty, 4000);
        tryCompare(stage, "phase", MdCaseStage.Idle, 3000);
        verify(!Playback.playing);
    }

    // Neon's V3 review: a row clicked while the cartridge is on its way finishes the insert and
    // plays that row (it used to start a second, competing play).
    function test_trackClickedMidInsert() {
        const win = openAlbums();
        const deck = win.visualizerView.deckArea;
        const stage = takeOutSettled(win, "Kid A");
        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.PullingOut);
        stage.playTrackRequested(stage.trackModel, 0);
        compare(deck.phase, MdDeck.Loaded);
        verify(stage.cartridgeRemoved);
        compare(deck.disc.source, "visualizer");
        tryCompare(Playback, "playing", true, 5000);
        wait(300);
        compare(deck.disc.source, "visualizer"); // still ours, not taken for playback elsewhere
    }

    // Neon's V3 review: playback started elsewhere during an eject puts its disc in at the end.
    function test_playbackStartedDuringEject() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = loadKidA(win);
        const kidA = v.pile.model.get(indexIn(v.pile.model, "title", "Kid A")).albumId;
        act(win, KeyRouter.Eject);
        compare(deck.phase, MdDeck.OpeningLid);
        Playback.playItem("album", kidA);
        tryCompare(Playback, "playing", true, 5000);
        tryVerify(() => deck.phase === MdDeck.Loaded && deck.disc.source === "external", 5000);
        tryCompare(v.caseArea, "phase", MdCaseStage.Idle, 3000);
    }

    // Neon's V3 review: an empty mix disc can't go in (it would claim every track as its own).
    function test_emptyMixDisc() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = v.deckArea;
        playlistId = Library.createPlaylist("Empty mix", []);
        v.browse.setMode(BrowsePane.Playlists);
        tryVerify(() => indexIn(v.pile.model, "name", "Empty mix") >= 0, 5000);
        act(win, KeyRouter.MoveDown, indexIn(v.pile.model, "name", "Empty mix") - v.pile.currentIndex);
        act(win, KeyRouter.Activate);
        tryCompare(v.caseArea, "phase", MdCaseStage.Front, 3000);
        wait(300);
        compare(v.caseArea.trackModel.count, 0);
        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.Empty);
    }

    function test_sideLcd() {
        const lcd = createTemporaryObject(lcdComp, tc, { active: true });
        verify(!findChild(lcd, "lcdMeter").visible); // analyser off: blank
        compare(lcd.lowLit, 0);
        const bassOnly = [];
        for (let i = 0; i < 24; ++i)
            bassOnly.push(i < 10 ? 1 : 0);
        lcd.levels = bassOnly;
        lcd.analyzerEnabled = true;
        verify(findChild(lcd, "lcdMeter").visible);
        compare(lcd.lowLit, 10);
        compare(lcd.highLit, 0);

        lcd.trackNumber = 3;
        compare(findChild(lcd, "lcdTrack").text, "03");
        lcd.positionMs = 65000;
        lcd.durationMs = 200000;
        compare(findChild(lcd, "lcdReadout").text, "01:05");
        lcd.displayMode = 1;
        compare(findChild(lcd, "lcdReadout").text, "-02:15");
        lcd.displayMode = 2;
        lcd.title = "Everything In Its Right Place";
        compare(findChild(lcd, "lcdReadout").text, "Everything In Its Right Place");
        lcd.active = false;
        compare(findChild(lcd, "lcdTrack").text, "--");

        // On the player: track 01 while Kid A plays.
        const win = openAlbums();
        const deck = loadKidA(win);
        tryCompare(findChild(deck.player.lcd, "lcdTrack"), "text", "01");
    }

    // Window hidden: nothing animates; insert and eject land at once; the disc doesn't turn.
    function test_hiddenWindow() {
        const win = openAlbums();
        const v = win.visualizerView;
        const deck = v.deckArea;
        takeOutSettled(win, "Kid A");
        Playback.windowVisible = false;
        act(win, KeyRouter.PlayCase);
        compare(deck.phase, MdDeck.Loaded);
        tryCompare(Playback, "playing", true, 5000);
        wait(200);
        verify(!deck.player.loadedCartridge.disc.animating);
        act(win, KeyRouter.Eject);
        compare(deck.phase, MdDeck.Empty);
        compare(v.caseArea.phase, MdCaseStage.Idle);
    }

    function test_layouts() {
        const win = openAlbums();
        const deck = win.visualizerView.deckArea;
        const sizes = [[1280, 800, false], [1280, 800, true], [960, 600, false], [960, 600, true]];
        for (const [w, h, queue] of sizes) {
            win.width = w;
            win.height = h;
            win.queueOpen = queue;
            wait(100);
            const c = deck.caseStage;
            const p = deck.player;
            const tag = w + "×" + h + (queue ? " +queue" : "") + ": ";
            // The cells don't overlap, and the player's drawing fits its cell.
            if (deck.stacked)
                verify(c.y + c.height <= p.y, tag + "stacked cells overlap");
            else
                verify(c.x + c.width <= p.x, tag + "side-by-side cells overlap");
            verify(p.totalW <= p.width + 1 && p.totalH <= p.height + 1, tag + "player " + p.totalW + "×" + p.totalH + " in " + p.width + "×" + p.height);
            verify(c.caseH >= 120, tag + "case " + c.caseH);
        }
        win.queueOpen = false;
    }

    // MMM_SCREENSHOT_DIR: the player with Kid A in, its side LCD, and the lid open.
    function test_screenshots() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        Playback.analyzerEnabled = true;
        const win = openAlbums();
        const deck = loadKidA(win);
        tryVerify(() => deck.player.loadedCartridge.disc.showingCover, 5000);
        wait(1500);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-loaded.png"); // incl. the side LCD
        deck.player.displayMode = 2;
        wait(400);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-title.png");
        act(win, KeyRouter.Eject);
        tryCompare(deck, "phase", MdDeck.Empty, 4000);
        tryCompare(win.visualizerView.caseArea, "phase", MdCaseStage.Idle, 3000);
        takeOutSettled(win, "Kid A");
        act(win, KeyRouter.PlayCase);
        tryCompare(deck, "phase", MdDeck.FlyingIn, 2000);
        tryVerify(() => deck.fly > 0.45, 2000);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-inserting.png");
        tryCompare(deck, "phase", MdDeck.Loaded, 3000);
        act(win, KeyRouter.Eject);
        tryVerify(() => deck.lid > 0.9, 2000);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-open.png");
        tryCompare(deck, "phase", MdDeck.FlyingBack, 3000);
        tryVerify(() => deck.fly > 0.4, 2000);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-flying.png");
        win.width = win.minimumWidth;
        win.height = win.minimumHeight;
        tryCompare(deck, "phase", MdDeck.Empty, 3000);
        loadKidA(win);
        wait(500);
        grabImage(win.contentItem.parent).save(screenshotDir + "/visualizer-player-small.png");
    }
}
