// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Export screen's right pane when a NetMD recorder is selected: the disc (M10: title,
// capacity, tracks with groups) and the tracks to record onto it (M11: the queue, whether it
// fits, the recording's progress), switched with o; or why there's no disc.
// Keyboard (via ExportView): j/k move, n / Enter rename (disc view), x / X remove / clear
// (record view), S record / cancel, R re-reads. M12b (disc view): v marks, d / x deletes (asks),
// J / K move the track, D wipes the disc (asks), Esc clears the marks.
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
    component ViewTab: Button {
        property bool selected: false
        flat: !selected
        highlighted: selected
        focusPolicy: Qt.NoFocus
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

        // Capacity: recorded, then (in the record view) what the queue needs, then free.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSM
            Rectangle {
                id: bar
                objectName: "mdCapacityBar"
                Layout.preferredWidth: 200
                Layout.preferredHeight: 8
                radius: 4
                color: Theme.surfaceInset
                border.color: Theme.border
                Rectangle {
                    id: usedPart
                    height: parent.height
                    radius: 4
                    width: parent.width * MiniDisc.usedFraction
                    color: Theme.accent
                }
                Rectangle {
                    objectName: "mdQueuedPart"
                    visible: MiniDisc.recordView && !MiniDisc.recording && (MiniDisc.plan.pending ?? 0) > 0
                    x: usedPart.width
                    height: parent.height
                    radius: 4
                    width: Math.min(parent.width - usedPart.width, parent.width * (MiniDisc.plan.queuedFraction ?? 0))
                    color: MiniDisc.plan.overflow ? Theme.errorText : Theme.accentMuted
                }
            }
            Label {
                objectName: "mdCapacity"
                Layout.fillWidth: true
                text: MiniDisc.recordView && (MiniDisc.plan.pending ?? 0) > 0 && !MiniDisc.recording
                      ? qsTr("%1 · to record %2 · %3 free in %4").arg(MiniDisc.capacityText.split(" · ")[0])
                            .arg(MiniDisc.plan.neededText ?? "").arg(MiniDisc.plan.availableText ?? "")
                            .arg(MiniDisc.recordModeName)
                      : MiniDisc.capacityText
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            Label {
                visible: MiniDisc.busy && !MiniDisc.recording
                text: MiniDisc.busyText
                font.pixelSize: Theme.fontSmall
                color: Theme.accent
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            ViewTab {
                objectName: "mdDiscTab"
                selected: !MiniDisc.recordView
                text: qsTr("On disc (%1)").arg(MiniDisc.trackCount)
                onClicked: {
                    MiniDisc.recordView = false;
                    root.interacted();
                }
            }
            ViewTab {
                objectName: "mdRecordTab"
                selected: MiniDisc.recordView
                text: qsTr("To record (%1)").arg(MiniDisc.queue.count)
                onClicked: {
                    MiniDisc.recordView = true;
                    root.interacted();
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            objectName: "mdError"
            Layout.fillWidth: true
            visible: MiniDisc.errorString !== "" && !MiniDisc.recordView
            text: MiniDisc.errorString
            wrapMode: Text.Wrap
            color: Theme.errorText
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: MiniDisc.recordView ? 1 : 0

            // On disc
            ColumnLayout {
                spacing: Theme.spaceSM
                Label {
                    objectName: "mdBlank"
                    Layout.fillWidth: true
                    visible: MiniDisc.trackCount === 0
                    text: qsTr("Blank disc. Add albums or playlists with i (they go to \"To record\"), then press S.")
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
                        required property bool marked

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
                            marked: entry.marked
                            paneActive: root.paneActive && !MiniDisc.recordView
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

            // To record
            ColumnLayout {
                spacing: Theme.spaceSM

                // Progress while recording; the outcome afterwards.
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: MiniDisc.recording
                    spacing: 2
                    Label {
                        objectName: "mdRecordTrack"
                        Layout.fillWidth: true
                        text: MiniDisc.progress.trackText ?? ""
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    ProgressBar {
                        objectName: "mdRecordProgress"
                        Layout.fillWidth: true
                        value: MiniDisc.progress.overallFraction ?? 0
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [MiniDisc.progress.stage ?? "", MiniDisc.progress.bytesText ?? ""].filter(s => s !== "").join(" · ")
                        font.pixelSize: Theme.fontSmall
                        color: MiniDisc.cancelling ? Theme.accent : Theme.textSecondary
                        elide: Text.ElideRight
                    }
                }
                Label {
                    objectName: "mdRecordResult"
                    Layout.fillWidth: true
                    visible: !MiniDisc.recording && MiniDisc.resultText !== ""
                    text: MiniDisc.resultText
                    wrapMode: Text.Wrap
                    font.bold: true
                }

                Label {
                    objectName: "mdPlanError"
                    Layout.fillWidth: true
                    visible: !MiniDisc.recording && (MiniDisc.plan.pending ?? 0) > 0 && (MiniDisc.plan.error ?? "") !== ""
                    text: MiniDisc.plan.error ?? ""
                    wrapMode: Text.Wrap
                    color: Theme.errorText
                }
                Label {
                    Layout.fillWidth: true
                    visible: !MiniDisc.recording && (MiniDisc.plan.warning ?? "") !== ""
                    text: MiniDisc.plan.warning ?? ""
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSmall
                    color: Theme.accent
                }
                CheckBox {
                    objectName: "mdTitleDiscBox"
                    visible: !MiniDisc.recording && (MiniDisc.plan.suggestedDiscTitle ?? "") !== ""
                    text: qsTr("Title the disc \"%1\"").arg(MiniDisc.plan.suggestedDiscTitle ?? "")
                    checked: MiniDisc.titleDisc
                    focusPolicy: Qt.NoFocus
                    onToggled: MiniDisc.titleDisc = checked
                }
                Label {
                    objectName: "mdQueueHint"
                    Layout.fillWidth: true
                    visible: MiniDisc.queue.count === 0
                    text: qsTr("Pick an album, artist, genre or playlist on the left and press i (or double-click it) to add its tracks. Then press S to record them in SP.")
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                }

                ListView {
                    id: queueList
                    objectName: "mdQueueList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: MiniDisc.queue
                    currentIndex: MiniDisc.queue.currentIndex
                    boundsBehavior: Flickable.StopAtBounds
                    highlightMoveDuration: 0
                    ScrollBar.vertical: ScrollBar {}
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                    delegate: NavRow {
                        id: queued
                        required property int index
                        required property int number
                        required property string title
                        required property string artist
                        required property string durationText
                        required property string itemState
                        required property string error

                        width: ListView.view.width
                        leftPadding: Theme.spaceSM
                        topPadding: 3
                        bottomPadding: 3
                        current: queued.index === MiniDisc.queue.currentIndex
                        paneActive: root.paneActive && MiniDisc.recordView
                        onClicked: {
                            MiniDisc.queue.currentIndex = queued.index;
                            root.interacted();
                        }

                        contentItem: RowLayout {
                            spacing: Theme.spaceSM
                            Label {
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignRight
                                text: String(queued.number).padStart(2, "0")
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textMuted
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: queued.title
                                    elide: Text.ElideRight
                                    color: queued.itemState === "skipped" ? Theme.textMuted : Theme.textPrimary
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: queued.artist !== "" || queued.error !== ""
                                    text: queued.error !== "" ? queued.error : queued.artist
                                    font.pixelSize: Theme.fontSmall
                                    color: queued.error !== "" ? Theme.errorText : Theme.textMuted
                                    elide: Text.ElideRight
                                }
                            }
                            Tag {
                                visible: queued.itemState !== "pending"
                                text: queued.itemState === "decoding" ? qsTr("preparing")
                                    : queued.itemState === "uploading" ? qsTr("recording")
                                    : queued.itemState === "done" ? qsTr("done")
                                    : queued.itemState === "failed" ? qsTr("failed")
                                    : qsTr("skipped")
                                color: queued.itemState === "failed" ? Theme.errorText
                                     : queued.itemState === "done" || queued.itemState === "uploading" || queued.itemState === "decoding" ? Theme.accent
                                     : Theme.textMuted
                            }
                            Tag {
                                text: MiniDisc.recordModeName // how it goes on the disc (M12)
                            }
                            Label {
                                text: queued.durationText
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
}
