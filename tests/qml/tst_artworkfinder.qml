// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// ArtworkFinderDialog's keyboard flow on fake albums (ArtFinder.setTestData: no network).
// Album ids don't exist in the fixture library, so nothing on disk changes: saving fails.
TestCase {
    id: tc
    name: "ArtworkFinderDialog"
    when: windowShown
    width: 1000
    height: 700
    visible: true

    ArtworkFinderDialog {
        id: dlg
    }
    SignalSpy {
        id: acceptedSpy
        target: dlg.artFinder
        signalName: "accepted"
    }

    function candidates(n) {
        const out = [];
        for (let i = 0; i < n; ++i)
            out.push({ title: "Release " + i, year: 2000 + i, format: "CD", trackCount: 10,
                       previewUrl: "", saveUrl: "file:///nonexistent/mmm-test/" + i + ".jpg" });
        return out;
    }

    function openWith(albums) {
        dlg.batch = true;
        dlg.focusPane = 0;
        dlg.open();
        tryCompare(dlg, "opened", true);
        dlg.artFinder.setTestData(albums);
    }

    function cleanup() {
        dlg.close();
        tryCompare(dlg, "visible", false);
        acceptedSpy.clear();
    }

    function test_navigateAcceptSkip() {
        openWith([
            { albumId: 900001, title: "Alpha", artist: "A", candidates: candidates(4) },
            { albumId: 900002, title: "Bravo", artist: "B", status: ArtFinder.NotFound },
            { albumId: 900003, title: "Charlie", artist: "C", candidates: candidates(1) },
        ]);
        const f = dlg.artFinder;
        compare(f.count, 3);
        compare(f.currentIndex, 0);
        compare(dlg.candidateIndex, 0);
        compare(findChild(dlg.contentItem, "artAlbumTitle").text, "Alpha");

        // Albums ↔ covers; the grid has 3 columns.
        dlg.handleAction(KeyRouter.MoveRight);
        compare(dlg.focusPane, 1);
        dlg.handleAction(KeyRouter.MoveRight);
        compare(dlg.candidateIndex, 1);
        dlg.handleAction(KeyRouter.MoveDown); // 1 + 3 is past the end: clamps to the last
        compare(dlg.candidateIndex, 3);
        dlg.handleAction(KeyRouter.MoveUp);
        compare(dlg.candidateIndex, 0);
        dlg.handleAction(KeyRouter.MoveRight);
        dlg.handleAction(KeyRouter.MoveRight);
        dlg.handleAction(KeyRouter.MoveRight); // the row ends at column 3
        compare(dlg.candidateIndex, 2);
        dlg.handleAction(KeyRouter.MoveLeft);
        dlg.handleAction(KeyRouter.MoveLeft);
        compare(dlg.candidateIndex, 0);
        dlg.handleAction(KeyRouter.MoveLeft); // off the grid's left edge: back to the albums
        compare(dlg.focusPane, 0);
        dlg.handleAction(KeyRouter.MoveDown);
        compare(f.currentIndex, 1);
        compare(dlg.candidateIndex, 0); // a new album starts at its best match
        dlg.handleAction(KeyRouter.MoveRight); // Bravo has no covers: stays on the list
        compare(dlg.focusPane, 0);
        dlg.handleAction(KeyRouter.MoveUp);

        // Enter on Alpha: saving starts and the cursor moves on to the next open album.
        dlg.candidateIndex = 2;
        dlg.handleAction(KeyRouter.Activate);
        compare(acceptedSpy.count, 1);
        compare(acceptedSpy.signalArguments[0][0], 900001);
        compare(f.currentIndex, 1);
        // s skips Bravo and moves on to Charlie.
        dlg.handleAction(KeyRouter.Skip);
        compare(f.currentIndex, 2);
        compare(f.skippedCount, 1);
        compare(findChild(dlg.contentItem, "artAlbumTitle").text, "Charlie");

        // Alpha's download fails (no such file): it's open again, with the error shown.
        tryVerify(() => f.nextOpenRow(0) === 0, 5000);
        f.currentIndex = 0;
        verify(f.current.error.indexOf("download") >= 0);
        compare(f.current.status, ArtFinder.Found);
        verify(findChild(dlg.contentItem, "artError").visible);

        // Skipping the last open albums leaves nothing to move to: the cursor stays.
        f.currentIndex = 2;
        dlg.handleAction(KeyRouter.Skip);
        compare(f.currentIndex, 0); // wrapped to Alpha, still open
        dlg.handleAction(KeyRouter.Skip);
        compare(f.nextOpenRow(0), -1);
        compare(f.skippedCount, 3);

        // r searches a skipped album again (here: nothing to search, it waits).
        dlg.handleAction(KeyRouter.Retry);
        compare(f.current.status, ArtFinder.Pending);

        dlg.handleAction(KeyRouter.Back);
        tryCompare(dlg, "visible", false);
        compare(f.count, 0);
    }

    function test_unhandledActionsFallThrough() {
        openWith([{ albumId: 900001, title: "Alpha", candidates: candidates(1) }]);
        verify(!dlg.handleAction(KeyRouter.PlayPause));
        verify(!dlg.handleAction(KeyRouter.ToggleQueue));
        verify(dlg.handleAction(KeyRouter.Skip));
    }

    function test_emptyList() {
        openWith([]);
        compare(dlg.artFinder.count, 0);
        verify(findChild(dlg.contentItem, "artEmptyLabel").visible);
        dlg.handleAction(KeyRouter.Activate); // nothing to accept: no crash
        dlg.handleAction(KeyRouter.Skip);
        compare(acceptedSpy.count, 0);
    }

    function test_screenshot() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        openWith([
            { albumId: 900001, title: "Alpha", artist: "Artist A", candidates: candidates(4) },
            { albumId: 900002, title: "Bravo", artist: "Artist B", status: ArtFinder.NotFound },
            { albumId: 900003, title: "Charlie", artist: "Artist C", status: ArtFinder.Searching },
        ]);
        dlg.focusPane = 1;
        wait(200);
        const img = grabImage(dlg.contentItem.parent);
        img.save(screenshotDir + "/artwork-finder.png");
    }
}
