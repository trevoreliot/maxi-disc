// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tracks matching the search box, best match first.
// Keyboard: moveSelection() / activateCurrent() (plays the results from that one).
Pane {
    id: root

    property alias searchText: results.searchText
    readonly property alias count: results.count
    readonly property alias currentIndex: list.currentIndex
    readonly property alias tracksModel: results
    property bool paneActive: false

    signal interacted()

    function moveSelection(delta: int) {
        if (list.count === 0)
            return;
        list.currentIndex = Math.max(0, Math.min(list.count - 1, list.currentIndex + delta));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
    }

    // `i` / the button: the track under the cursor.
    function sendCurrentToDevice() {
        if (list.currentIndex < 0 || list.currentIndex >= results.count) {
            SendToDevice.sendItems([]);
            return;
        }
        const t = results.get(list.currentIndex);
        SendToDevice.sendItem("track", t.trackId, t.artist ? t.title + " – " + t.artist : t.title);
    }

    function activateCurrent() {
        if (list.currentIndex >= 0)
            Playback.playFromModel(results, list.currentIndex);
    }

    padding: 0
    background: Rectangle {
        color: Theme.bg
    }
    onSearchTextChanged: list.currentIndex = 0

    FocusBar {
        active: root.paneActive
    }

    TrackListModel {
        id: results
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12

            Label {
                Layout.fillWidth: true
                text: results.loading && results.count === 0 ? qsTr("Searching…")
                    : results.count === 0 ? qsTr("No matches")
                    : results.count >= results.searchLimit ? qsTr("First %1 matches").arg(results.count)
                    : results.count === 1 ? qsTr("1 match")
                    : qsTr("%1 matches").arg(results.count)
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
            }
            SendToDeviceButton {
                objectName: "sendSearchButton"
                flat: true
                text: qsTr("Send to %1").arg(SendToDevice.targetName)
                hint: qsTr("The track under the cursor (i)")
                canTrigger: list.currentIndex >= 0 && list.currentIndex < results.count
                onTriggered: root.sendCurrentToDevice()
            }
        }

        ListView {
            id: list
            objectName: "resultList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: results
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
                // Plays the results from here, so "next" continues through them.
                onDoubleClicked: Playback.playFromModel(results, index)

                contentItem: RowLayout {
                    spacing: 12

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 4
                        text: (row.playingNow ? "▶ " : "") + row.title
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
