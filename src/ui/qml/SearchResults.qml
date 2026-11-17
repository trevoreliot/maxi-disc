// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Tracks matching the search box, best match first.
Pane {
    id: root

    property alias searchText: results.searchText
    readonly property alias count: results.count

    padding: 0

    TrackListModel {
        id: results
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 12
            text: results.loading && results.count === 0 ? qsTr("Searching…")
                : results.count === 0 ? qsTr("No matches")
                : results.count >= results.searchLimit ? qsTr("First %1 matches").arg(results.count)
                : results.count === 1 ? qsTr("1 match")
                : qsTr("%1 matches").arg(results.count)
            opacity: 0.7
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: results
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
                // Plays the results from here, so "next" continues through them.
                onDoubleClicked: Playback.playFromModel(results, index)

                contentItem: RowLayout {
                    spacing: 12

                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 4
                        text: (row.playingNow ? "▶ " : "") + row.title
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
