// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Renames the MiniDisc or one of its tracks (M10). MiniDiscs store plain ASCII, so the dialog
// shows what the title becomes on the disc as you type ("Björk" → "Bjork"). Enter saves, Esc
// cancels; the dialog owns its keys (a text field).
Dialog {
    id: root

    property bool disc: true
    property int trackIndex: -1
    readonly property var cleaned: MiniDisc.preview(titleField.text, root.disc)

    function renameDisc() {
        disc = true;
        trackIndex = -1;
        titleField.text = MiniDisc.discTitle;
        open();
    }

    function renameTrack(index: int) {
        disc = false;
        trackIndex = index;
        titleField.text = MiniDisc.tracks.titleAt(index);
        open();
    }

    function save() {
        if (MiniDisc.busy)
            return;
        if (disc)
            MiniDisc.renameDisc(titleField.text);
        else
            MiniDisc.renameTrack(trackIndex, titleField.text);
        close();
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 520)
    padding: Theme.spaceLG
    title: disc ? qsTr("Rename the disc") : qsTr("Rename track %1").arg(trackIndex + 1)

    onOpened: {
        titleField.forceActiveFocus();
        titleField.selectAll();
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
            objectName: "saveTitleButton"
            text: qsTr("Save")
            enabled: !MiniDisc.busy
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onAccepted: root.save()
        onRejected: root.close()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceSM

        TextField {
            id: titleField
            objectName: "mdTitleField"
            Layout.fillWidth: true
            placeholderText: root.disc ? qsTr("Disc title (empty: untitled)") : qsTr("Track title (empty: untitled)")
            onAccepted: root.save()
        }
        Label {
            objectName: "mdTitlePreview"
            Layout.fillWidth: true
            visible: root.cleaned.changed || root.cleaned.truncated
            text: qsTr("On the disc: %1").arg(root.cleaned.text === "" ? qsTr("(untitled)") : root.cleaned.text)
                  + (root.cleaned.truncated ? qsTr(" (shortened)") : "")
            wrapMode: Text.Wrap
            color: Theme.textSecondary
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("MiniDiscs store plain letters, digits and punctuation: accents and other characters are replaced.")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
    }
}
