// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Small modal dialog asking for one line of text (playlist names).
Dialog {
    id: root

    property alias text: field.text
    property alias placeholderText: field.placeholderText

    signal submitted(string text)

    modal: true
    anchors.centerIn: Overlay.overlay
    width: 360
    standardButtons: Dialog.Ok | Dialog.Cancel

    onOpened: {
        field.selectAll();
        field.forceActiveFocus();
    }
    onAccepted: if (field.text.trim() !== "") root.submitted(field.text.trim())

    ColumnLayout {
        anchors.fill: parent

        TextField {
            id: field
            Layout.fillWidth: true
            onAccepted: root.accept()
        }
    }
}
