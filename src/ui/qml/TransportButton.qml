// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Round transport button (prev / play / stop / next). `primary` gets the amber ring.
AbstractButton {
    id: control

    property string glyph: "play"
    property bool primary: false
    property string toolTip

    implicitWidth: 32
    implicitHeight: 32
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.4

    ToolTip.visible: hovered && toolTip !== ""
    ToolTip.text: toolTip
    ToolTip.delay: 600

    background: Rectangle {
        radius: width / 2
        color: control.down ? Theme.surfaceInset : control.hovered ? Theme.surfaceElevated : Theme.surface
        border.width: control.primary ? 2 : 1
        border.color: control.primary ? Theme.accent : Theme.borderStrong
    }

    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            // The play triangle looks off-centre when centred geometrically.
            anchors.horizontalCenterOffset: control.glyph === "play" ? 1 : 0
            name: control.glyph
            size: Math.round(control.width * 0.55)
            color: control.primary ? Theme.accent : Theme.textPrimary
        }
    }
}
