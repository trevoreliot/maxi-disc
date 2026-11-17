// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The play queue: double-click to jump, ✕ to remove, save as a playlist.
Pane {
    id: root

    padding: 0

    TextPrompt {
        id: savePrompt
        title: qsTr("Save queue as playlist")
        placeholderText: qsTr("Playlist name")
        onSubmitted: (name) => Playback.saveQueueAsPlaylist(name)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8

            Label {
                Layout.fillWidth: true
                text: Playback.queue.count === 0 ? qsTr("Queue is empty")
                    : qsTr("Queue · %1 · %2").arg(Playback.queue.count === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(Playback.queue.count))
                                                 .arg(AppBridge.formatDuration(Playback.queue.totalDurationMs))
                font.bold: true
                elide: Text.ElideRight
            }
            ToolButton {
                text: qsTr("Save…")
                enabled: Playback.queue.count > 0
                onClicked: {
                    savePrompt.text = "";
                    savePrompt.open();
                }
            }
            ToolButton {
                text: qsTr("Clear")
                enabled: Playback.queue.count > 0
                onClicked: Playback.clearQueue()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Playback.queue
            currentIndex: Playback.queue.currentIndex
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row

                required property int index
                required property string title
                required property string artist
                required property real durationMs
                required property bool isCurrent

                width: list.width
                leftPadding: 10
                rightPadding: 4
                highlighted: isCurrent
                onDoubleClicked: Playback.playQueueIndex(index)

                contentItem: RowLayout {
                    spacing: 8

                    Label {
                        Layout.preferredWidth: 14
                        text: row.isCurrent ? (Playback.playing ? "▶" : "⏸") : ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Label {
                            Layout.fillWidth: true
                            text: row.title
                            font.bold: row.isCurrent
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.artist
                            opacity: 0.6
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        text: AppBridge.formatDuration(row.durationMs)
                        opacity: 0.6
                        font.features: { "tnum": 1 }
                    }
                    ToolButton {
                        text: "✕"
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
