// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Export screen devices (M8) through the real shell: the device list, keyboard between the
// device and browse panes, and setting up a profile in the dialog. Devices come from
// Devices.setTestDevices (tests never look at the machine's real drives).
TestCase {
    id: tc
    name: "Export"
    when: windowShown

    Component {
        id: mainComp
        Main {}
    }

    function testDevices() {
        return [
            { id: "uuid:T-STICK", name: "THUMB", fsType: "vfat", totalBytes: 16000000000 },
            { id: "uuid:T-DAP", name: "DAP", vendor: "FiiO", model: "M3K", fsType: "exfat",
              mountPoint: "/run/media/me/DAP", totalBytes: 64000000000, freeBytes: 20000000000 }
        ];
    }

    function cleanup() {
        Devices.setTestDevices([]);
    }

    function openExport() {
        const win = createTemporaryObject(mainComp, tc);
        // The test's own window may still be activating: insist until the app window has
        // keyboard focus (real key presses go to the focus window).
        tryVerify(() => {
            win.requestActivate();
            win.contentItem.forceActiveFocus();
            return win.active && win.activeFocusItem !== null;
        }, 5000, "app window never got keyboard focus");
        win.showScreen(Main.ExportScreen);
        return win;
    }

    function test_noDevices() {
        Devices.setTestDevices([]);
        const win = openExport();
        verify(findChild(win.exportView, "noDevicesLabel").visible);
        compare(win.exportView.pane, ExportView.Browse); // nothing to focus in the device pane
        keyClick(Qt.Key_Return); // no device: Enter goes to the browse pane, no dialog
        verify(!win.profileEditorDialog.opened);
        keyClick("m"); // nothing to mount: harmless
    }

    function test_keyboardSetUpProfile() {
        Devices.setTestDevices(testDevices());
        const win = openExport();
        const view = win.exportView;
        compare(Devices.count, 2);
        compare(Devices.currentIndex, 0);
        compare(view.pane, ExportView.DeviceList);
        verify(!findChild(view, "noDevicesLabel").visible);
        compare(findChild(view, "selectedDeviceLabel").text, "THUMB");
        compare(findChild(view, "editProfileButton").text, "Set up…");

        // j / k through the devices; past the last into the browse pane and back.
        keyClick("j");
        compare(Devices.currentIndex, 1);
        compare(findChild(view, "selectedDeviceLabel").text, "DAP");
        keyClick("j");
        compare(view.pane, ExportView.Browse);
        keyClick("k"); // the browse list is at its top: back up to the devices
        compare(view.pane, ExportView.DeviceList);
        keyClick("k");
        compare(Devices.currentIndex, 0);

        // Enter: set up THUMB. The dialog owns the keys (typing goes into the fields).
        keyClick(Qt.Key_Return);
        const dlg = win.profileEditorDialog;
        tryCompare(dlg, "opened", true);
        const ed = dlg.profileEditor;
        compare(ed.deviceId, "uuid:T-STICK");
        verify(ed.isNew);
        const nameField = findChild(dlg.contentItem, "profileNameField");
        tryVerify(() => nameField.activeFocus);
        compare(nameField.text, "THUMB");
        nameField.selectAll();
        keyClick("C");
        keyClick("a");
        keyClick("r");
        compare(ed.name, "Car");
        compare(findChild(dlg.contentItem, "examplePath").text, "Music/AC_DC/Back in Black/2-04 Back in Black.flac");
        mouseClick(findChild(dlg.contentItem, "presetDapButton"));
        compare(ed.presetId, "generic-dap");
        compare(findChild(dlg.contentItem, "examplePath").text, "Music/AC_DC/Back in Black/2-04 Back in Black.mp3");
        verify(findChild(dlg.contentItem, "codec_MP3").checked);
        verify(!findChild(dlg.contentItem, "codec_FLAC").checked);
        nameField.forceActiveFocus();
        keyClick(Qt.Key_Return); // Enter in a field saves
        tryCompare(dlg, "visible", false);
        compare(Devices.current.profileName, "Car");
        compare(findChild(view, "editProfileButton").text, "Edit profile…");
        verify(findChild(view, "profileSummary").text.indexOf("Car") >= 0);

        // Esc cancels: nothing changes.
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "opened", true);
        verify(!ed.isNew);
        compare(ed.name, "Car");
        const layout = findChild(dlg.contentItem, "layoutField");
        layout.forceActiveFocus();
        layout.selectAll();
        keyClick("x"); // an invalid layout: no {title}/{track}
        verify(ed.layoutError !== "");
        verify(!findChild(dlg.footer, "saveProfileButton").enabled);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
        compare(Devices.current.profileName, "Car");

        // Remove the profile again (the fixture database is shared by all QML tests).
        dlg.edit(Devices.current.deviceId);
        tryCompare(dlg, "opened", true);
        mouseClick(findChild(dlg.footer, "removeProfileButton"));
        tryCompare(dlg, "visible", false);
        compare(Devices.current.hasProfile, false);
    }

    // M9: choosing what goes on a device and planning a sync, by keyboard.
    function test_keyboardSyncSelection() {
        // The fixture folder stands in for the mounted stick (only planned here: the C++
        // tests run real syncs).
        Devices.setTestDevices([{ id: "uuid:T-SYNC", name: "SYNCSTICK", fsType: "vfat", mountPoint: fixtureDir,
                                  totalBytes: 16000000000, freeBytes: 8000000000 }]);
        const win = openExport();
        const view = win.exportView;
        const ed = win.profileEditorDialog.profileEditor;
        verify(ed.load("uuid:T-SYNC"));
        verify(ed.save());
        tryCompare(Sync, "canPlan", true);
        compare(Sync.deviceId, "uuid:T-SYNC");
        verify(findChild(view, "selectionHint").visible);

        keyClick("j"); // past the only device: the browse list
        compare(view.pane, ExportView.Browse);
        view.browse.setMode(BrowsePane.Albums);
        keyClick("j"); // the first album
        verify(view.browse.currentItem() !== null);
        compare(view.browse.currentItem().kind, "album");
        keyClick("i");
        tryCompare(Sync.selection, "count", 1);
        tryVerify(() => Sync.tracks.count > 0);
        tryVerify(() => win.toast.text.startsWith("Added ") && win.toast.text.endsWith(" to SYNCSTICK")); // M11b
        verify(findChild(view, "exportCount").visible);
        verify(!findChild(view, "selectionHint").visible);
        keyClick("i"); // the same album again: still one entry
        tryCompare(win.toast, "text", "Already on SYNCSTICK's list" + (Sync.tracks.count > 1 ? " (" + Sync.tracks.count + " tracks)" : ""));
        compare(Sync.selection.count, 1);
        keyClick("j");
        keyClick("i");
        tryCompare(Sync.selection, "count", 2);

        // l: into the selection; x removes the entry under the cursor; h: back.
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        keyClick("j");
        compare(view.syncView.selectionIndex, 1);
        keyClick("x");
        compare(Sync.selection.count, 1);
        keyClick("h");
        compare(view.pane, ExportView.Browse);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-selection.png");
        }

        // p plans; Esc closes the plan (and stays on the Export screen).
        keyClick("p");
        tryCompare(Sync, "state", Sync.Ready, 10000);
        compare(findChild(view, "exportTitle").text, "Sync plan");
        verify(findChild(view, "planSummary").text !== "");
        compare(findChild(view, "syncButton").enabled, Sync.canSync);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-plan.png");
        }
        keyClick(Qt.Key_Escape);
        compare(Sync.state, Sync.Idle);
        compare(win.currentScreen, Main.ExportScreen);
        compare(findChild(view, "exportTitle").text, "Tracks to Export");

        // Clean up the shared fixture database.
        Sync.removeAt(0);
        compare(Sync.selection.count, 0);
        verify(ed.remove());
    }

    // M9b: the right pane's "On Device" view, by keyboard.
    function test_onDeviceView() {
        Devices.setTestDevices([{ id: "uuid:T-OD", name: "ODSTICK", fsType: "vfat" }]); // not mounted
        const win = openExport();
        const view = win.exportView;
        compare(OnDevice.active, false);
        verify(findChild(view, "syncPane").visible);
        keyClick("o");
        compare(OnDevice.active, true);
        tryVerify(() => findChild(view, "onDevicePane").visible);
        verify(!findChild(view, "syncPane").visible);
        verify(findChild(view, "onDeviceMountButton").visible);
        compare(findChild(view, "onDeviceUnmounted").text, "ODSTICK isn't mounted.");

        // Mounted (the fixture folder stands in for it): its music shows.
        Devices.setTestDevices([{ id: "uuid:T-OD", name: "ODSTICK", fsType: "vfat", mountPoint: fixtureDir,
                                  totalBytes: 16000000000, freeBytes: 8000000000 }]);
        tryVerify(() => OnDevice.model.trackCount > 0 && !OnDevice.browsing, 10000);
        verify(!findChild(view, "onDeviceMountButton").visible);
        verify(findChild(view, "onDeviceSummary").text.indexOf(" track") > 0);
        compare(OnDevice.model.currentIndex, 0);

        // o put the keys in the list (once it had rows): j/k move, Enter folds the artist; h goes
        // back to where o was pressed (the device list), l in again.
        compare(view.pane, ExportView.Selection);
        keyClick("h");
        compare(view.pane, ExportView.DeviceList);
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        const rows = OnDevice.model.count;
        keyClick(Qt.Key_Return); // row 0 is an artist
        verify(OnDevice.model.count < rows);
        keyClick(Qt.Key_Return);
        compare(OnDevice.model.count, rows);
        keyClick("j");
        compare(OnDevice.model.currentIndex, 1);
        keyClick("k");
        compare(OnDevice.model.currentIndex, 0);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-on-device.png");
        }

        // o: back to the export view.
        keyClick("o");
        compare(OnDevice.active, false);
        tryVerify(() => findChild(view, "syncPane").visible);
    }

    // M10: a NetMD recorder's disc, read and retitled by keyboard (the in-memory recorder).
    function test_miniDisc() {
        Devices.setTestDevices([]);
        MiniDisc.setTestDevices([{ id: "netmd:qml", name: "Sony MZ-NE410" }]);
        const win = openExport();
        const view = win.exportView;
        compare(Devices.count, 1);
        compare(Devices.currentIndex, 0);
        verify(view.mdSelected);
        tryVerify(() => MiniDisc.hasDisc, 5000);
        verify(findChild(view, "miniDiscPane").visible);
        compare(findChild(view, "mdTitle").text, "MiniDisc · Sony MZ-NE410");
        compare(findChild(view, "mdDiscTitle").text, "Scratch");
        verify(findChild(view, "mdRenameButton").enabled);
        verify(!findChild(view, "onDeviceTab").visible);

        // l from the device list: into the track list; h: back to the devices. Enter on the
        // recorder goes to its tracks too (it has no profile).
        compare(view.pane, ExportView.DeviceList);
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        keyClick("h");
        compare(view.pane, ExportView.DeviceList);
        keyClick(Qt.Key_Return);
        compare(view.pane, ExportView.Selection);
        verify(!win.profileEditorDialog.opened);
        // From the browse list, h goes back there.
        keyClick("h");
        keyClick("j");
        compare(view.pane, ExportView.Browse);
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        keyClick("h");
        compare(view.pane, ExportView.Browse);
        keyClick("l");
        // j moves; n renames the track, the dialog shows the clean title.
        keyClick("j");
        compare(MiniDisc.tracks.currentIndex, 1);
        keyClick("n");
        const dlg = win.renameMdDialog;
        tryCompare(dlg, "opened", true);
        verify(!dlg.disc);
        compare(dlg.trackIndex, 1);
        const field = findChild(dlg.contentItem, "mdTitleField");
        tryVerify(() => field.activeFocus);
        field.text = "Sigur Rós";
        verify(findChild(dlg.contentItem, "mdTitlePreview").visible);
        compare(findChild(dlg.contentItem, "mdTitlePreview").text, "On the disc: Sigur Ros");
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "visible", false);
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.tracks.titleAt(1), "Sigur Ros");
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-minidisc.png");
        }

        // N renames the disc; Esc cancels.
        keyClick("N");
        tryCompare(dlg, "opened", true);
        verify(dlg.disc);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
        compare(MiniDisc.discTitle, "Scratch");

        // Put the track back (the fake is shared).
        MiniDisc.renameTrack(1, "");
        tryCompare(MiniDisc, "busy", false);
        MiniDisc.setTestDevices([]);
        compare(Devices.count, 0);
    }

    // M11: queueing an album for the MiniDisc and recording it, by keyboard (in-memory recorder,
    // fake decoder).
    function test_miniDiscRecord() {
        Devices.setTestDevices([]);
        MiniDisc.setTestDevices([{ id: "netmd:rec", name: "Sony MZ-NE410" }]);
        const win = openExport();
        const view = win.exportView;
        tryVerify(() => MiniDisc.hasDisc, 5000);
        MiniDisc.clearQueue();
        MiniDisc.recordView = false;
        const before = MiniDisc.trackCount;

        // o: the record view (empty: a hint), o again: the disc.
        keyClick("o");
        verify(MiniDisc.recordView);
        verify(findChild(view, "mdQueueHint").visible);
        keyClick("o");
        verify(!MiniDisc.recordView);

        // Albums in the browse pane: Kid A is the 4th; i queues its two tracks.
        keyClick("j"); // the browse list (the recorder is the only device)
        compare(view.pane, ExportView.Browse);
        view.browse.setMode(BrowsePane.Albums);
        for (let i = 0; i < 4; ++i)
            keyClick("j");
        compare(view.browse.currentItem().name.indexOf("Kid A"), 0);
        keyClick("i");
        tryCompare(MiniDisc.queue, "count", 2);
        tryCompare(win.toast, "text", "Added 2 tracks to Sony MZ-NE410");
        verify(MiniDisc.recordView);
        verify(findChild(view, "mdPlanError").visible); // 02.flac isn't in the fixture
        verify(!findChild(view, "mdRecordButton").enabled);

        // M12: M cycles SP → LP2 → LP4 → SP; the buttons and the free time follow.
        compare(MiniDisc.recordMode, 0);
        verify(findChild(view, "mdModeSP").checked);
        keyClick("M");
        compare(MiniDisc.recordMode, 1);
        verify(findChild(view, "mdModeLP2").checked);
        verify(!findChild(view, "mdModeSP").checked);
        compare(MiniDisc.plan.modeName, "LP2");
        keyClick("M");
        compare(MiniDisc.recordModeName, "LP4");
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-minidisc-lp4.png");
        }
        keyClick("M");
        compare(MiniDisc.recordMode, 0);

        // l into the queue, j to the missing one, x removes it.
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        keyClick("j");
        compare(MiniDisc.queue.currentIndex, 1);
        keyClick("x");
        compare(MiniDisc.queue.count, 1);
        verify(findChild(view, "mdRecordButton").enabled);
        verify(findChild(view, "mdQueuedPart").visible);

        // S records.
        keyClick("S");
        tryCompare(MiniDisc, "recording", false, 10000);
        compare(findChild(view, "mdRecordResult").text, "Recorded 1 track.");
        compare(MiniDisc.trackCount, before + 1);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem).save(screenshotDir + "/export-minidisc-record.png");
        }

        // X clears the list.
        keyClick("X");
        compare(MiniDisc.queue.count, 0);
        MiniDisc.recordView = false;
        MiniDisc.setTestDevices([]);
    }

    // M12b: editing the disc by keyboard: J / K move, v marks, d deletes (asks), D wipes (typed).
    function test_miniDiscEdit() {
        fakeRecorder.reset(); // the shared recorder: its starting disc
        Devices.setTestDevices([]);
        MiniDisc.setTestDevices([{ id: "netmd:edit", name: "Sony MZ-NE410" }]);
        const win = openExport();
        const view = win.exportView;
        const dlg = win.confirmDialog;
        tryVerify(() => MiniDisc.hasDisc && !MiniDisc.busy, 5000);
        compare(MiniDisc.trackCount, 3); // Intro, (untitled), Outro; "Side A" = 1-2
        MiniDisc.recordView = false;
        verify(MiniDisc.canEdit);
        keyClick("l");
        compare(view.pane, ExportView.Selection);
        compare(MiniDisc.tracks.currentIndex, 0);

        // J: Intro down a place, inside its group; again: out of the group. The cursor follows.
        keyClick("J");
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.tracks.titleAt(1), "Intro");
        compare(MiniDisc.tracks.currentIndex, 1);
        keyClick("J");
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.tracks.titleAt(2), "Intro");
        compare(MiniDisc.tracks.currentIndex, 2);
        compare(MiniDisc.groupCount, 1);
        keyClick("J"); // the last place already: nothing
        compare(MiniDisc.busy, false);
        keyClick("K");
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.tracks.titleAt(1), "Intro");
        compare(MiniDisc.tracks.currentIndex, 1);

        // d asks; n keeps the track; so does Esc; so does a change of disc under the question.
        keyClick("d");
        tryCompare(dlg, "opened", true);
        compare(dlg.title, "Delete track 2?");
        compare(dlg.details.length, 1);
        keyClick("n");
        tryCompare(dlg, "visible", false);
        keyClick("d");
        tryCompare(dlg, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
        compare(win.currentScreen, Main.ExportScreen);
        keyClick("d");
        tryCompare(dlg, "opened", true);
        MiniDisc.refresh(); // the disc is read again (as after a swap)
        tryCompare(dlg, "visible", false);
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.trackCount, 3);

        // v marks two; Esc clears the marks; marked again, d + Enter deletes both.
        keyClick("v");
        keyClick("k");
        keyClick("v");
        compare(MiniDisc.tracks.markedCount, 2);
        compare(findChild(view, "mdDeleteButton").text, "Delete 2 (d)");
        keyClick(Qt.Key_Escape);
        compare(MiniDisc.tracks.markedCount, 0);
        compare(win.currentScreen, Main.ExportScreen); // Esc cleared the marks, nothing else
        keyClick("v");
        keyClick("j");
        keyClick("v");
        keyClick("d");
        tryCompare(dlg, "opened", true);
        compare(dlg.title, "Delete 2 tracks?");
        compare(dlg.details.length, 2);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/export-minidisc-delete.png");
        }
        mouseClick(findChild(dlg.footer, "confirmButton")); // the button, like Enter
        tryCompare(dlg, "visible", false);
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.trackCount, 1);
        compare(MiniDisc.tracks.titleAt(0), "Outro");
        compare(MiniDisc.tracks.markedCount, 0);
        compare(MiniDisc.tracks.currentIndex, 0);

        // D asks for WIPE: Enter does nothing until it's typed (any case).
        keyClick("D");
        tryCompare(dlg, "opened", true);
        compare(dlg.phrase, "WIPE");
        const field = findChild(dlg.contentItem, "confirmPhrase");
        tryVerify(() => field.activeFocus);
        verify(!findChild(dlg.footer, "confirmButton").enabled);
        keyClick(Qt.Key_Return);
        verify(dlg.opened);
        keyClick("y"); // typed into the field, not a "yes"
        verify(dlg.opened);
        field.text = "";
        for (const c of "wipe")
            keyClick(c);
        verify(findChild(dlg.footer, "confirmButton").enabled);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/export-minidisc-wipe.png");
        }
        keyClick(Qt.Key_Return);
        tryCompare(dlg, "visible", false);
        tryCompare(MiniDisc, "busy", false);
        compare(MiniDisc.trackCount, 0);
        compare(MiniDisc.discTitle, "");
        verify(MiniDisc.hasDisc);

        fakeRecorder.reset();
        MiniDisc.setTestDevices([]);
    }

    function test_screenshot() {
        if (screenshotDir === "")
            skip("set MMM_SCREENSHOT_DIR to save screenshots");
        Devices.setTestDevices(testDevices());
        const win = openExport();
        wait(200);
        grabImage(win.contentItem).save(screenshotDir + "/export-devices.png");
        win.profileEditorDialog.edit(Devices.current.deviceId);
        tryCompare(win.profileEditorDialog, "opened", true);
        wait(200);
        grabImage(win.contentItem.parent).save(screenshotDir + "/profile-editor.png");
        win.profileEditorDialog.close();
    }
}
