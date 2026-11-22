// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Transport bar controls and the volume wedge.
TestCase {
    id: tc
    name: "Transport"
    when: windowShown
    width: 1100
    height: 400
    visible: true

    Component {
        id: wedgeComp
        VolumeWedge {
            width: 136
            height: 48
        }
    }
    Component {
        id: barComp
        TransportBar {
            width: 1100
        }
    }
    SignalSpy {
        id: spy
    }

    function cleanup() {
        Playback.stop(); // clearing keeps a playing track
        Playback.clearQueue();
    }

    function test_volumeWedge() {
        const w = createTemporaryObject(wedgeComp, tc, { value: 0.5 });
        const label = findChild(w, "volumeLabel");
        const wedge = findChild(w, "wedge");
        compare(label.text, "50%");
        spy.target = w;
        spy.signalName = "moved";
        spy.clear();

        mouseClick(wedge, 0, wedge.height / 2);
        compare(w.value, 0);
        compare(label.text, "0%");
        compare(spy.count, 1);

        mouseClick(wedge, wedge.width * 0.75, wedge.height / 2);
        verify(Math.abs(w.value - 0.75) < 0.02);

        // Dragging past either end clamps.
        mousePress(wedge, wedge.width / 2, 10);
        mouseMove(wedge, wedge.width + 50, 10);
        compare(w.value, 1);
        mouseMove(wedge, -50, 10);
        compare(w.value, 0);
        mouseRelease(wedge, -50, 10);

        mouseWheel(wedge, 10, 10, 0, 120);
        verify(Math.abs(w.value - 0.05) < 1e-9);
        mouseWheel(wedge, 10, 10, 0, -120);
        compare(w.value, 0);

        // Setting the value from outside (Playback) doesn't count as a user move.
        spy.clear();
        w.value = 0.3;
        compare(spy.count, 0);
        compare(label.text, "30%");
    }

    function test_transportBar() {
        const bar = createTemporaryObject(barComp, tc);
        verify(!findChild(bar, "playPauseButton").enabled); // nothing queued
        verify(!findChild(bar, "nextButton").enabled);
        compare(findChild(bar, "nowPlayingLabel").text, "Not playing");

        // Stubs for features that aren't built yet: visible, but inert.
        const discord = findChild(bar, "discordToggle");
        verify(!discord.interactive);
        mouseClick(discord, discord.width / 2, discord.height - 8);
        verify(!discord.checked);
        verify(!findChild(bar, "lastfmToggle").interactive);
        verify(findChild(bar, "shuffleButton") !== null);

        // Repeat cycles off → all → one → off.
        const before = Playback.repeatMode;
        const repeat = findChild(bar, "repeatButton");
        mouseClick(repeat);
        compare(Playback.repeatMode, (before + 1) % 3);
        mouseClick(repeat);
        mouseClick(repeat);
        compare(Playback.repeatMode, before);

        // The volume wedge drives Playback.volume.
        const oldVolume = Playback.volume;
        const wedge = findChild(findChild(bar, "volumeWedge"), "wedge");
        mouseClick(wedge, wedge.width / 2, wedge.height / 2);
        verify(Math.abs(Playback.volume - 0.5) < 0.02);
        Playback.volume = oldVolume;
    }

    function test_playAndStopButtons() {
        // Kid A / 01.flac is the fixture's only real audio file.
        const id = Library.createPlaylist("tmp", [fixtureDir + "/Radiohead/Kid A/01.flac"]);
        verify(id > 0);
        const playlist = createTemporaryObject(playlistComp, tc, { playlistId: id, playlistName: "tmp", itemCount: 1 });
        tryCompare(playlist, "trackCount", 1);
        playlist.activateCurrent();
        tryCompare(Playback, "playing", true);

        const bar = createTemporaryObject(barComp, tc);
        compare(findChild(bar, "nowPlayingLabel").text, "Everything in Its Right Place — Radiohead");
        mouseClick(findChild(bar, "playPauseButton"));
        tryCompare(Playback, "paused", true);
        mouseClick(findChild(bar, "playPauseButton"));
        tryCompare(Playback, "playing", true);
        mouseClick(findChild(bar, "stopButton"));
        tryCompare(Playback, "playing", false);
        verify(!Playback.paused);
        verify(Library.deletePlaylist(id));
    }

    Component {
        id: playlistComp
        PlaylistPage {
            y: 120
            width: 900
            height: 260
        }
    }
}
