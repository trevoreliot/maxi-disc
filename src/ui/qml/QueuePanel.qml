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

    // Right-click / `,` (Q1): a queued track's menu, or the queue's own on the empty space.
    // Where the menu's track is now (the queue may have moved while the menu was open): its
    // row if it's still there, else the nearest row with its file; -1 if it's gone.
    function rowOf(i: int, path: string): int {
        const q = Playback.queue;
        if (i < q.count && q.get(i).path === path)
            return i;
        let best = -1;
        for (let r = 0; r < q.count; ++r) {
            if (q.get(r).path === path && (best < 0 || Math.abs(r - i) < Math.abs(best - i)))
                best = r;
        }
        return best;
    }

    function rowEntries(i: int): var {
        const q = Playback.queue;
        const t = q.get(i);
        const cur = q.currentIndex;
        const at = fn => () => { const r = root.rowOf(i, t.path); if (r >= 0) fn(r); };
        return [
            { text: qsTr("Play now"), onTriggered: at(r => Playback.playQueueIndex(r)) },
            { text: qsTr("Move to play next"), enabled: i !== cur && i !== cur + 1, onTriggered: at(r => Playback.moveToPlayNext(r)) },
            { text: qsTr("Move up"), enabled: i > 0, onTriggered: at(r => Playback.moveInQueue(r, r - 1)) },
            { text: qsTr("Move down"), enabled: i < q.count - 1, onTriggered: at(r => Playback.moveInQueue(r, r + 1)) },
            ContextMenus.separator(),
            { text: qsTr("Go to album"), enabled: t.albumId > 0, onTriggered: () => ContextMenus.goToAlbumRequested(t.albumId) },
            { text: qsTr("Add to playlist"), playlists: true,
              onPlaylist: id => Library.appendToPlaylist(id, [t.path]),
              onNewPlaylist: () => ContextMenus.newPlaylistRequested("", 0, [t.path], t.title) },
            { text: ContextMenus.sendText(""), hint: "i", enabled: SendToDevice.canSend, onTriggered: at(r => SendToDevice.sendQueueTrack(r)) },
            ContextMenus.separator(),
            { text: qsTr("Edit tags…"), onTriggered: () => ContextMenus.editTagsRequested([t.path], t.path) },
            ContextMenus.separator(),
            { text: qsTr("Remove from queue"), destructive: true, onTriggered: at(r => Playback.removeFromQueue(r)) },
        ];
    }

    function queueEntries(): var {
        const n = Playback.queue.count;
        return [
            { text: qsTr("Save queue as playlist…"), enabled: n > 0, onTriggered: () => { savePrompt.text = ""; savePrompt.open(); } },
            { text: ContextMenus.sendText(qsTr("queue")), hint: "I", enabled: n > 0 && SendToDevice.canSend, onTriggered: () => SendToDevice.sendQueue() },
            ContextMenus.separator(),
            { text: qsTr("Clear queue"), destructive: true, enabled: n > (Playback.playing || Playback.paused ? 1 : 0),
              onTriggered: () => Playback.clearQueue() },
        ];
    }

    function openContextMenu(i: int, item: Item, x: real, y: real) {
        if (i >= 0 && i < list.count) {
            list.currentIndex = i;
            ContextMenus.open(item, x, y, rowEntries(i), null);
        } else {
            ContextMenus.open(item, x, y, queueEntries(), null);
        }
    }

    function openContextMenuForCurrent() {
        const item = list.currentItem;
        if (item)
            openContextMenu(list.currentIndex, item, Math.min(item.width * 0.35, 220), item.height);
        else
            openContextMenu(-1, list, Theme.spaceLG, Theme.spaceLG);
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

            // One handler for rows and the space below them.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: point => {
                    root.interacted();
                    const i = list.indexAt(point.position.x + list.contentX, point.position.y + list.contentY);
                    const item = i >= 0 ? list.itemAtIndex(i) : null;
                    if (item) {
                        const p = list.mapToItem(item, point.position.x, point.position.y);
                        root.openContextMenu(i, item, p.x, p.y);
                    } else {
                        root.openContextMenu(-1, list, point.position.x, point.position.y);
                    }
                }
            }

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
