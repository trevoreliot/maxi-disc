// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Bottom bar on every screen (docs/ui-draft/main-menu.jpeg): a now-playing + seek strip,
// then four sections: media buttons | audio analyzer | volume | Discord / ListenBrainz.
// Every command goes through the Playback singleton (the same commands MPRIS uses).
Rectangle {
    id: root

    signal listenBrainzSetupRequested() // Main opens the ListenBrainz dialog
    signal discordSetupRequested() // ... and the Discord one

    // Right-click on the cover or the title (Q2): the playing track's menu.
    function openNowPlayingMenu(item: Item, x: real, y: real) {
        if (Playback.hasTrack)
            ContextMenus.open(item, x, y, ContextMenus.nowPlayingEntries(), null);
    }

    implicitHeight: 104
    color: Theme.surfaceChrome

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 1
        spacing: 0

        // Now playing + seek.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.leftMargin: Theme.spaceMD
            Layout.rightMargin: Theme.spaceMD
            spacing: Theme.spaceSM

            CoverArt {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                source: Playback.coverUrl
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: point => root.openNowPlayingMenu(parent, point.position.x, point.position.y)
                }
            }
            Label {
                objectName: "nowPlayingLabel"
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: point => root.openNowPlayingMenu(parent, point.position.x, point.position.y)
                }
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                text: !Playback.hasTrack ? qsTr("Not playing")
                    : Playback.artist !== "" ? Playback.title + " — " + Playback.artist : Playback.title
                elide: Text.ElideRight
                font.pixelSize: Theme.fontBody
                color: Playback.hasTrack ? Theme.textPrimary : Theme.textMuted
            }
            Label {
                text: AppBridge.formatDuration(seekSlider.pressed ? seekSlider.value : Playback.position)
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
            Slider {
                id: seekSlider
                objectName: "seekSlider"
                Layout.fillWidth: true
                focusPolicy: Qt.NoFocus
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
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 1. Media buttons, with repeat / shuffle underneath.
            ColumnLayout {
                Layout.preferredWidth: 232
                Layout.fillHeight: true
                Layout.topMargin: Theme.spaceXS
                Layout.bottomMargin: Theme.spaceXS
                spacing: 2

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spaceSM

                    TransportButton {
                        objectName: "prevButton"
                        glyph: "prev"
                        enabled: Playback.canGoPrevious
                        toolTip: qsTr("Previous (Ctrl+←)")
                        onClicked: Playback.previous()
                    }
                    TransportButton {
                        objectName: "playPauseButton"
                        implicitWidth: 40
                        implicitHeight: 40
                        primary: true
                        glyph: Playback.playing ? "pause" : "play"
                        enabled: Playback.hasTrack || Playback.queue.count > 0
                        toolTip: Playback.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                        onClicked: Playback.playPause()
                    }
                    TransportButton {
                        objectName: "stopButton"
                        glyph: "stop"
                        enabled: Playback.hasTrack
                        toolTip: qsTr("Stop")
                        onClicked: Playback.stop()
                    }
                    TransportButton {
                        objectName: "nextButton"
                        glyph: "next"
                        enabled: Playback.canGoNext
                        toolTip: qsTr("Next (Ctrl+→)")
                        onClicked: Playback.next()
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spaceLG

                    ToolButton {
                        objectName: "repeatButton"
                        implicitWidth: 28
                        implicitHeight: 22
                        focusPolicy: Qt.NoFocus
                        onClicked: Playback.cycleRepeatMode()
                        ToolTip.visible: hovered
                        ToolTip.text: Playback.repeatMode === 0 ? qsTr("Repeat: off")
                                    : Playback.repeatMode === 1 ? qsTr("Repeat: all") : qsTr("Repeat: one")
                        contentItem: Item {
                            Glyph {
                                anchors.centerIn: parent
                                name: "repeat"
                                size: 16
                                color: Playback.repeatMode === 0 ? Theme.textMuted : Theme.accent
                            }
                            Label {
                                visible: Playback.repeatMode === 2
                                anchors.right: parent.right
                                anchors.top: parent.top
                                text: "1"
                                font.pixelSize: Theme.fontMicro
                                font.bold: true
                                color: Theme.accent
                            }
                        }
                    }
                    // Not built yet: a greyed glyph that still explains itself on hover.
                    Item {
                        objectName: "shuffleButton"
                        implicitWidth: 28
                        implicitHeight: 22
                        opacity: 0.4
                        ToolTip.visible: shuffleHover.hovered
                        ToolTip.text: qsTr("Shuffle isn't available yet")

                        HoverHandler {
                            id: shuffleHover
                        }
                        Glyph {
                            anchors.centerIn: parent
                            name: "shuffle"
                            size: 16
                            color: Theme.textMuted
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.borderSubtle
            }

            // 2. Audio analyser (Q3).
            AudioAnalyzer {
                objectName: "audioAnalyzer"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 160
                Layout.margins: Theme.spaceSM
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.borderSubtle
            }

            // 3. Volume.
            VolumeWedge {
                objectName: "volumeWedge"
                Layout.preferredWidth: 136
                Layout.preferredHeight: 48
                Layout.leftMargin: Theme.spaceLG
                Layout.rightMargin: Theme.spaceLG
                Layout.alignment: Qt.AlignVCenter
                value: Playback.volume
                onMoved: (v) => Playback.volume = v
                ToolTip.visible: volumeHover.hovered
                ToolTip.text: qsTr("Volume (scroll to adjust)")
                ToolTip.delay: 800

                HoverHandler {
                    id: volumeHover
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.borderSubtle
            }

            // 4. Online services (M15 Discord, M14 ListenBrainz).
            RowLayout {
                Layout.preferredWidth: 220
                Layout.leftMargin: Theme.spaceLG
                Layout.rightMargin: Theme.spaceLG
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spaceLG

                // Set up: switches the status on / off. Not (yet): opens the Discord dialog.
                PillToggle {
                    objectName: "discordToggle"
                    text: qsTr("Discord")
                    autoToggle: false
                    checked: Discord.enabled && Discord.hasAppId
                    toolTip: Discord.toolTip
                    onClicked: {
                        if (Discord.hasAppId)
                            Discord.enabled = !Discord.enabled;
                        else
                            root.discordSetupRequested();
                    }
                }
                // Connected: switches submitting on / off. Not (yet): opens the ListenBrainz dialog.
                PillToggle {
                    objectName: "listenbrainzToggle"
                    text: qsTr("ListenBrainz")
                    autoToggle: false
                    checked: ListenBrainz.connected && ListenBrainz.enabled
                    toolTip: ListenBrainz.toolTip
                    onClicked: {
                        if (ListenBrainz.connected)
                            ListenBrainz.enabled = !ListenBrainz.enabled;
                        else
                            root.listenBrainzSetupRequested();
                    }
                }
            }
        }
    }
}
