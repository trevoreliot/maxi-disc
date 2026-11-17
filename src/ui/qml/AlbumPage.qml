// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One album: cover, details and its tracks (grouped by disc when there's more than one).
Pane {
    id: root

    property int albumId: 0
    property string albumTitle
    property string albumArtist
    property int year: 0
    property string coverUrl

    readonly property alias trackCount: tracks.count
    readonly property alias tracksModel: tracks

    signal back()

    padding: 0

    TrackListModel {
        id: tracks
        albumId: root.albumId
    }

    readonly property bool multiDisc: tracks.discCount > 1

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 20

            CoverArt {
                Layout.preferredWidth: 200
                Layout.preferredHeight: 200
                Layout.alignment: Qt.AlignTop
                source: root.coverUrl
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 6

                Button {
                    text: qsTr("‹ Albums")
                    flat: true
                    onClicked: root.back()
                }
                Label {
                    Layout.fillWidth: true
                    text: root.albumTitle
                    font.pixelSize: 26
                    font.bold: true
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: root.albumArtist
                    font.pixelSize: 18
                    elide: Text.ElideRight
                }
                Label {
                    text: {
                        const parts = [];
                        if (root.year > 0)
                            parts.push(root.year);
                        parts.push(tracks.count === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(tracks.count));
                        if (tracks.count > 0)
                            parts.push(AppBridge.formatDuration(tracks.totalDurationMs));
                        return parts.join(" · ");
                    }
                    opacity: 0.7
                }
                RowLayout {
                    spacing: 8

                    Button {
                        objectName: "playAlbumButton"
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
                        text: qsTr("Play next")
                        enabled: tracks.count > 0
                        onClicked: Playback.enqueueFromModel(tracks, true)
                    }
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

            section.property: root.multiDisc ? "discNumber" : ""
            section.delegate: Label {
                required property string section
                width: list.width
                leftPadding: 16
                topPadding: 12
                bottomPadding: 4
                text: qsTr("Disc %1").arg(section)
                font.bold: true
                opacity: 0.8
            }

            delegate: ItemDelegate {
                id: row

                required property int index
                required property int trackId
                required property int trackNumber
                required property string title
                required property string artist
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
                        text: row.playingNow ? "▶" : (row.trackNumber > 0 ? row.trackNumber : "")
                        opacity: 0.6
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.title
                        font.bold: row.playingNow
                        elide: Text.ElideRight
                    }
                    Label {
                        // Only for compilations / guest artists: hide when it's the album artist.
                        Layout.maximumWidth: list.width * 0.3
                        visible: row.artist !== "" && row.artist !== root.albumArtist
                        text: row.artist
                        elide: Text.ElideRight
                        opacity: 0.7
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
