// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// Discord status (M15) through the real shell: the toggle, the dialog, and what the stand-in
// Discord app (tst_qml's Setup; it knows application id 123456789012345678) is told while a
// track plays. The real Discord is never touched; no browser opens.
TestCase {
    id: tc
    name: "Discord"
    when: windowShown

    readonly property string knownId: "123456789012345678"

    Component {
        id: mainComp
        Main {}
    }
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

    function openMain() {
        const win = createTemporaryObject(mainComp, tc);
        tryVerify(() => {
            win.requestActivate();
            win.contentItem.forceActiveFocus();
            return win.active && win.activeFocusItem !== null;
        }, 5000, "app window never got keyboard focus");
        return win;
    }

    function clickToggle(toggle) {
        mouseClick(toggle, toggle.width / 2, toggle.height - 8); // the rocker, under the label
    }

    function lastActivity() {
        const all = fakeDiscord.activities();
        return all.length > 0 ? all[all.length - 1] : null;
    }

    function cleanup() {
        Playback.stop();
        Playback.clearQueue();
        Discord.enabled = false;
        Discord.setAppId("");
        fakeDiscord.clear();
    }

    function test_setUpFromTheToggleThenPlay() {
        const win = openMain();
        const toggle = findChild(win.footer, "discordToggle");
        const dlg = win.discordDialog;
        verify(toggle.interactive);
        verify(!toggle.checked);
        compare(Discord.state, "off");
        verify(toggle.toolTip.indexOf("not set up") >= 0);

        // Not set up: the toggle opens the dialog.
        clickToggle(toggle);
        tryCompare(dlg, "opened", true);
        const field = findChild(dlg.contentItem, "discordAppIdField");
        tryVerify(() => field.activeFocus);
        const save = findChild(dlg.footer, "discordSaveButton");
        verify(!save.enabled);

        // The link goes to the Developer Portal (recorded, not opened).
        const before = openedUrls.all().length;
        Discord.openDeveloperPortal();
        compare(openedUrls.all()[before], "https://discord.com/developers/applications");

        // Typing goes to the field, not the app's keys (q would open the queue).
        keyClick("q");
        compare(field.text, "q");
        verify(!win.queueOpen);
        verify(findChild(dlg.contentItem, "discordIdHint").visible); // not digits
        field.text = tc.knownId;
        verify(!findChild(dlg.contentItem, "discordIdHint").visible);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/discord-setup.png");
        }
        keyClick(Qt.Key_Return); // Enter saves too
        compare(Discord.appId, tc.knownId);
        verify(Discord.enabled); // a first id switches it on
        tryCompare(Discord, "state", "connected");
        verify(toggle.checked);
        verify(toggle.toolTip.indexOf("showing what you play") >= 0);
        verify(findChild(dlg.contentItem, "discordEnabledSwitch").visible);
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/discord-connected.png");
        }
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);

        // Play Kid A's first track (the fixture's one real audio file): Discord is told.
        const grid = createTemporaryObject(gridComp, tc);
        tryCompare(grid, "count", 5);
        let kidA = null;
        for (let i = 0; i < grid.count; ++i) {
            const tile = grid.itemAtIndex(i);
            if (tile && tile.title === "Kid A")
                kidA = tile;
        }
        verify(kidA);
        const page = createTemporaryObject(pageComp, tc, { albumId: kidA.albumId });
        tryCompare(page, "trackCount", 2);
        Playback.playFromModel(page.tracksModel, 0);
        tryCompare(Playback, "playing", true);
        tryVerify(() => lastActivity() !== null && lastActivity().activity !== undefined, 5000);
        const shown = lastActivity().activity;
        compare(shown.type, 2);
        compare(shown.details, Playback.title + " - Radiohead"); // also the one-line status
        compare(shown.status_display_type, 2);
        compare(shown.state, "Kid A");
        verify(shown.timestamps.start > 0);

        // Paused: cleared.
        Playback.playPause();
        tryVerify(() => lastActivity().activity === undefined, 5000);

        // Off: Discord's told to clear and the connection goes.
        clickToggle(toggle);
        verify(!Discord.enabled);
        verify(!toggle.checked);
        compare(Discord.state, "off");
        verify(!dlg.opened);
        clickToggle(toggle); // back on: connects again
        tryCompare(Discord, "state", "connected");
    }

    function test_aWrongIdSaysSo() {
        const win = openMain();
        win.chrome.discordRequested();
        const dlg = win.discordDialog;
        tryCompare(dlg, "opened", true);
        findChild(dlg.contentItem, "discordAppIdField").text = "999";
        mouseClick(findChild(dlg.footer, "discordSaveButton"));
        tryCompare(Discord, "state", "error");
        verify(Discord.errorText.indexOf("Invalid Client ID") >= 0);
        verify(findChild(dlg.contentItem, "discordError").visible);
        const connections = fakeDiscord.connections();
        wait(300);
        compare(fakeDiscord.connections(), connections); // not retried
        // The right id: connected.
        findChild(dlg.contentItem, "discordAppIdField").text = tc.knownId;
        mouseClick(findChild(dlg.footer, "discordSaveButton"));
        tryCompare(Discord, "state", "connected");
        compare(Discord.errorText, "");
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
    }
}
