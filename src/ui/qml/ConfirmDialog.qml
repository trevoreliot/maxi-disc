// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Asks before something that can't be undone (M12b: deleting MiniDisc tracks, wiping the disc).
// ask({title, message, details, confirmText, phrase, action}): `details` is a list of lines,
// `action` runs on confirm (the button, or the keys below: both go through Dialog.accept()). Without a `phrase`, y / Enter confirms and n / Esc cancels; with
// one, it must be typed first (any case) and Enter confirms. Main owns it; it owns its keys.
Dialog {
    id: root

    property string message
    property list<string> details: []
    property string confirmText: qsTr("Delete")
    property string phrase: ""
    property var action: null
    readonly property bool typed: phrase === "" || phraseField.text.trim().toUpperCase() === phrase.toUpperCase()

    function ask(options: var) {
        title = options.title ?? "";
        message = options.message ?? "";
        details = options.details ?? [];
        confirmText = options.confirmText ?? qsTr("Delete");
        phrase = options.phrase ?? "";
        action = options.action ?? null;
        phraseField.text = "";
        open();
    }

    function confirm() {
        if (typed)
            accept(); // Dialog's: emits accepted (below) and closes
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 480)
    padding: Theme.spaceLG

    onOpened: {
        if (phrase !== "")
            phraseField.forceActiveFocus();
        else
            body.forceActiveFocus();
    }
    onAccepted: {
        const run = action;
        action = null;
        if (run)
            run();
    }
    onRejected: action = null

    header: Label {
        text: root.title
        padding: Theme.spaceLG
        bottomPadding: 0
        font.pixelSize: Theme.fontTitle
        font.bold: true
    }

    footer: DialogButtonBox {
        Button {
            objectName: "confirmButton"
            text: root.confirmText
            enabled: root.typed
            palette.buttonText: Theme.errorText
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: Theme.spaceSM

        Keys.onPressed: event => {
            // Plain presses only: a held key or Ctrl+Y mustn't delete anything.
            if (root.phrase !== "" || event.isAutoRepeat || event.modifiers !== Qt.NoModifier)
                return;
            if (event.key === Qt.Key_Y || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.confirm();
                event.accepted = true;
            } else if (event.key === Qt.Key_N) {
                root.reject();
                event.accepted = true;
            }
        }

        Label {
            objectName: "confirmMessage"
            Layout.fillWidth: true
            text: root.message
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.details
            delegate: Label {
                required property string modelData
                Layout.fillWidth: true
                leftPadding: Theme.spaceMD
                text: modelData
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceSM
            text: root.phrase !== "" ? qsTr("Type %1 to confirm.").arg(root.phrase)
                                     : qsTr("y or Enter: %1 · n or Esc: cancel").arg(root.confirmText.toLowerCase())
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
        TextField {
            id: phraseField
            objectName: "confirmPhrase"
            Layout.fillWidth: true
            visible: root.phrase !== ""
            placeholderText: root.phrase
            onAccepted: root.confirm()
        }
    }
}
