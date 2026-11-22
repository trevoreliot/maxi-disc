// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One album (docs/ui-draft/music-library.jpeg): title with small cover art beside it,
// tracks below (grouped by disc when there's more than one), and a faded watermark of the
// cover behind them. Keyboard: moveSelection() / activateCurrent() (plays from that track).
// Tracks can be marked for tag editing: Ctrl+click toggles, Shift+click marks a range,
// toggleMarked() marks the cursor row (key `v`). editRequested() asks Main for the editor.
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
    // Rows marked for tag editing (indexes into tracksModel).
    property list<int> markedRows: []
    property int markAnchor: 0
    // The header follows the tracks once loaded, so it shows edited tags; until then (and
    // for an empty album) it uses what the opener passed.
    readonly property var firstTrack: tracks.count > 0 ? tracks.get(0) : null
    readonly property string shownTitle: firstTrack ? firstTrack.album : albumTitle
    readonly property string shownArtist: firstTrack ? firstTrack.albumArtist : albumArtist
    // The tracks' art once loaded, so a newly saved cover shows up.
    readonly property string shownCoverUrl: firstTrack && firstTrack.coverUrl !== "" ? firstTrack.coverUrl : coverUrl
    readonly property int shownYear: {
        let y = 0;
        for (let i = 0; i < tracks.count; ++i) {
            const ty = tracks.get(i).year;
            if (ty > 0 && (y === 0 || ty < y))
                y = ty;
        }
        return tracks.count > 0 ? y : year;
    }

    signal back()
    signal interacted()
    signal editRequested(list<string> paths, string label)
    signal findArtRequested(int albumId)

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

    function pathsOf(rows: list<int>): list<string> {
        const out = [];
        for (const r of rows) {
            if (r >= 0 && r < tracks.count)
                out.push(tracks.get(r).path);
        }
        return out;
    }
    function allRows(): list<int> {
        const out = [];
        for (let i = 0; i < tracks.count; ++i)
            out.push(i);
        return out;
    }
    function clearMarks() {
        markedRows = [];
    }
    function toggleMarked() {
        if (list.currentIndex < 0)
            return;
        const rows = Array.from(markedRows);
        const at = rows.indexOf(list.currentIndex);
        if (at >= 0)
            rows.splice(at, 1);
        else
            rows.push(list.currentIndex);
        markedRows = rows.sort((a, b) => a - b);
        markAnchor = list.currentIndex;
    }
    // Plain click: cursor only. Ctrl: toggle the row's mark. Shift: mark anchor..row.
    function clickRow(row: int, modifiers: int) {
        list.currentIndex = row;
        if (modifiers & Qt.ShiftModifier) {
            const rows = [];
            for (let i = Math.min(markAnchor, row); i <= Math.max(markAnchor, row); ++i)
                rows.push(i);
            markedRows = rows;
        } else if (modifiers & Qt.ControlModifier) {
            toggleMarked();
        } else {
            clearMarks();
            markAnchor = row;
        }
        interacted();
    }
    // `e`: the marked tracks, else the cursor track. `E` / the button: marked, else all.
    function editTags(all: bool) {
        const rows = markedRows.length > 0 ? Array.from(markedRows)
                   : all ? allRows() : (list.currentIndex >= 0 ? [list.currentIndex] : []);
        if (rows.length === 0)
            return;
        const label = rows.length === 1 ? tracks.get(rows[0]).path
                    : rows.length === tracks.count ? qsTr("%1 — all tracks").arg(shownTitle)
                    : qsTr("%1 — %2 of %3 tracks").arg(shownTitle).arg(rows.length).arg(tracks.count);
        editRequested(pathsOf(rows), label);
    }

    padding: 0
    background: Rectangle {
        color: Theme.bg
    }

    TrackListModel {
        id: tracks
        albumId: root.albumId
    }
    // A new album starts with the cursor on its first track and nothing marked.
    onAlbumIdChanged: {
        list.currentIndex = 0;
        clearMarks();
        markAnchor = 0;
    }

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
            source: root.shownCoverUrl
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
                    text: root.shownTitle
                    font.pixelSize: Theme.fontDisplay
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.shownArtist
                    font.pixelSize: Theme.fontHeading
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
                Label {
                    text: {
                        const parts = [];
                        if (root.shownYear > 0)
                            parts.push(root.shownYear);
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
                    Button {
                        objectName: "editTagsButton"
                        text: root.markedRows.length > 0 ? qsTr("Edit tags (%1)…").arg(root.markedRows.length)
                                                         : qsTr("Edit tags…")
                        focusPolicy: Qt.NoFocus
                        enabled: tracks.count > 0
                        onClicked: root.editTags(true)
                    }
                    Button {
                        objectName: "findArtButton"
                        text: qsTr("Find cover…")
                        focusPolicy: Qt.NoFocus
                        enabled: root.albumId > 0
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Look up cover art on MusicBrainz (a)")
                        onClicked: root.findArtRequested(root.albumId)
                    }
                }
            }

            CoverArt {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 88
                Layout.alignment: Qt.AlignTop
                source: root.shownCoverUrl
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
                marked: root.markedRows.includes(index)
                onDoubleClicked: Playback.playFromModel(tracks, index)

                // Clicks with their modifiers (ItemDelegate's clicked() has none).
                TapHandler {
                    acceptedModifiers: Qt.KeyboardModifierMask
                    onTapped: root.clickRow(row.index, point.modifiers)
                }

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
                        visible: row.artist !== "" && row.artist !== root.shownArtist
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
