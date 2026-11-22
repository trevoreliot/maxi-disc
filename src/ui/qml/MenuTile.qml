// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One numbered main-menu tile: number, illustration, label, subtitle.
// `current` = keyboard cursor; `interactive: false` greys it out (tooltip still works).
Rectangle {
    id: root

    property int number: 1
    property string label
    property string subtitle
    property string iconKind: "library"
    property bool current: false
    property bool paneActive: false
    property bool interactive: true
    property string toolTip

    signal activated()

    radius: Theme.radiusLarge
    color: hover.hovered && interactive ? Theme.surfaceElevated : Theme.surface
    border.width: current && paneActive ? 2 : 1
    border.color: current && paneActive ? Theme.focusRing : current ? Theme.accentMuted : Theme.border
    opacity: interactive ? 1.0 : 0.5

    HoverHandler {
        id: hover
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    TapHandler {
        enabled: root.interactive
        onTapped: root.activated()
    }
    ToolTip.visible: hover.hovered && toolTip !== ""
    ToolTip.text: toolTip
    ToolTip.delay: 500

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXL
        spacing: Theme.spaceMD

        Label {
            text: root.number
            font.pixelSize: Theme.fontDisplay
            font.family: Theme.fontMono
            color: root.current && root.paneActive ? Theme.focusRing : Theme.textMuted
        }
        MenuIcon {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            kind: root.iconKind
            muted: !root.interactive
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.label
            font.pixelSize: Theme.fontTitle
            font.bold: true
            color: Theme.textPrimary
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.subtitle
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }
}
