// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// A list row with the app's selection look. `current` marks the keyboard cursor; it's
// stronger (with an amber edge) while the row's pane has keyboard focus (`paneActive`).
// `marked`: picked for a multi-track action (a tint and a muted amber edge).
ItemDelegate {
    id: control

    property bool current: false
    property bool paneActive: false
    property bool marked: false

    leftPadding: Theme.spaceLG
    rightPadding: Theme.spaceLG
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        color: control.current ? (control.paneActive ? Theme.rowActiveFocused : Theme.rowSelected)
             : control.marked ? Theme.rowMarked
             : control.hovered ? Theme.rowHover : "transparent"

        Rectangle {
            visible: control.marked || (control.current && control.paneActive)
            width: control.marked && !(control.current && control.paneActive) ? 3 : 2
            height: parent.height
            color: control.current && control.paneActive ? Theme.focusRing : Theme.accentMuted
        }
    }
}
