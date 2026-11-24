// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// M11b: sending music from the Library screen to the selected device (here the fake MiniDisc
// recorder): `i` on the browse list / album page (cursor or marked tracks), `I` for the play
// queue, the Send buttons, and the toast that says what happened.
TestCase {
    id: tc
    name: "Send"
    when: windowShown
    visible: true

    Component {
        id: mainComp
        Main {}
    }

    function cleanup() {
        MiniDisc.clearQueue();
        MiniDisc.recordView = false;
        MiniDisc.setTestDevices([]);
        Devices.setTestDevices([]);
        Playback.stop();
        Playback.clearQueue();
    }

    function act(win, action, times) {
        for (let i = 0; i < (times === undefined ? 1 : times); ++i)
            win.handleAction(action);
    }

    function openLibraryWithRecorder() {
        Devices.setTestDevices([]);
        MiniDisc.setTestDevices([{ id: "netmd:send", name: "MZ-SEND" }]);
        const win = createTemporaryObject(mainComp, tc);
        win.showScreen(Main.LibraryScreen);
        tryCompare(SendToDevice, "canSend", true);
        compare(SendToDevice.targetName, "MZ-SEND");
        MiniDisc.clearQueue();
        return win;
    }

    // Albums mode, cursor on Kid A, opened (focus in the album page).
    function openKidA(win) {
        const lib = win.libraryView;
        lib.browse.setMode(BrowsePane.Albums);
        tryCompare(lib.browse.currentList, "count", 5);
        let row = -1;
        for (let i = 0; i < lib.browse.currentList.count; ++i) {
            if (lib.browse.currentList.model.get(i).title === "Kid A")
                row = i;
        }
        verify(row >= 0);
        act(win, KeyRouter.MoveDown, row + 1);
        compare(lib.browse.currentItem().name.indexOf("Kid A"), 0);
        return row;
    }

    function test_browseRowAndToast() {
        const win = openLibraryWithRecorder();
        openKidA(win);
        // i on the browse row: the whole album.
        act(win, KeyRouter.AddToDevice);
        tryCompare(MiniDisc.queue, "count", 2);
        tryCompare(win.toast, "text", "Added 2 tracks to MZ-SEND");
        verify(!win.toast.isError);
        verify(win.toast.visible);
        act(win, KeyRouter.AddToDevice);
        tryCompare(win.toast, "text", "Already on MZ-SEND's list (2 tracks)");
    }

    function test_albumPageCursorAndMarks() {
        const win = openLibraryWithRecorder();
        const lib = win.libraryView;
        openKidA(win);
        act(win, KeyRouter.Activate); // into the album page
        compare(lib.content, LibraryView.Album);
        compare(lib.focusedPane, LibraryView.Content);
        tryCompare(lib.albumPage, "trackCount", 2);
        const button = findChild(lib.albumPage, "sendAlbumButtonInner");
        compare(button.text, "Send to MZ-SEND");
        verify(button.enabled);

        // i: the cursor track only.
        act(win, KeyRouter.AddToDevice);
        tryCompare(win.toast, "text", "Added 1 track to MZ-SEND");
        compare(MiniDisc.queue.count, 1);
        // Mark both (v, j, v): i sends the marked ones; the first is there already.
        act(win, KeyRouter.ToggleMark);
        act(win, KeyRouter.MoveDown);
        act(win, KeyRouter.ToggleMark);
        compare(lib.albumPage.markedRows.length, 2);
        compare(button.text, "Send 2 to MZ-SEND");
        act(win, KeyRouter.AddToDevice);
        tryCompare(win.toast, "text", "Added 1 track to MZ-SEND · 1 already there");
        compare(MiniDisc.queue.count, 2);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/library-send-toast.png");
        }

        // No device: the button says so, and i explains in the toast.
        MiniDisc.setTestDevices([]);
        tryCompare(SendToDevice, "canSend", false);
        compare(button.text, "Send to device");
        verify(!button.enabled);
        act(win, KeyRouter.AddToDevice);
        tryCompare(win.toast, "text", "Select a device on the Export screen first");
        verify(win.toast.isError);
    }

    function test_playQueue() {
        const win = openLibraryWithRecorder();
        act(win, KeyRouter.SendQueueToDevice);
        tryCompare(win.toast, "text", "The play queue is empty");
        verify(win.toast.isError);

        // Queue Kid A from its album page, then I: both tracks go, the queue stays.
        const lib = win.libraryView;
        openKidA(win);
        act(win, KeyRouter.Activate);
        tryCompare(lib.albumPage, "trackCount", 2);
        Playback.enqueueFromModel(lib.albumPage.tracksModel, false);
        compare(Playback.queue.count, 2);
        act(win, KeyRouter.SendQueueToDevice);
        tryCompare(win.toast, "text", "Added 2 tracks to MZ-SEND");
        compare(MiniDisc.queue.count, 2);
        compare(Playback.queue.count, 2);
        verify(findChild(win.queuePanel, "sendQueueButtonInner").enabled);

        // i in the queue panel: the track under its cursor (both are on the list already).
        MiniDisc.clearQueue();
        win.queueOpen = true;
        win.queueFocused = true;
        act(win, KeyRouter.AddToDevice);
        tryCompare(win.toast, "text", "Added 1 track to MZ-SEND");
        compare(MiniDisc.queue.count, 1);
        win.queueFocused = false;
    }
}
