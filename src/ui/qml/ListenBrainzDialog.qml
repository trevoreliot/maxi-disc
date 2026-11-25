// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ListenBrainz (M14): connect an account (paste the user token from the ListenBrainz settings
// page), switch submitting on / off, see what's waiting to be sent. Main owns it; it owns its
// keys (a text field). Esc closes it.
Dialog {
    id: root

    readonly property string account: ListenBrainz.state
    readonly property bool setup: account !== "connected"

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(500, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 500)
    padding: Theme.spaceLG
    title: qsTr("ListenBrainz")

    function tryConnect() {
        if (tokenField.text.trim() !== "")
            ListenBrainz.connectAccount(tokenField.text);
    }

    // The token isn't kept in the field once it's done its job.
    onOpened: {
        tokenField.text = "";
        if (setup)
            tokenField.forceActiveFocus();
        else
            body.forceActiveFocus();
    }
    onClosed: tokenField.text = ""
    onAccountChanged: {
        if (account === "connected") {
            tokenField.text = "";
            body.forceActiveFocus();
        } else if (opened && account !== "checking") {
            tokenField.forceActiveFocus(); // back from a check (disabled meanwhile): try another
        }
    }

    header: Label {
        text: root.title
        padding: Theme.spaceLG
        bottomPadding: 0
        font.pixelSize: Theme.fontTitle
        font.bold: true
    }

    footer: DialogButtonBox {
        Button {
            objectName: "listenbrainzConnectButton"
            visible: root.setup
            enabled: root.account !== "checking" && tokenField.text.trim() !== ""
            text: root.account === "checking" ? qsTr("Checking…") : qsTr("Connect")
            highlighted: true
            onClicked: root.tryConnect()
        }
        Button {
            objectName: "listenbrainzDisconnectButton"
            visible: root.account === "connected"
            text: qsTr("Disconnect")
            onClicked: ListenBrainz.disconnectAccount()
        }
        Button {
            objectName: "listenbrainzCloseButton"
            text: root.account === "checking" ? qsTr("Cancel") : qsTr("Close")
            onClicked: {
                if (root.account === "checking")
                    ListenBrainz.cancelConnect();
                else
                    root.close();
            }
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Theme.spaceSM

        // --- Not connected (or the token stopped working) ---
        Label {
            objectName: "listenbrainzExpired"
            Layout.fillWidth: true
            visible: root.account === "expired"
            text: qsTr("ListenBrainz no longer accepts the saved token. Paste your current one to send what's waiting.")
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
        Label {
            Layout.fillWidth: true
            visible: root.setup
            text: qsTr("Paste your ListenBrainz user token. It's on your "
                       + "<a href=\"settings\">ListenBrainz settings page</a> "
                       + "(sign in, or create a free account, first). ") + ListenBrainz.secretStoreHint
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            onLinkActivated: ListenBrainz.openTokenPage()
        }
        Label {
            visible: root.setup
            text: qsTr("User token")
            font.pixelSize: Theme.fontSmall
        }
        TextField {
            id: tokenField
            objectName: "listenbrainzTokenField"
            Layout.fillWidth: true
            visible: root.setup
            enabled: root.account !== "checking"
            echoMode: TextInput.Password
            font.family: Theme.fontMono
            onAccepted: root.tryConnect()
        }

        // --- Connected ---
        Label {
            objectName: "listenbrainzUser"
            Layout.fillWidth: true
            visible: root.account === "connected"
            text: qsTr("Connected as %1").arg(ListenBrainz.user)
            font.bold: true
        }
        Switch {
            objectName: "listenbrainzEnabledSwitch"
            visible: root.account === "connected"
            text: qsTr("Submit what I play")
            checked: ListenBrainz.enabled
            onToggled: ListenBrainz.enabled = checked
        }
        Label {
            objectName: "listenbrainzLastListen"
            Layout.fillWidth: true
            visible: root.account === "connected" && ListenBrainz.lastListen !== ""
            text: qsTr("Last listen: %1").arg(ListenBrainz.lastListen)
            elide: Text.ElideRight
            color: Theme.textSecondary
        }

        // --- Always: what's waiting, what went wrong ---
        RowLayout {
            Layout.fillWidth: true
            visible: ListenBrainz.statusText !== ""
            Label {
                objectName: "listenbrainzStatus"
                Layout.fillWidth: true
                text: ListenBrainz.statusText
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }
            Button {
                objectName: "listenbrainzRetryButton"
                visible: root.account === "connected" && ListenBrainz.enabled && ListenBrainz.queued > 0
                text: qsTr("Retry now")
                onClicked: ListenBrainz.retryNow()
            }
        }
        Label {
            objectName: "listenbrainzError"
            Layout.fillWidth: true
            visible: ListenBrainz.errorText !== "" && root.account !== "expired"
            text: ListenBrainz.errorText
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
    }
}
