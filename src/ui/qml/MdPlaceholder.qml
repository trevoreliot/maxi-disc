// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

// A dashed outline with a heading: where something of the MD Visualizer will stand (the case
// area while no case is out, the player until V3).
Item {
    id: placeholder

    property string heading
    property string detail

    Shape {
        anchors.fill: parent
        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.border
            strokeWidth: 1.5
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            PathRectangle {
                x: 1
                y: 1
                width: placeholder.width - 2
                height: placeholder.height - 2
                radius: Theme.radiusLarge
            }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spaceLG
        spacing: Theme.spaceXS

        Label {
            objectName: "placeholderHeading"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: placeholder.heading
            elide: Text.ElideRight
            font.pixelSize: Theme.fontHeading
            color: Theme.textSecondary
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            horizontalAlignment: Text.AlignHCenter
            text: placeholder.detail
            elide: Text.ElideRight
            color: Theme.textMuted
        }
    }
}
