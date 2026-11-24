// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// A short message at the bottom of the window (M11b: what "send to device" did). Never takes
// clicks or focus; a new message replaces the shown one and restarts the timer.
Rectangle {
    id: root

    property string text
    property bool isError: false
    property int durationMs: 3000

    function show(message: string, error: bool) {
        text = message;
        isError = error;
        opacity = 1;
        hideTimer.restart();
    }

    objectName: "toast"
    enabled: false // clicks go through
    visible: opacity > 0
    opacity: 0
    width: Math.min(label.implicitWidth + 2 * Theme.spaceLG, (parent ? parent.width : 400) - 2 * Theme.spaceLG)
    height: label.implicitHeight + 2 * Theme.spaceSM
    radius: Theme.radiusMedium
    color: isError ? Theme.errorBg : Theme.surfaceElevated
    border.width: 1
    border.color: isError ? Theme.errorText : Theme.accent

    Behavior on opacity {
        NumberAnimation {
            duration: root.opacity > 0 ? 250 : 150
            easing.type: Easing.OutQuad
        }
    }

    Timer {
        id: hideTimer
        interval: root.durationMs
        onTriggered: root.opacity = 0
    }

    Label {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spaceLG
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        color: root.isError ? Theme.errorText : Theme.textPrimary
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }
}
