// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Playback UI against the fixture library (only Kid A / 01.flac is a real audio file;
// the others are paths in the DB, which also exercises skipping unplayable tracks).
TestCase {
    id: tc
    name: "PlaybackUi"
    when: windowShown
    width: 1100
    height: 700
    visible: true

    Component {
        id: gridComp
        AlbumGrid {
            width: 900
            height: 400
        }
    }
    Component {
        id: pageComp
        AlbumPage {
            width: 900
            height: 500
        }
    }
    Component {
        id: playlistComp
        PlaylistPage {
            width: 900
            height: 500
        }
    }
    Component {
        id: barComp
        NowPlayingBar {
            width: 1100
        }
    }
    Component {
        id: queueComp
        QueuePanel {
            width: 340
            height: 500
        }
    }

    function shot(item, name) {
        if (screenshotDir === "")
            return;
        wait(300);
        grabImage(item).save(screenshotDir + "/" + name + ".png");
    }

    function album(grid, title) {
        for (let i = 0; i < grid.count; ++i) {
            const tile = grid.itemAtIndex(i);
            if (tile && tile.title === title)
                return tile;
        }
        return null;
    }

    function cleanup() {
        Playback.clearQueue();
    }

    function test_playAlbumFromPage() {
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        const kidA = album(grid, "Kid A");
        const page = createTemporaryObject(pageComp, tc, {
            albumId: kidA.albumId, albumTitle: "Kid A", albumArtist: "Radiohead", year: 2000, coverUrl: kidA.coverUrl
        });
        tryCompare(page, "trackCount", 2);

        mouseClick(findChild(page, "playAlbumButton"));
        compare(Playback.queue.count, 2);
        compare(Playback.queue.currentIndex, 0);
        compare(Playback.title, "Everything in Its Right Place");
        verify(Playback.coverUrl.startsWith("image://cover/"));
        tryCompare(Playback, "playing", true);

        const bar = createTemporaryObject(barComp, tc, { y: 520 });
        shot(bar, "now-playing-bar");
        Playback.playPause();
        tryCompare(Playback, "paused", true);
        Playback.playPause();
        tryCompare(Playback, "playing", true);
    }

    function test_enqueueAndPlayNext() {
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        const wall = createTemporaryObject(pageComp, tc, { albumId: album(grid, "The Wall").albumId });
        tryCompare(wall, "trackCount", 3);
        const kid = createTemporaryObject(pageComp, tc, { albumId: album(grid, "Kid A").albumId });
        tryCompare(kid, "trackCount", 2);

        Playback.playFromModel(null); // ignored
        compare(Playback.queue.count, 0);

        Playback.playFromModel(wall.tracksModel, 0);
        compare(Playback.queue.count, 3);
        compare(Playback.title, "In the Flesh?");
        Playback.enqueueFromModel(kid.tracksModel, true); // play next: right after the current track
        compare(Playback.queue.count, 5);
        compare(Playback.queue.currentIndex, 0);
        Playback.next();
        compare(Playback.title, "Everything in Its Right Place"); // not The Wall's 2nd track
        Playback.enqueueFromModel(wall.tracksModel, false, 2); // a single row, at the end
        compare(Playback.queue.count, 6);
    }

    function test_queuePanel() {
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        const kidA = album(grid, "Kid A");
        const page = createTemporaryObject(pageComp, tc, { albumId: kidA.albumId, albumTitle: "Kid A", albumArtist: "Radiohead" });
        tryCompare(page, "trackCount", 2);
        mouseClick(findChild(page, "playAlbumButton"));
        const panel = createTemporaryObject(queueComp, tc, { x: 760 });
        compare(Playback.queue.count, 2);
        shot(panel, "queue-panel");
        Playback.removeFromQueue(1);
        compare(Playback.queue.count, 1);
    }

    function test_playlistPage() {
        const base = fixtureDir + "/";
        const id = Library.createPlaylist("Mix", [base + "Radiohead/Kid A/01.flac", base + "missing/song.flac",
                                                  base + "Beatles/Abbey Road/02.flac"]);
        verify(id > 0);
        const page = createTemporaryObject(playlistComp, tc, { playlistId: id, playlistName: "Mix", itemCount: 3 });
        tryCompare(page, "trackCount", 2); // the missing file is skipped
        shot(page, "playlist-page");
        verify(Library.deletePlaylist(id));
    }
}
