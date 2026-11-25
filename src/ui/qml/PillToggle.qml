// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

// Labelled [off | on] rocker switch (Discord / ListenBrainz). With `interactive: false` it's
// greyed out but still shows its tooltip (a disabled control gets no hover events). With
// `autoToggle: false` a tap only says so (clicked()): `checked` stays bound to the real state.
Item {
    id: root

    property string text
    property bool checked: false
    property bool interactive: true
    property string toolTip
    property bool autoToggle: true

    signal toggled(bool checked)
    signal clicked()

    implicitWidth: 88
    implicitHeight: 44
    opacity: interactive ? 1.0 : 0.45

    HoverHandler {
        id: hover
    }
    ToolTip.visible: hover.hovered && toolTip !== ""
    ToolTip.text: toolTip
    ToolTip.delay: 400

    Column {
        anchors.fill: parent
        spacing: 4

        Label {
            text: root.text
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
        }

        Rectangle {
            width: root.width
            height: 22
            radius: height / 2
            color: Theme.surfaceInset
            border.color: Theme.borderStrong

            Rectangle {
                x: root.checked ? parent.width / 2 : 0
                width: parent.width / 2
                height: parent.height
                radius: height / 2
                color: root.checked ? Theme.accent : Theme.surfaceElevated
            }
            Row {
                anchors.fill: parent
                Label {
                    width: parent.width / 2
                    height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("off")
                    font.pixelSize: Theme.fontSmall
                    color: root.checked ? Theme.textMuted : Theme.textPrimary
                }
                Label {
                    width: parent.width / 2
                    height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("on")
                    font.pixelSize: Theme.fontSmall
                    color: root.checked ? Theme.onAccent : Theme.textMuted
                }
            }
            TapHandler {
                enabled: root.interactive
                onTapped: {
                    root.clicked();
                    if (!root.autoToggle)
                        return;
                    root.checked = !root.checked;
                    root.toggled(root.checked);
                }
            }
        }
    }
}
