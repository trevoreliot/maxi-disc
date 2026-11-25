// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Right-click menus, Q2: the now-playing area and the Export screen (stand-in devices and the
// in-memory MiniDisc recorder; files the menus would show are only recorded).
TestCase {
    id: tc
    name: "ContextMenuExport"
    when: windowShown

    Component {
        id: mainComp
        Main {}
    }

    function openMain(screen) {
        const win = createTemporaryObject(mainComp, tc);
        tryVerify(() => {
            win.requestActivate();
            win.contentItem.forceActiveFocus();
            return win.active && win.activeFocusItem !== null;
        }, 5000, "app window never got keyboard focus");
        win.showScreen(screen);
        return win;
    }

    function menuTexts(menu) {
        const out = [];
        for (let i = 0; i < menu.count; ++i) {
            const it = menu.itemAt(i);
            if (it && it.text !== undefined && it.text !== "")
                out.push(it.text);
        }
        return out;
    }

    function menuItem(menu, text) {
        for (let i = 0; i < menu.count; ++i) {
            const it = menu.itemAt(i);
            if (it && it.text === text)
                return it;
        }
        return null;
    }

    // (A click right as a popup opens can get lost: let it settle, like a hand would.)
    function rightClick(item, x, y) {
        wait(50);
        mouseClick(item, x ?? item.width / 2, y ?? item.height / 2, Qt.RightButton);
    }

    function pick(menu, text) {
        tryCompare(menu, "opened", true);
        wait(100);
        const it = menuItem(menu, text);
        verify(it, "no menu item " + text);
        verify(it.enabled, text + " is disabled");
        mouseClick(it);
        tryCompare(menu, "opened", false);
    }

    function lastOpened() {
        const all = openedUrls.all();
        return all.length > 0 ? all[all.length - 1] : "";
    }

    function cleanup() {
        Playback.stop();
        Playback.clearQueue();
        OnDevice.active = false;
        Devices.setTestDevices([]);
        MiniDisc.clearQueue();
        MiniDisc.recordMode = 0;
        MiniDisc.recordView = false;
        MiniDisc.tracks.clearMarks();
        MiniDisc.setTestDevices([]);
        fakeRecorder.reset();
    }

    function test_nowPlaying() {
        const win = openMain(Main.LibraryScreen);
        const menu = win.contextMenu;
        const label = findChild(win.footer, "nowPlayingLabel");
        rightClick(label);
        wait(100);
        verify(!menu.opened); // nothing playing: no menu

        const grid = win.libraryView.grid;
        let kidA = 0;
        tryVerify(() => {
            for (let i = 0; i < grid.count; ++i) {
                const t = grid.itemAtIndex(i);
                if (t && t.title === "Kid A")
                    kidA = t.albumId;
            }
            return kidA > 0;
        });
        Playback.playItem("album", kidA);
        tryCompare(Playback, "hasTrack", true);
        compare(Playback.currentAlbumId, kidA);
        verify(Playback.currentPath.endsWith("/Radiohead/Kid A/01.flac"));
        rightClick(label);
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Go to album", "Add to playlist", "Send to device", "Find cover…", "Edit tags…",
                                  "Open containing folder"]);
        pick(menu, "Open containing folder");
        tryVerify(() => lastOpened().startsWith("show:file://") && lastOpened().endsWith("/Kid%20A/01.flac"), 2000, lastOpened());
        rightClick(label);
        pick(menu, "Go to album");
        tryCompare(win.libraryView, "content", LibraryView.Album);
        compare(win.libraryView.albumId, kidA);
    }

    function test_deviceAndOnDevice() {
        Devices.setTestDevices([{ id: "uuid:T-CM", name: "CMSTICK", fsType: "vfat", mountPoint: fixtureDir,
                                  totalBytes: 16000000000, freeBytes: 8000000000 }]);
        const win = openMain(Main.ExportScreen);
        const menu = win.contextMenu;
        const view = win.exportView;
        const devices = findChild(view, "deviceList");
        tryVerify(() => devices.itemAtIndex(0) !== null);
        rightClick(devices.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Unmount", "Set up…", "Open in file manager", "Refresh"]);
        verify(!menuItem(menu, "Refresh").enabled); // the on-device view isn't showing
        pick(menu, "Open in file manager");
        tryVerify(() => lastOpened().startsWith("file://") && lastOpened().endsWith(fixtureDir.replace(/ /g, "%20")), 2000, lastOpened());

        // On the device: an artist folds; a track shows its file, an album opens its folder.
        keyClick("o");
        tryVerify(() => OnDevice.model.count > 0 && !OnDevice.browsing, 10000);
        const list = findChild(view, "onDeviceList");
        tryVerify(() => list.itemAtIndex(0) !== null);
        rightClick(list.itemAtIndex(0)); // an artist
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Collapse", "Open in file manager", "Refresh"]);
        const rows = OnDevice.model.count;
        pick(menu, "Collapse");
        tryVerify(() => OnDevice.model.count < rows);
        OnDevice.model.toggle(0); // open again
        tryCompare(OnDevice.model, "count", rows);
        let track = -1;
        for (let i = 0; i < OnDevice.model.count && track < 0; ++i) {
            const it = list.itemAtIndex(i);
            if (it && it.kind === 2)
                track = i;
        }
        verify(track > 0);
        rightClick(list.itemAtIndex(track));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Open in file manager", "Refresh"]); // a track doesn't fold
        pick(menu, "Open in file manager");
        tryVerify(() => lastOpened().startsWith("show:file://") && lastOpened().endsWith(".flac"), 2000, lastOpened());

        // `,` on the device list: the device's menu.
        view.focusedPane = ExportView.DeviceList;
        keyClick(",");
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu)[0], "Unmount");
        keyClick(Qt.Key_Escape);
        tryCompare(menu, "opened", false);
    }

    function test_miniDisc() {
        Devices.setTestDevices([]);
        MiniDisc.setTestDevices([{ id: "netmd:cm", name: "Sony MZ-NE410" }]);
        const win = openMain(Main.ExportScreen);
        const menu = win.contextMenu;
        const view = win.exportView;
        const dlg = win.confirmDialog;
        tryVerify(() => MiniDisc.hasDisc && !MiniDisc.busy, 5000);
        MiniDisc.recordView = false;
        compare(MiniDisc.trackCount, 3); // Intro, (untitled), Outro
        const list = findChild(view, "mdTrackList");
        tryVerify(() => list.itemAtIndex(2) !== null);

        // The first track: it can't go up.
        rightClick(list.itemAtIndex(0), 40, list.itemAtIndex(0).height - 10);
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Rename…", "Move up", "Move down", "Mark", "Delete…"]);
        verify(!menuItem(menu, "Move up").enabled);
        pick(menu, "Mark");
        tryCompare(MiniDisc.tracks, "markedCount", 1); // (entries run once the menu has gone)
        MiniDisc.tracks.toggleMark(2);
        compare(MiniDisc.tracks.markedCount, 2);

        // An unmarked track: Delete is for it alone, not the marked ones.
        rightClick(list.itemAtIndex(1), 40, list.itemAtIndex(1).height / 2);
        tryCompare(menu, "opened", true);
        verify(menuTexts(menu).includes("Clear marks (2)"));
        pick(menu, "Delete…");
        tryCompare(dlg, "opened", true);
        compare(dlg.title, "Delete track 2?");
        keyClick("n");
        tryCompare(dlg, "visible", false);
        // A marked one: all of them; rename / move are off.
        rightClick(list.itemAtIndex(2), 40, list.itemAtIndex(2).height / 2);
        tryCompare(menu, "opened", true);
        verify(menuTexts(menu).includes("Delete 2 tracks…"), menuTexts(menu));
        verify(!menuItem(menu, "Rename…").enabled);
        pick(menu, "Delete 2 tracks…");
        tryCompare(dlg, "opened", true);
        compare(dlg.title, "Delete 2 tracks?");
        keyClick("n");
        tryCompare(dlg, "visible", false);
        MiniDisc.tracks.clearMarks();

        // The disc: rename, the record mode (a choice), refresh, wipe.
        rightClick(findChild(view, "mdDiscTitle"));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Rename disc…", "Record in SP", "Record in LP2", "Record in LP4", "Refresh", "Wipe disc…"]);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/contextmenu-disc.png");
        }
        if (MiniDisc.lpAvailable) {
            pick(menu, "Record in LP2");
            tryCompare(MiniDisc, "recordMode", 1);
        } else {
            verify(!menuItem(menu, "Record in LP2").enabled);
            keyClick(Qt.Key_Escape);
            tryCompare(menu, "opened", false);
        }

        // The browse pane: onto the recorder first.
        const browse = findChild(view, "exportBrowse");
        const artists = findChild(browse, "artistList");
        tryVerify(() => artists.count > 0 && artists.itemAtIndex(0) !== null);
        rightClick(artists.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Add to Sony MZ-NE410", "Play", "Play next", "Add to queue"]);
        pick(menu, "Add to Sony MZ-NE410");
        tryVerify(() => MiniDisc.queue.count > 0);

        // The "to record" list: remove / clear.
        MiniDisc.recordView = true;
        const queueList = findChild(view, "mdQueueList");
        tryVerify(() => queueList.itemAtIndex(0) !== null);
        rightClick(queueList.itemAtIndex(0));
        tryCompare(menu, "opened", true);
        compare(menuTexts(menu), ["Remove", "Clear list"]);
        pick(menu, "Clear list");
        tryCompare(MiniDisc.queue, "count", 0);
    }
}
