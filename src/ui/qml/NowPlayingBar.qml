// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Transport controls and what's playing. Everything goes through the Playback singleton
// (the same commands MPRIS uses).
ToolBar {
    id: root

    property bool queueVisible: false

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 10

        CoverArt {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            source: Playback.coverUrl
        }

        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: Playback.hasTrack ? Playback.title : qsTr("Nothing playing")
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: Playback.hasTrack
                text: Playback.album !== "" ? Playback.artist + " — " + Playback.album : Playback.artist
                elide: Text.ElideRight
                opacity: 0.7
            }
        }

        ToolButton {
            text: "⏮"
            font.pixelSize: 18
            enabled: Playback.canGoPrevious
            onClicked: Playback.previous()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous")
        }
        ToolButton {
            objectName: "playPauseButton"
            text: Playback.playing ? "⏸" : "▶"
            font.pixelSize: 22
            enabled: Playback.hasTrack || Playback.queue.count > 0
            onClicked: Playback.playPause()
            ToolTip.visible: hovered
            ToolTip.text: Playback.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
        }
        ToolButton {
            text: "⏭"
            font.pixelSize: 18
            enabled: Playback.canGoNext
            onClicked: Playback.next()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next")
        }

        Label {
            text: AppBridge.formatDuration(seekSlider.pressed ? seekSlider.value : Playback.position)
            font.features: { "tnum": 1 }
            opacity: 0.7
        }
        Slider {
            id: seekSlider
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, Playback.duration)
            enabled: Playback.hasTrack && Playback.seekable
            // Follow playback except while the user drags; seek on release.
            Binding on value {
                value: Playback.position
                when: !seekSlider.pressed
                restoreMode: Binding.RestoreNone
            }
            onPressedChanged: if (!pressed) Playback.seek(value)
        }
        Label {
            text: Playback.hasTrack ? AppBridge.formatDuration(Playback.duration) : "0:00"
            font.features: { "tnum": 1 }
            opacity: 0.7
        }

        // Plain Unicode, not emoji: emoji fonts aren't guaranteed to be installed.
        ToolButton {
            text: Playback.repeatMode === 2 ? "↻1" : "↻"
            font.pixelSize: 18
            opacity: Playback.repeatMode === 0 ? 0.4 : 1.0
            onClicked: Playback.cycleRepeatMode()
            ToolTip.visible: hovered
            ToolTip.text: Playback.repeatMode === 0 ? qsTr("Repeat: off")
                        : Playback.repeatMode === 1 ? qsTr("Repeat: all") : qsTr("Repeat: one")
        }

        Slider {
            id: volumeSlider
            Layout.preferredWidth: 110
            from: 0
            to: 1
            value: Playback.volume
            onMoved: Playback.volume = value
            ToolTip.visible: hovered || pressed
            ToolTip.text: qsTr("Volume %1%").arg(Math.round(volumeSlider.value * 100))
        }

        ToolButton {
            text: qsTr("Queue (%1)").arg(Playback.queue.count)
            checkable: true
            checked: root.queueVisible
            onToggled: root.queueVisible = checked
        }
    }
}
