// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// "Send to <device>" (M11b): enabled while SendToDevice can send; when it can't, the
// tooltip says why (shown even though the button is disabled: the hover is on the wrapper).
Item {
    id: root

    property string text                              // while it can send
    property string disabledText: qsTr("Send to device")
    property string hint                              // tooltip while it can send
    property bool canTrigger: true                    // something to send here
    property bool flat: false

    signal triggered()

    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    Button {
        id: button
        objectName: root.objectName + "Inner"
        anchors.fill: parent
        flat: root.flat
        focusPolicy: Qt.NoFocus
        enabled: SendToDevice.canSend && root.canTrigger
        text: SendToDevice.canSend ? root.text : root.disabledText
        onClicked: root.triggered()
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && ToolTip.text !== ""
    ToolTip.delay: 600
    ToolTip.text: !SendToDevice.canSend ? SendToDevice.disabledReason : root.hint
}
