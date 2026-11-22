// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Thin amber bar along the top of the pane that has keyboard focus.
Rectangle {
    property bool active: false

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: 2
    z: 10
    color: active ? Theme.focusRing : "transparent"
}
