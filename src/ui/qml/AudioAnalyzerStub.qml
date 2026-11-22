// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Placeholder for the spectrum analyzer: a recessed bay with flat bars. Real levels need a
// GStreamer `spectrum` element in the Player (docs/PLAN.md follow-ups); `levels` (0..1 per
// band) is where they'll go.
Rectangle {
    id: root

    property var levels: []
    readonly property int bands: 24

    color: Theme.surfaceInset
    border.color: Theme.borderSubtle
    radius: Theme.radiusSmall

    Row {
        id: bars
        anchors.fill: parent
        anchors.margins: Theme.spaceSM
        spacing: 3

        Repeater {
            model: root.bands
            delegate: Item {
                required property int index
                width: (bars.width - bars.spacing * (root.bands - 1)) / root.bands
                height: bars.height

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(2, parent.height * (root.levels[parent.index] ?? 0))
                    color: Theme.accentMuted
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: root.levels.length === 0
        text: qsTr("ANALYZER · COMING SOON")
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1.5
        color: Theme.textMuted
    }
}
