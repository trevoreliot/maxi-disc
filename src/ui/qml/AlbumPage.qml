// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One album (docs/ui-draft/music-library.jpeg): title with small cover art beside it,
// tracks below (grouped by disc when there's more than one), and a faded watermark of the
// cover behind them. Keyboard: moveSelection() / activateCurrent() (plays from that track).
Pane {
    id: root

    property int albumId: 0
    property string albumTitle
    property string albumArtist
    property int year: 0
    property string coverUrl
    property bool showBack: false // "‹ Albums" (opened from the album grid)
    property bool paneActive: false

    readonly property alias trackCount: tracks.count
    readonly property alias tracksModel: tracks
    readonly property alias currentIndex: list.currentIndex
    readonly property bool multiDisc: tracks.discCount > 1

    signal back()
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

    TrackListModel {
        id: tracks
        albumId: root.albumId
    }
    // A new album starts with the cursor on its first track.
    onAlbumIdChanged: list.currentIndex = 0

    // Watermark: the cover, large and faint, on the right; fades out towards the left.
    Item {
        anchors.fill: parent
        clip: true

        CoverArt {
            id: watermark
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(parent.width * 0.68, parent.height)
            height: width
            source: root.coverUrl
            requestSize: 512
            showPlaceholder: false
            opacity: Theme.watermarkOpacity
        }
        Rectangle {
            anchors.left: watermark.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: watermark.width * 0.45
            visible: watermark.ready
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.bg }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    FocusBar {
        active: root.paneActive
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spaceLG
            spacing: Theme.spaceLG

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.maximumWidth: root.width - 88 - 3 * Theme.spaceLG
                spacing: Theme.spaceXS

                Button {
                    visible: root.showBack
                    text: qsTr("‹ Albums")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.back()
                }
                Label {
                    objectName: "albumTitle"
                    Layout.fillWidth: true
                    text: root.albumTitle
                    font.pixelSize: Theme.fontDisplay
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.albumArtist
                    font.pixelSize: Theme.fontHeading
                    color: Theme.textSecondary
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
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.topMargin: Theme.spaceXS
                    spacing: Theme.spaceSM

                    Button {
                        objectName: "playAlbumButton"
                        text: qsTr("▶ Play")
                        highlighted: true
                        focusPolicy: Qt.NoFocus
                        enabled: tracks.count > 0
                        onClicked: Playback.playFromModel(tracks, 0)
                    }
                    Button {
                        text: qsTr("Play next")
                        focusPolicy: Qt.NoFocus
                        enabled: tracks.count > 0
                        onClicked: Playback.enqueueFromModel(tracks, true)
                    }
                    Button {
                        text: qsTr("Add to queue")
                        focusPolicy: Qt.NoFocus
                        enabled: tracks.count > 0
                        onClicked: Playback.enqueueFromModel(tracks, false)
                    }
                }
            }

            CoverArt {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 88
                Layout.alignment: Qt.AlignTop
                source: root.coverUrl
                requestSize: 256
            }

            Item {
                Layout.fillWidth: true
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

            section.property: root.multiDisc ? "discNumber" : ""
            section.delegate: Label {
                required property string section
                width: list.width
                leftPadding: Theme.spaceLG
                topPadding: Theme.spaceMD
                bottomPadding: Theme.spaceXS
                text: qsTr("Disc %1").arg(section)
                font.bold: true
                color: Theme.textSecondary
            }

            delegate: NavRow {
                id: row

                required property int index
                required property int trackId
                required property int trackNumber
                required property string title
                required property string artist
                required property string duration

                readonly property bool playingNow: trackId === Playback.currentTrackId

                width: list.width
                height: 36
                current: ListView.isCurrentItem
                paneActive: root.paneActive
                onClicked: {
                    list.currentIndex = index;
                    root.interacted();
                }
                onDoubleClicked: Playback.playFromModel(tracks, index)

                contentItem: RowLayout {
                    spacing: Theme.spaceMD

                    Label {
                        Layout.preferredWidth: 32
                        horizontalAlignment: Text.AlignRight
                        text: row.playingNow ? "▶" : (row.trackNumber > 0 ? row.trackNumber : "")
                        font.family: Theme.fontMono
                        color: row.playingNow ? Theme.accent : Theme.textMuted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.title
                        font.bold: row.playingNow
                        color: row.playingNow ? Theme.accent : Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    Label {
                        // Only for compilations / guest artists: hide when it's the album artist.
                        Layout.maximumWidth: list.width * 0.3
                        visible: row.artist !== "" && row.artist !== root.albumArtist
                        text: row.artist
                        elide: Text.ElideRight
                        color: Theme.textSecondary
                    }
                    Label {
                        Layout.preferredWidth: 56
                        horizontalAlignment: Text.AlignRight
                        text: row.duration
                        font.family: Theme.fontMono
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
