// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// One playlist: play, export, rename, delete; its tracks (entries not in the library
// are skipped and counted). Keyboard: moveSelection() / activateCurrent().
Pane {
    id: root

    property int playlistId: 0
    property string playlistName
    property int itemCount: 0
    property bool paneActive: false
    readonly property alias trackCount: tracks.count
    readonly property alias currentIndex: list.currentIndex

    signal closed()
    signal interacted()

    function moveSelection(delta: int) {
        if (list.count === 0)
            return;
        list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + delta));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
    }

    function activateCurrent() {
        if (list.currentIndex >= 0)
            Playback.playFromModel(tracks, list.currentIndex);
    }

    padding: 0
    background: Rectangle {
        color: Theme.bg
    }
    onPlaylistIdChanged: list.currentIndex = 0

    FocusBar {
        active: root.paneActive
    }

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
        focus: true // keys go to the dialog, not the KeyRouter
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
                font.pixelSize: Theme.fontDisplay
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
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
            }
            RowLayout {
                spacing: 8

                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("▶ Play")
                    highlighted: true
                    enabled: tracks.count > 0
                    onClicked: Playback.playFromModel(tracks, 0)
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Add to queue")
                    enabled: tracks.count > 0
                    onClicked: Playback.enqueueFromModel(tracks, false)
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Export…")
                    onClicked: exportDialog.open()
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Rename…")
                    onClicked: {
                        renamePrompt.text = root.playlistName;
                        renamePrompt.open();
                    }
                }
                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Delete")
                    onClicked: confirmDelete.open()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        ListView {
            id: list
            objectName: "trackList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: tracks
            ScrollBar.vertical: ScrollBar {}

            delegate: NavRow {
                id: row

                required property int index
                required property int trackId
                required property string title
                required property string artist
                required property string album
                required property string duration

                readonly property bool playingNow: trackId === Playback.currentTrackId

                width: list.width
                current: ListView.isCurrentItem
                paneActive: root.paneActive
                onClicked: {
                    list.currentIndex = index;
                    root.interacted();
                }
                onDoubleClicked: Playback.playFromModel(tracks, index)

                contentItem: RowLayout {
                    spacing: 12

                    Label {
                        Layout.preferredWidth: 28
                        horizontalAlignment: Text.AlignRight
                        text: row.playingNow ? "▶" : row.index + 1
                        font.family: Theme.fontMono
                        color: row.playingNow ? Theme.accent : Theme.textMuted
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 4
                        text: row.title
                        font.bold: row.playingNow
                        color: row.playingNow ? Theme.accent : Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: row.artist
                        elide: Text.ElideRight
                        color: Theme.textSecondary
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 3
                        text: row.album
                        elide: Text.ElideRight
                        color: Theme.textMuted
                    }
                    Label {
                        text: row.duration
                        font.family: Theme.fontMono
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
