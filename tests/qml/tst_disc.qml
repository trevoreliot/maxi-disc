// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Q4: the transport bar's spinning MiniDisc. Most cases drive a disc's inputs directly; the
// fixture's Kid A / 01.flac (8 s, embedded art) plays for the end-to-end ones.
TestCase {
    id: tc
    name: "Disc"
    when: windowShown
    width: 400
    height: 200
    visible: true

    property int playlistId: 0 // deleted in cleanup(), even when a test fails half-way

    Component {
        id: discComp
        SpinningDisc {
            width: 56
            height: 56
        }
    }
    Component {
        id: freeDiscComp
        // Inputs not bound to Playback: set by the test.
        SpinningDisc {
            width: 56
            height: 56
            coverKey: ""
            coverUrl: ""
            playing: false
            paused: false
            spinEnabled: true
            shown: true
        }
    }
    Component {
        id: playlistComp
        PlaylistPage {
            y: 70
            width: 380
            height: 100
        }
    }
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
        if (playlistId > 0)
            Library.deletePlaylist(playlistId);
        playlistId = 0;
        Playback.discSpinEnabled = true;
        Playback.stop();
        Playback.clearQueue();
    }

    function playFixture() {
        playlistId = Library.createPlaylist("disc", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        verify(playlistId > 0);
        const playlist = createTemporaryObject(playlistComp, tc, { playlistId: playlistId, playlistName: "disc", itemCount: 1 });
        tryCompare(playlist, "trackCount", 1);
        playlist.activateCurrent();
        tryCompare(Playback, "playing", true);
    }

    function test_spinCoastAndResume() {
        const d = createTemporaryObject(freeDiscComp, tc);
        verify(!d.animating);
        compare(d.angle, 0);

        d.playing = true;
        tryVerify(() => d.animating);
        tryVerify(() => d.angle > 20, 2000, "it turns");

        // Pause: the speed eases down from where it was (no jump), then the disc holds still.
        d.paused = true;
        d.playing = false;
        wait(50);
        verify(d.speed <= 1 && d.speed > 0.3, "coasting: " + d.speed);
        tryCompare(d, "animating", false, 2000);
        const held = d.angle;
        wait(150);
        compare(d.angle, held);

        // Resumes from where it stopped.
        d.angle = 100;
        d.paused = false;
        d.playing = true;
        wait(150);
        verify(d.angle > 100 && d.angle < 180, "resumed from 100: " + d.angle);

        // Stop: a quick stop.
        d.playing = false;
        tryCompare(d, "animating", false, 1000);

        // Spinning off, or the window hidden, while playing: halts at once, keeps the angle.
        d.playing = true;
        tryVerify(() => d.animating);
        d.spinEnabled = false;
        wait(20);
        verify(!d.animating);
        const off = d.angle;
        wait(100);
        compare(d.angle, off);
        d.spinEnabled = true;
        tryVerify(() => d.animating);
        d.shown = false;
        wait(20);
        verify(!d.animating);
        d.shown = true;
        d.playing = false;
    }

    function test_coverFadesAndNeverReloadsWithinAnAlbum() {
        playFixture();
        const url = Playback.discCoverUrl;
        const key = Playback.coverKey;
        verify(url.startsWith("image://coverround/"), url);
        verify(key !== "");
        Playback.stop();

        const d = createTemporaryObject(freeDiscComp, tc);
        verify(!d.showingCover);
        d.coverKey = key;
        d.coverUrl = url;
        tryCompare(d, "showingCover", true, 5000);
        const shownSource = d.front.source.toString();
        compare(shownSource, url);
        compare(d.front.opacity, 1);

        // Another track of the album: a different URL (embedded art names the track), the same key.
        d.coverUrl = url + "-other-track";
        d.coverKey = key;
        wait(100);
        verify(d.showingCover);
        compare(d.front.source.toString(), shownSource); // not reloaded
        compare(d.back.source.toString(), "");           // nothing loading either

        // No art: back to the plain disc.
        d.coverKey = "";
        d.coverUrl = "";
        tryCompare(d, "showingCover", false, 2000);
        compare(d.front.opacity, 0);

        // A missing picture (the file's gone): stays the plain disc, no error shown.
        d.coverKey = "999";
        d.coverUrl = "image://coverround/999/bm9wZQ"; // "nope"
        wait(500);
        verify(!d.showingCover);
    }

    function test_followsPlayback() {
        const d = createTemporaryObject(discComp, tc);
        verify(!d.animating);
        playFixture();
        tryVerify(() => d.animating, 3000, "spins while playing");
        tryCompare(d, "showingCover", true, 5000);
        Playback.playPause();
        tryCompare(Playback, "paused", true);
        tryCompare(d, "animating", false, 3000);
        verify(d.showingCover); // paused: the cover stays
    }

    function test_menu() {
        const d = createTemporaryObject(discComp, tc);
        menuSpy.clear();
        mouseClick(d, d.width / 2, d.height / 2, Qt.RightButton);
        compare(menuSpy.count, 1);
        let entries = menuSpy.signalArguments[0][3];
        compare(entries.length, 1); // nothing playing: only the spin switch
        compare(entries[0].text, "Stop spinning");
        entries[0].onTriggered();
        verify(!Playback.discSpinEnabled);

        playFixture();
        d.openMenu(10, 10);
        entries = menuSpy.signalArguments[1][3];
        compare(entries[0].text, "Go to album");
        compare(entries[entries.length - 1].text, "Start spinning");
        verify(entries[entries.length - 2].separator);
        entries[entries.length - 1].onTriggered();
        verify(Playback.discSpinEnabled);
    }

    // V2: the cartridge's disc is only a picture: no menu, no going to the album.
    function test_notInteractive() {
        const d = createTemporaryObject(freeDiscComp, tc, { interactive: false });
        menuSpy.clear();
        mouseClick(d, d.width / 2, d.height / 2, Qt.RightButton);
        mouseClick(d, d.width / 2, d.height / 2);
        compare(menuSpy.count, 0);
        verify(!d.ToolTip.visible);
    }

    // The analyser keeps its 160 px beside the disc in the smallest window.
    function test_smallestWindow() {
        const win = createTemporaryObject(mainComp, tc);
        win.width = win.minimumWidth;
        tryCompare(win, "width", win.minimumWidth);
        const analyzer = findChild(win.footer, "audioAnalyzer");
        const disc = findChild(win.footer, "spinningDisc");
        verify(analyzer, "analyser found");
        verify(disc, "disc found");
        tryVerify(() => analyzer.width >= 160, 2000, "analyser width " + analyzer.width);
        compare(disc.width, 56);
        const toggle = findChild(win.footer, "listenbrainzToggle");
        const right = toggle.mapToItem(win.footer, toggle.width, 0).x;
        verify(right <= win.footer.width, "nothing cut off on the right: " + right + " > " + win.footer.width);
    }

    // MMM_SCREENSHOT_DIR: the disc plain and with a cover, at 1× and 2×.
    function test_screenshot() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        playFixture();
        const url = Playback.discCoverUrl;
        const key = Playback.coverKey;
        Playback.stop();
        const plain = createTemporaryObject(freeDiscComp, tc, { x: 10, y: 10 });
        const big = createTemporaryObject(freeDiscComp, tc, { x: 80, y: 10, width: 112, height: 112 });
        const covered = createTemporaryObject(freeDiscComp, tc, { x: 210, y: 10 });
        big.coverKey = key;
        big.coverUrl = url;
        covered.coverKey = key;
        covered.coverUrl = url;
        tryCompare(covered, "showingCover", true, 5000);
        tryCompare(big, "showingCover", true, 5000);
        plain.angle = 30;
        wait(100);
        grabImage(tc).save(screenshotDir + "/disc.png");
    }
}
