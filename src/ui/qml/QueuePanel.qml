// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The play queue, docked on the right of every screen: double-click (or Enter) to jump,
// ✕ to remove, save as a playlist. The keyboard cursor is separate from the playing track;
// it starts on the playing track whenever the panel gains keyboard focus.
Pane {
    id: root

    property bool paneActive: false
    readonly property alias currentIndex: list.currentIndex

    signal interacted()

    function moveSelection(delta: int) {
        if (list.count === 0)
            return;
        list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + delta));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
    }

    function activateCurrent() {
        if (list.currentIndex >= 0)
            Playback.playQueueIndex(list.currentIndex);
    }

    // Down/Up/Enter; anything else isn't the queue's.
    function handleAction(action: int): bool {
        switch (action) {
        case KeyRouter.MoveDown:
            moveSelection(1);
            return true;
        case KeyRouter.MoveUp:
            moveSelection(-1);
            return true;
        case KeyRouter.Activate:
            activateCurrent();
            return true;
        case KeyRouter.AddToDevice:
            SendToDevice.sendQueueTrack(list.currentIndex);
            return true;
        }
        return false;
    }

    onPaneActiveChanged: {
        if (paneActive) {
            list.currentIndex = Math.max(0, Playback.queue.currentIndex);
            if (list.currentIndex < list.count)
                list.positionViewAtIndex(list.currentIndex, ListView.Contain);
        }
    }

    padding: 0
    background: Rectangle {
        color: Theme.surface

        Rectangle {
            width: 1
            height: parent.height
            color: Theme.border
        }
    }

    TextPrompt {
        id: savePrompt
        title: qsTr("Save queue as playlist")
        placeholderText: qsTr("Playlist name")
        onSubmitted: (name) => Playback.saveQueueAsPlaylist(name)
    }

    FocusBar {
        active: root.paneActive
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spaceSM

            Label {
                Layout.fillWidth: true
                text: Playback.queue.count === 0 ? qsTr("Queue is empty")
                    : qsTr("Queue · %1 · %2").arg(Playback.queue.count === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(Playback.queue.count))
                                                 .arg(AppBridge.formatDuration(Playback.queue.totalDurationMs))
                font.bold: true
                elide: Text.ElideRight
            }
            SendToDeviceButton {
                objectName: "sendQueueButton"
                flat: true
                text: qsTr("Send")
                disabledText: qsTr("Send")
                hint: qsTr("Send the queue to %1 (I)").arg(SendToDevice.targetName)
                canTrigger: Playback.queue.count > 0
                onTriggered: SendToDevice.sendQueue()
            }
            ToolButton {
                text: qsTr("Save…")
                focusPolicy: Qt.NoFocus
                enabled: Playback.queue.count > 0
                onClicked: {
                    savePrompt.text = "";
                    savePrompt.open();
                }
            }
            ToolButton {
                text: qsTr("Clear")
                focusPolicy: Qt.NoFocus
                // A playing / paused track stays: nothing to clear if it's all there is.
                enabled: Playback.queue.count > (Playback.playing || Playback.paused ? 1 : 0)
                ToolTip.visible: hovered
                ToolTip.text: Playback.playing || Playback.paused ? qsTr("Clear the queue (the current track keeps playing)")
                                                                   : qsTr("Clear the queue")
                onClicked: Playback.clearQueue()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        ListView {
            id: list
            objectName: "queueList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Playback.queue
            ScrollBar.vertical: ScrollBar {}

            delegate: NavRow {
                id: row

                required property int index
                required property string title
                required property string artist
                required property real durationMs
                required property bool isCurrent

                width: list.width
                leftPadding: 10
                rightPadding: 4
                current: ListView.isCurrentItem
                paneActive: root.paneActive
                onClicked: {
                    list.currentIndex = index;
                    root.interacted();
                }
                onDoubleClicked: Playback.playQueueIndex(index)

                contentItem: RowLayout {
                    spacing: Theme.spaceSM

                    Label {
                        Layout.preferredWidth: 14
                        text: row.isCurrent ? (Playback.playing ? "▶" : "⏸") : ""
                        color: Theme.accent
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            font.bold: row.isCurrent
                            color: row.isCurrent ? Theme.accent : Theme.textPrimary
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.artist
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        text: AppBridge.formatDuration(row.durationMs)
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    ToolButton {
                        text: "✕"
                        focusPolicy: Qt.NoFocus
                        opacity: row.hovered ? 0.8 : 0.0
                        onClicked: Playback.removeFromQueue(row.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove from queue")
                    }
                }
            }
        }
    }
}
