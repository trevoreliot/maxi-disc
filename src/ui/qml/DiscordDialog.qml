// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Discord status (M15): the Discord application id (the user's own: its name is what Discord
// shows), on / off, and how the connection to the Discord app is doing. Main owns it; it owns
// its keys (a text field). Esc closes it.
Dialog {
    id: root

    readonly property string status: Discord.state

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 520)
    padding: Theme.spaceLG
    title: qsTr("Discord")

    function save() {
        if (idField.text.trim() !== Discord.appId)
            Discord.setAppId(idField.text);
    }

    onOpened: {
        idField.text = Discord.appId;
        idField.forceActiveFocus();
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
            objectName: "discordSaveButton"
            enabled: idField.text.trim() !== Discord.appId
            text: Discord.hasAppId ? qsTr("Save") : qsTr("Connect")
            highlighted: true
            onClicked: root.save()
        }
        Button {
            text: qsTr("Close")
            onClicked: root.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceSM

        Label {
            Layout.fillWidth: true
            text: qsTr("Shows what you play on your Discord profile (“Listening to …”) while the Discord app "
                       + "runs on this computer. Create an application (free) on the "
                       + "<a href=\"portal\">Discord Developer Portal</a>: its name is what Discord shows "
                       + "(e.g. Music MD Manager). Then paste its Application ID here.")
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            onLinkActivated: Discord.openDeveloperPortal()
        }
        Label {
            text: qsTr("Application ID")
            font.pixelSize: Theme.fontSmall
        }
        TextField {
            id: idField
            objectName: "discordAppIdField"
            Layout.fillWidth: true
            font.family: Theme.fontMono
            inputMethodHints: Qt.ImhDigitsOnly
            placeholderText: qsTr("e.g. 1234567890123456789")
            onAccepted: root.save()
        }
        Label {
            objectName: "discordIdHint"
            Layout.fillWidth: true
            visible: idField.text.trim() !== "" && !/^[0-9]+$/.test(idField.text.trim())
            text: qsTr("An application ID is usually all digits.")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
        }
        Switch {
            objectName: "discordEnabledSwitch"
            visible: Discord.hasAppId
            text: qsTr("Show what I play")
            checked: Discord.enabled
            onToggled: Discord.enabled = checked
        }
        Label {
            objectName: "discordStatus"
            Layout.fillWidth: true
            visible: Discord.statusText !== ""
            text: Discord.statusText
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        Label {
            objectName: "discordError"
            Layout.fillWidth: true
            visible: Discord.errorText !== ""
            text: Discord.errorText
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
    }
}
