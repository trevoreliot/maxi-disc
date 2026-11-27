// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The back of a MiniDisc case (V2): the track list on the insert behind the clear sleeve (number,
// title, duration; "Disc N" sections for a multi-disc album), the cursor in it. A mix disc's is
// its playlist, on ruled paper. The list gets its model only once `showRows` is set (when the
// case has landed), so no rows are built while it moves. Fixed size: scaled to turn.
Item {
    id: root

    property bool mix: false
    property int caseId: 0
    property string title
    property color caseColor: Theme.spineDefault
    property TrackListModel trackModel: null
    property bool showRows: false
    property int currentIndex: 0
    property bool active: false // the case has the keyboard
    property bool interactive: true // false while the case moves: rows take no clicks
    readonly property alias tracksView: trackList

    signal trackClicked(int row)
    signal trackActivated(int row)

    function positionAt(i: int) {
        trackList.positionViewAtIndex(i, ListView.Contain);
    }

    function formatTime(ms: real): string {
        const total = Math.round(ms / 1000);
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const ss = (s < 10 ? "0" : "") + s;
        return h > 0 ? h + ":" + (m < 10 ? "0" : "") + m + ":" + ss : m + ":" + ss;
    }

    readonly property color ink: mix ? Theme.mdLabelInk : Theme.textPrimary
    readonly property color inkMuted: mix ? Theme.mdLabelInk : Theme.textSecondary

    Rectangle { // the clear sleeve
        anchors.fill: parent
        radius: 3
        color: Theme.mdCasePlastic
        border.color: Theme.mdCaseBorder
        border.width: 1
    }

    Rectangle {
        id: insert
        anchors.fill: parent
        anchors.margins: 5
        color: root.mix ? Theme.mdLabelPaper : Theme.mdBackInsertBg
        clip: true

        Repeater { // a mix disc's ruled paper
            model: root.mix ? Math.max(0, Math.floor(insert.height / 18)) : 0
            Rectangle {
                required property int index
                x: Theme.spaceSM
                width: insert.width - 2 * Theme.spaceSM
                y: 18 * (index + 1)
                height: 1
                color: Theme.mdLabelRule
            }
        }
        Rectangle { // the album's colour along the top; the blank's for a mix disc
            id: band
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 3
            color: root.mix ? Theme.mdBlankColor(root.caseId) : root.caseColor
        }

        RowLayout {
            id: headerRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: band.bottom
            anchors.margins: Theme.spaceSM
            spacing: Theme.spaceSM

            Label {
                objectName: "backTitle"
                Layout.fillWidth: true
                text: root.title
                elide: Text.ElideRight
                font.bold: !root.mix
                font.italic: root.mix
                color: root.ink
            }
            Label {
                visible: root.trackModel !== null && root.trackModel.count > 0
                text: root.trackModel ? root.formatTime(root.trackModel.totalDurationMs) : ""
                font.pixelSize: Theme.fontSmall
                color: root.inkMuted
            }
        }

        ListView {
            id: trackList
            objectName: "backTracks"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: headerRow.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spaceXS
            anchors.bottomMargin: Theme.spaceXS
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.showRows ? root.trackModel : null
            currentIndex: root.currentIndex
            highlightFollowsCurrentItem: false
            section.property: root.trackModel && root.trackModel.discCount > 1 && !root.mix ? "discNumber" : ""
            section.delegate: Label {
                required property string section
                width: trackList.width
                leftPadding: Theme.spaceSM
                topPadding: 2
                text: qsTr("Disc %1").arg(section)
                font.pixelSize: Theme.fontMicro
                font.bold: true
                color: root.inkMuted
            }

            delegate: Item {
                id: row
                required property int index
                required property int trackNumber
                required property string title
                required property string artist
                required property string duration

                width: trackList.width
                height: 24

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 2
                    anchors.rightMargin: 2
                    radius: 2
                    visible: row.index === root.currentIndex
                    color: root.active ? Theme.rowActiveFocused : Theme.rowSelected
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceSM
                    anchors.rightMargin: Theme.spaceSM
                    spacing: Theme.spaceSM

                    Label {
                        Layout.preferredWidth: 20
                        horizontalAlignment: Text.AlignRight
                        text: root.mix || row.trackNumber <= 0 ? row.index + 1 : row.trackNumber
                        font.pixelSize: Theme.fontSmall
                        color: root.inkMuted
                    }
                    Label {
                        objectName: "trackTitle"
                        Layout.fillWidth: true
                        text: root.mix && row.artist !== "" ? row.title + " – " + row.artist : row.title
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSmall
                        font.italic: root.mix
                        color: root.ink
                    }
                    Label {
                        objectName: "trackDuration"
                        text: row.duration
                        font.pixelSize: Theme.fontSmall
                        color: root.inkMuted
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    enabled: root.interactive
                    onTapped: root.trackClicked(row.index)
                    onDoubleTapped: root.trackActivated(row.index)
                }
            }
        }

        Label {
            anchors.centerIn: trackList
            visible: trackList.count === 0
            text: root.trackModel && root.trackModel.loading ? qsTr("Loading…") : qsTr("No tracks")
            color: root.inkMuted
        }
    }

    Rectangle { // the sleeve's sheen
        anchors.fill: parent
        radius: 3
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.mdCaseSheen }
            GradientStop { position: 0.3; color: "transparent" }
        }
    }
}
