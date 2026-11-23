// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Export screen's right pane when a NetMD recorder is selected (M10): the disc's title,
// capacity and tracks (with groups), or why there's none (no disc, no permission, busy).
// Keyboard (via ExportView): j/k move, Enter / n rename the track, N the disc, R re-reads.
Item {
    id: root

    property bool paneActive: false

    signal interacted()
    signal renameTrackRequested(int index)
    signal renameDiscRequested()

    component Tag: Label {
        leftPadding: 5
        rightPadding: 5
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Theme.fontMicro
        color: Theme.textSecondary
        background: Rectangle {
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: Theme.border
        }
    }

    // --- No disc / error ---
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spaceLG, 460)
        visible: !MiniDisc.hasDisc && !MiniDisc.busy
        spacing: Theme.spaceSM

        Label {
            objectName: "mdStatus"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: MiniDisc.errorString !== "" ? MiniDisc.errorString : qsTr("Insert a disc.")
            color: MiniDisc.errorString !== "" ? Theme.errorText : Theme.textSecondary
        }
        Label {
            objectName: "mdUdevHelp"
            Layout.fillWidth: true
            visible: MiniDisc.permissionError
            text: "sudo install -Dm644 docs/udev/70-netmd.rules /etc/udev/rules.d/70-netmd.rules\nsudo udevadm control --reload-rules && sudo udevadm trigger"
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WrapAnywhere
            color: Theme.textSecondary
        }
        Button {
            objectName: "mdRetryButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Try again (R)")
            focusPolicy: Qt.NoFocus
            onClicked: MiniDisc.refresh()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        visible: MiniDisc.busy && !MiniDisc.hasDisc
        running: visible
    }

    // --- The disc ---
    ColumnLayout {
        anchors.fill: parent
        visible: MiniDisc.hasDisc
        spacing: Theme.spaceSM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSM
            Label {
                objectName: "mdDiscTitle"
                Layout.fillWidth: true
                text: MiniDisc.discTitle !== "" ? MiniDisc.discTitle : qsTr("Untitled disc")
                font.pixelSize: Theme.fontHeading
                font.bold: MiniDisc.discTitle !== ""
                font.italic: MiniDisc.discTitle === ""
                color: MiniDisc.discTitle !== "" ? Theme.textPrimary : Theme.textMuted
                elide: Text.ElideRight
            }
            Button {
                objectName: "mdRenameDiscButton"
                text: qsTr("Rename disc (N)")
                flat: true
                enabled: !MiniDisc.busy
                focusPolicy: Qt.NoFocus
                onClicked: root.renameDiscRequested()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSM
            ProgressBar {
                Layout.preferredWidth: 160
                value: MiniDisc.usedFraction
            }
            Label {
                objectName: "mdCapacity"
                Layout.fillWidth: true
                text: MiniDisc.capacityText
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            Label {
                visible: MiniDisc.busy
                text: MiniDisc.busyText
                font.pixelSize: Theme.fontSmall
                color: Theme.accent
            }
        }
        Label {
            objectName: "mdError"
            Layout.fillWidth: true
            visible: MiniDisc.errorString !== ""
            text: MiniDisc.errorString
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
        Label {
            objectName: "mdBlank"
            Layout.fillWidth: true
            visible: MiniDisc.trackCount === 0
            text: qsTr("Blank disc. Recording to it arrives with uploads (M11); you can title it already.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }

        ListView {
            id: list
            objectName: "mdTrackList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: MiniDisc.tracks
            currentIndex: MiniDisc.tracks.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: Column {
                id: entry
                required property int index
                required property int number
                required property string title
                required property string durationText
                required property string mode
                required property bool protectedTrack
                required property string groupTitle
                required property bool inGroup

                width: ListView.view.width

                Label {
                    width: parent.width
                    visible: entry.groupTitle !== ""
                    leftPadding: Theme.spaceSM
                    topPadding: Theme.spaceSM
                    bottomPadding: 2
                    text: entry.groupTitle
                    font.bold: true
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }
                NavRow {
                    width: parent.width
                    leftPadding: Theme.spaceSM + (entry.inGroup ? Theme.spaceLG : 0)
                    topPadding: 3
                    bottomPadding: 3
                    current: entry.index === MiniDisc.tracks.currentIndex
                    paneActive: root.paneActive
                    onClicked: {
                        MiniDisc.tracks.currentIndex = entry.index;
                        root.interacted();
                    }
                    onDoubleClicked: if (!MiniDisc.busy) root.renameTrackRequested(entry.index)

                    contentItem: RowLayout {
                        spacing: Theme.spaceSM
                        Label {
                            Layout.preferredWidth: 24
                            horizontalAlignment: Text.AlignRight
                            text: String(entry.number).padStart(2, "0")
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: entry.title !== "" ? entry.title : qsTr("Untitled")
                            font.italic: entry.title === ""
                            color: entry.title !== "" ? Theme.textPrimary : Theme.textMuted
                            elide: Text.ElideRight
                        }
                        Tag {
                            visible: entry.protectedTrack
                            text: qsTr("protected")
                        }
                        Tag {
                            visible: entry.mode !== ""
                            text: entry.mode
                        }
                        Label {
                            text: entry.durationText
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                        }
                    }
                }
            }
        }
    }
}
