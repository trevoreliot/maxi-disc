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
        compare(Sync.selection.count, 1);
        tryVerify(() => Sync.tracks.count > 0);
        verify(findChild(view, "exportCount").visible);
        verify(!findChild(view, "selectionHint").visible);
        keyClick("i"); // the same album again: still one entry
        compare(Sync.selection.count, 1);
        keyClick("j");
        keyClick("i");
        compare(Sync.selection.count, 2);

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
