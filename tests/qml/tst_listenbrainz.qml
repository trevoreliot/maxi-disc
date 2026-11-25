// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// ListenBrainz (M14) through the real shell: the transport bar's toggle, the dialog, connecting.
// ListenBrainz (tst_qml's Setup) talks to a stand-in server ("GOOD" is alice's token) with an
// in-memory keyring and never opens a browser (`openedUrls` lists what it would have opened).
TestCase {
    id: tc
    name: "ListenBrainz"
    when: windowShown

    Component {
        id: mainComp
        Main {}
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

    function cleanup() {
        if (ListenBrainz.state === "checking")
            ListenBrainz.cancelConnect();
        if (ListenBrainz.state !== "disconnected")
            ListenBrainz.disconnectAccount();
    }

    function test_connectFromTheToggle() {
        const win = openMain();
        const toggle = findChild(win.footer, "listenbrainzToggle");
        const dlg = win.listenBrainzDialog;
        verify(toggle);
        verify(!toggle.checked);
        compare(ListenBrainz.state, "disconnected");
        verify(toggle.toolTip.indexOf("not connected") >= 0);

        // Not connected: the toggle opens the dialog.
        clickToggle(toggle);
        tryCompare(dlg, "opened", true);
        verify(!toggle.checked);
        const token = findChild(dlg.contentItem, "listenbrainzTokenField");
        tryVerify(() => token.activeFocus);
        const connect = findChild(dlg.footer, "listenbrainzConnectButton");
        verify(!connect.enabled);

        // Typing goes to the field, not the app's keys (q would open the queue, space play).
        keyClick("q");
        keyClick(Qt.Key_Space);
        compare(token.text, "q ");
        verify(!win.queueOpen);

        // A token ListenBrainz doesn't know: said so, still not connected.
        token.text = "WRONG";
        verify(connect.enabled);
        mouseClick(connect);
        tryVerify(() => ListenBrainz.errorText.indexOf("doesn't know that token") >= 0);
        compare(ListenBrainz.state, "disconnected");
        verify(findChild(dlg.contentItem, "listenbrainzError").visible);

        token.text = "GOOD";
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/listenbrainz-setup.png");
        }
        keyClick(Qt.Key_Return); // Enter in the field connects too
        tryCompare(ListenBrainz, "state", "connected");
        compare(ListenBrainz.user, "alice");
        compare(findChild(dlg.contentItem, "listenbrainzUser").text, "Connected as alice");
        compare(token.text, ""); // not kept once it's done its job
        verify(ListenBrainz.enabled);
        verify(ListenBrainz.errorText === "");
        if (screenshotDir !== "") {
            wait(200);
            grabImage(win.contentItem.parent).save(screenshotDir + "/listenbrainz-connected.png");
        }
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);

        // Connected: the toggle switches submitting on and off.
        verify(toggle.checked);
        verify(toggle.toolTip.indexOf("submitting as alice") >= 0);
        clickToggle(toggle);
        verify(!ListenBrainz.enabled);
        verify(!toggle.checked);
        verify(toggle.toolTip.indexOf("off") >= 0);
        verify(!dlg.opened);
        clickToggle(toggle);
        verify(ListenBrainz.enabled);
        verify(toggle.checked);

        // The label fits beside Discord's at the window's size.
        const discord = findChild(win.footer, "discordToggle");
        const a = toggle.mapToItem(win.contentItem, 0, 0);
        const b = discord.mapToItem(win.contentItem, 0, 0);
        verify(a.x >= b.x + discord.width, "the toggles overlap");
        verify(a.x + toggle.width <= win.width, "the toggle runs off the window");
    }

    function test_theMenuOpensItAndTheLinkGoesToTheSettingsPage() {
        const win = openMain();
        const dlg = win.listenBrainzDialog;
        win.chrome.listenBrainzRequested();
        tryCompare(dlg, "opened", true);
        const before = openedUrls.all().length;
        ListenBrainz.openTokenPage();
        compare(openedUrls.all().length, before + 1);
        compare(openedUrls.all()[before], "https://listenbrainz.org/settings/");
        const token = findChild(dlg.contentItem, "listenbrainzTokenField");
        token.text = "GOOD";
        mouseClick(findChild(dlg.footer, "listenbrainzConnectButton"));
        tryCompare(ListenBrainz, "state", "connected");
        mouseClick(findChild(dlg.footer, "listenbrainzDisconnectButton"));
        compare(ListenBrainz.state, "disconnected");
        compare(ListenBrainz.user, "");
        verify(findChild(dlg.footer, "listenbrainzConnectButton").visible);
        keyClick(Qt.Key_Escape);
        tryCompare(dlg, "visible", false);
    }
}
