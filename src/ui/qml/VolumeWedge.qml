// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// Wedge-shaped volume control (rises left to right) with a percentage label.
// Click or drag to set, scroll to nudge by 5 %. `moved` fires on user changes only.
Item {
    id: root

    property real value: 0.5 // 0..1
    signal moved(real value)

    implicitWidth: 136
    implicitHeight: 48

    function setFromUser(v: real) {
        const clamped = Math.max(0, Math.min(1, v));
        if (clamped === root.value)
            return;
        root.value = clamped;
        root.moved(clamped);
    }

    Label {
        id: percent
        objectName: "volumeLabel"
        anchors.right: parent.right
        anchors.top: parent.top
        text: Math.round(root.value * 100) + "%"
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSmall
        color: Theme.textSecondary
    }

    Item {
        id: wedge
        objectName: "wedge"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: percent.bottom
        anchors.topMargin: 2

        // Filled part: the whole wedge, clipped to the current value.
        Item {
            width: wedge.width * root.value
            height: wedge.height
            clip: true

            Shape {
                width: wedge.width
                height: wedge.height
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: Theme.accent
                    strokeColor: "transparent"
                    startX: 0
                    startY: wedge.height
                    PathLine { x: wedge.width; y: 0 }
                    PathLine { x: wedge.width; y: wedge.height }
                    PathLine { x: 0; y: wedge.height }
                }
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"
                strokeColor: Theme.borderStrong
                strokeWidth: 1
                startX: 0
                startY: wedge.height
                PathLine { x: wedge.width; y: 0 }
                PathLine { x: wedge.width; y: wedge.height }
                PathLine { x: 0; y: wedge.height }
            }
        }

        // Level tick.
        Rectangle {
            x: Math.min(wedge.width - width, Math.max(0, wedge.width * root.value - width / 2))
            width: 2
            height: wedge.height
            color: Theme.textPrimary
        }

        MouseArea {
            anchors.fill: parent
            preventStealing: true
            onPressed: (mouse) => root.setFromUser(mouse.x / width)
            onPositionChanged: (mouse) => {
                if (pressed)
                    root.setFromUser(mouse.x / width);
            }
            onWheel: (wheel) => root.setFromUser(root.value + (wheel.angleDelta.y > 0 ? 0.05 : -0.05))
        }
    }
}
