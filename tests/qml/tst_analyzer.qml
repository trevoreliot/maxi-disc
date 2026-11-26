// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Q3: the transport bar's analyser (SpectrumMeter + AudioAnalyzer) and Playback.levels.
// The fixture's Kid A / 01.flac is an 8 s 440 Hz tone: band 9 (378-486 Hz).
TestCase {
    id: tc
    name: "Analyzer"
    when: windowShown
    width: 700
    height: 200
    visible: true

    readonly property int toneBand: 9
    property int playlistId: 0 // deleted in cleanup(), even when a test fails half-way

    Component {
        id: meterComp
        SpectrumMeter {
            width: 240
            height: 40 // 10 segments of 4 px
            unlitColor: Theme.analyzerUnlit
            lowColor: Theme.analyzerLow
            midColor: Theme.analyzerMid
            highColor: Theme.analyzerHigh
            peakColor: Theme.analyzerPeak
        }
    }
    Component {
        id: analyzerComp
        AudioAnalyzer {
            width: 600
            height: 56
        }
    }
    Component {
        id: playlistComp
        PlaylistPage {
            y: 80
            width: 600
            height: 100
        }
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
        Playback.analyzerEnabled = true;
        Playback.windowVisible = true;
        Playback.stop();
        Playback.clearQueue();
    }

    function stepFor(meter, seconds) {
        for (let t = 0; t < seconds - 1e-9; t += 0.05)
            meter.step(0.05);
    }

    function levelsWith(band, value) {
        const l = [];
        for (let i = 0; i < 24; ++i)
            l.push(i === band ? value : 0);
        return l;
    }

    function playFixture(): void {
        const id = Library.createPlaylist("analyser", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        verify(id > 0);
        playlistId = id;
        const playlist = createTemporaryObject(playlistComp, tc, { playlistId: id, playlistName: "analyser", itemCount: 1 });
        tryCompare(playlist, "trackCount", 1);
        playlist.activateCurrent();
        tryCompare(Playback, "playing", true);
    }

    function test_segmentsAndPeakFall() {
        const m = createTemporaryObject(meterComp, tc);
        compare(m.segmentCount, 10);
        compare(m.bandCount, 24);
        verify(!m.animating);

        m.playing = true;
        verify(m.animating);
        m.levels = levelsWith(3, 0.8);
        stepFor(m, 0.2); // fast attack: there within a few frames
        compare(m.litSegments(3), 8);
        compare(m.peakSegment(3), 7);
        compare(m.litSegments(2), 0);
        compare(m.peakSegment(2), -1);

        // The signal goes away: the bar falls at once, the peak hangs 0.5 s, then falls too.
        m.levels = levelsWith(3, 0);
        stepFor(m, 0.4);
        verify(m.litSegments(3) <= 2);
        compare(m.peakSegment(3), 7);
        stepFor(m, 0.3);
        verify(m.peakSegment(3) < 7);
        verify(m.peakSegment(3) >= 0);

        // Stopped: everything comes to rest and the meter stops asking for frames.
        m.playing = false;
        stepFor(m, 1.5);
        compare(m.litSegments(3), 0);
        compare(m.peakSegment(3), -1);
        compare(m.displayLevel(3), 0);
        compare(m.peakLevel(3), 0);
        verify(!m.animating);
    }

    function test_menuTurnsItOff() {
        const a = createTemporaryObject(analyzerComp, tc);
        const off = findChild(a, "analyzerOffLabel");
        verify(!off.visible);
        menuSpy.clear();
        mouseClick(a, 100, 20, Qt.RightButton);
        compare(menuSpy.count, 1);
        let entries = menuSpy.signalArguments[0][3];
        compare(entries.length, 1);
        compare(entries[0].text, "Turn analyser off");
        entries[0].onTriggered();
        verify(!Playback.analyzerEnabled);
        verify(off.visible);

        a.openMenu(10, 10);
        entries = menuSpy.signalArguments[1][3];
        compare(entries[0].text, "Turn analyser on");
        entries[0].onTriggered();
        verify(Playback.analyzerEnabled);
        verify(!off.visible);
    }

    function test_levelsFollowPlayback() {
        const a = createTemporaryObject(analyzerComp, tc);
        const meter = a.meter;
        verify(!a.ticking); // at rest: no frames

        playFixture();
        tryVerify(() => Playback.levels[tc.toneBand] > 0.5, 5000, "the 440 Hz band lights");
        verify(a.ticking);
        tryVerify(() => meter.litSegments(tc.toneBand) >= 4, 5000);
        // The tone's band stands well above the rest (window leakage lights a little around it).
        tryVerify(() => {
            let others = 0;
            for (let b = 0; b < 24; ++b) {
                if (Math.abs(b - tc.toneBand) >= 2)
                    others = Math.max(others, Playback.levels[b]);
            }
            return Playback.levels[tc.toneBand] - others > 0.3;
        }, 3000, "other bands stay well below the tone's");

        // Paused: levels go to 0 and the meter falls to rest, then stops ticking.
        Playback.playPause();
        tryCompare(Playback, "paused", true);
        compare(Playback.levels[tc.toneBand], 0);
        tryCompare(a, "ticking", false, 3000);
        compare(meter.litSegments(tc.toneBand), 0);

        // Resumed, then the window "minimised": nothing analysed or drawn for nobody.
        Playback.playPause();
        tryVerify(() => Playback.levels[tc.toneBand] > 0.5, 5000);
        Playback.windowVisible = false;
        compare(Playback.levels[tc.toneBand], 0);
        verify(!a.ticking);
        wait(200);
        compare(Playback.levels[tc.toneBand], 0);
        Playback.windowVisible = true;
        tryVerify(() => Playback.levels[tc.toneBand] > 0.5, 5000);

        // Off: the same.
        Playback.analyzerEnabled = false;
        compare(Playback.levels[tc.toneBand], 0);
        wait(200);
        compare(Playback.levels[tc.toneBand], 0);
        tryCompare(a, "ticking", false, 3000);
        Playback.analyzerEnabled = true;
        tryVerify(() => Playback.levels[tc.toneBand] > 0.5, 5000);
    }

    // MMM_SCREENSHOT_DIR: the analyser at rest and lit (a made-up spectrum).
    function test_screenshot() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        const a = createTemporaryObject(analyzerComp, tc, { y: 10 });
        wait(100);
        grabImage(a).save(screenshotDir + "/analyzer-rest.png");

        const m = a.meter;
        m.playing = true;
        const shape = [];
        for (let b = 0; b < 24; ++b)
            shape.push(Math.max(0.05, 0.85 - Math.abs(b - 5) * 0.06 + (b % 3) * 0.05));
        m.levels = shape;
        stepFor(m, 0.3);
        m.levels = shape.map(v => v * 0.6); // peaks hang above the bars
        stepFor(m, 0.15);
        wait(100);
        grabImage(a).save(screenshotDir + "/analyzer-lit.png");
        m.playing = false;
    }
}
