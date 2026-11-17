// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// One playlist: play, export, rename, delete; its tracks (entries not in the library
// are skipped and counted).
Pane {
    id: root

    property int playlistId: 0
    property string playlistName
    property int itemCount: 0
    readonly property alias trackCount: tracks.count

    signal closed()

    padding: 0

    TrackListModel {
        id: tracks
        playlistId: root.playlistId
    }

    TextPrompt {
        id: renamePrompt
        title: qsTr("Rename playlist")
        onSubmitted: (name) => {
            if (Library.renamePlaylist(root.playlistId, name))
                root.playlistName = name;
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export playlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u8"
        nameFilters: [qsTr("Playlists (*.m3u8 *.m3u)")]
        onAccepted: Library.exportPlaylist(root.playlistId, selectedFile)
    }

    Dialog {
        id: confirmDelete
        title: qsTr("Delete “%1”?").arg(root.playlistName)
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("The playlist is removed. Your music files are not touched.")
        }
        onAccepted: {
            if (Library.deletePlaylist(root.playlistId))
                root.closed();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: root.playlistName
                font.pixelSize: 26
                font.bold: true
                elide: Text.ElideRight
            }
            Label {
                text: {
                    const n = tracks.count === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(tracks.count);
                    const missing = root.itemCount - tracks.count;
                    const parts = [n, AppBridge.formatDuration(tracks.totalDurationMs)];
                    if (missing > 0)
                        parts.push(qsTr("%1 not in library").arg(missing));
                    return parts.join(" · ");
                }
                opacity: 0.7
            }
            RowLayout {
                spacing: 8

                Button {
                    text: qsTr("▶ Play")
                    highlighted: true
                    enabled: tracks.count > 0
                    onClicked: Playback.playFromModel(tracks, 0)
                }
                Button {
                    text: qsTr("Add to queue")
                    enabled: tracks.count > 0
                    onClicked: Playback.enqueueFromModel(tracks, false)
                }
                Button {
                    text: qsTr("Export…")
                    onClicked: exportDialog.open()
                }
                Button {
                    text: qsTr("Rename…")
                    onClicked: {
                        renamePrompt.text = root.playlistName;
                        renamePrompt.open();
                    }
                }
                Button {
                    text: qsTr("Delete")
                    onClicked: confirmDelete.open()
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: tracks
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row

                required property int index
                required property int trackId
                required property string title
                required property string artist
                required property string album
                required property string duration

                readonly property bool playingNow: trackId === Playback.currentTrackId

                width: list.width
                leftPadding: 16
                rightPadding: 16
                onDoubleClicked: Playback.playFromModel(tracks, index)

                contentItem: RowLayout {
                    spacing: 12

                    Label {
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignRight
                        text: row.playingNow ? "▶" : row.index + 1
                        opacity: 0.6
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 4
                        text: row.title
                        font.bold: row.playingNow
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: row.artist
                        elide: Text.ElideRight
                        opacity: 0.8
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: row.album
                        elide: Text.ElideRight
                        opacity: 0.6
                    }
                    Label {
                        text: row.duration
                        opacity: 0.6
                        font.features: { "tnum": 1 }
                    }
                }
            }
        }
    }
}
