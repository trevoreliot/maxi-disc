// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Right side of the Export screen (M9): the selected device's sync selection and the tracks
// it resolves to; after "Plan sync" the plan; while syncing the progress; then the result.
// All state lives in the Sync singleton. Keyboard (via ExportView): j/k move in the
// selection, x / Delete removes the entry under the cursor.
Item {
    id: root

    property bool paneActive: false // the selection list has the keys
    readonly property int selectionIndex: selectionView.currentIndex
    readonly property bool showsPlan: Sync.state === Sync.Ready || Sync.state === Sync.Syncing || Sync.state === Sync.Done

    signal interacted()

    function moveSelection(delta: int) {
        const n = selectionView.count;
        if (n === 0)
            return;
        selectionView.currentIndex = Math.max(0, Math.min(n - 1, selectionView.currentIndex + delta));
    }

    function removeCurrent() {
        const i = selectionView.currentIndex;
        if (i < 0 || i >= selectionView.count)
            return;
        Sync.removeAt(i); // onCountChanged keeps the cursor in range
    }

    function formatDuration(ms: real): string {
        const minutes = Math.round(ms / 60000);
        return minutes >= 60 ? qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
                             : qsTr("%1 min").arg(minutes);
    }

    component Tag: Label {
        leftPadding: 5
        rightPadding: 5
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Theme.fontMicro
        color: Theme.textSecondary
        background: Rectangle {
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: Theme.border
        }
    }

    // --- Selection + resolved tracks (Idle / Planning) ---
    ColumnLayout {
        anchors.fill: parent
        visible: !root.showsPlan
        spacing: Theme.spaceSM

        Label {
            objectName: "selectionHint"
            Layout.fillWidth: true
            visible: Sync.selection.count === 0
            text: Sync.deviceId === ""
                  ? qsTr("Select a device above to choose what goes on it.")
                  : qsTr("Nothing chosen for this device yet. Pick an artist, album, genre or playlist on the left and press i (or double-click it).")
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }

        ListView {
            id: selectionView
            objectName: "selectionList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.35)
            visible: Sync.selection.count > 0
            clip: true
            model: Sync.selection
            // Kept in range by hand (a binding would be lost at the first j/k).
            onCountChanged: currentIndex = count === 0 ? -1 : Math.max(0, Math.min(currentIndex, count - 1))
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: NavRow {
                id: itemRow
                required property int index
                required property string kind
                required property string name
                required property int trackCount
                required property bool missing

                width: ListView.view.width
                current: index === selectionView.currentIndex
                paneActive: root.paneActive
                onClicked: {
                    selectionView.currentIndex = index;
                    root.interacted();
                }

                contentItem: RowLayout {
                    spacing: Theme.spaceSM
                    Tag {
                        text: itemRow.kind
                    }
                    Label {
                        Layout.fillWidth: true
                        text: itemRow.name
                        elide: Text.ElideRight
                        color: itemRow.missing ? Theme.textMuted : Theme.textPrimary
                    }
                    Label {
                        text: itemRow.missing ? qsTr("not in the library")
                                              : itemRow.trackCount < 0 ? "" : qsTr("%1 tracks").arg(itemRow.trackCount)
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    ToolButton {
                        objectName: "removeItemButton"
                        text: "×"
                        focusPolicy: Qt.NoFocus
                        enabled: Sync.state === Sync.Idle
                        onClicked: Sync.removeAt(itemRow.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove from this device's selection (x)")
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: Sync.selection.count > 0
            color: Theme.border
        }

        ListView {
            id: trackView
            objectName: "exportTrackList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Sync.tracks
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: trackRow
                required property string title
                required property string artist
                required property string album
                required property string codec
                required property string target
                required property bool transcode

                width: ListView.view.width
                focusPolicy: Qt.NoFocus
                hoverEnabled: false
                topPadding: 3
                bottomPadding: 3
                contentItem: RowLayout {
                    spacing: Theme.spaceSM
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: trackRow.title
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: [trackRow.artist, trackRow.album].filter(s => s !== "").join(" · ")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }
                    Label {
                        text: trackRow.transcode ? trackRow.codec + " → " + trackRow.target : trackRow.codec
                        font.pixelSize: Theme.fontSmall
                        color: trackRow.transcode ? Theme.accent : Theme.textMuted
                    }
                }
            }
        }
    }

    // --- Planning ---
    Rectangle {
        anchors.fill: parent
        visible: Sync.state === Sync.Planning
        color: Theme.bg
        opacity: 0.85
        ColumnLayout {
            anchors.centerIn: parent
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: parent.visible
            }
            Label {
                text: qsTr("Checking the device…")
                color: Theme.textSecondary
            }
        }
    }

    // --- Plan / progress / result (Ready / Syncing / Done) ---
    ColumnLayout {
        anchors.fill: parent
        visible: root.showsPlan
        spacing: Theme.spaceSM

        Label {
            objectName: "planSummary"
            Layout.fillWidth: true
            readonly property var s: Sync.planSummary
            text: {
                if (s.adds === undefined)
                    return "";
                if (!s.hasWork)
                    return s.unchanged > 0 ? qsTr("The device is up to date: %1 tracks already there.").arg(s.unchanged)
                                           : qsTr("Nothing to copy.");
                const parts = [];
                if (s.adds > 0) parts.push(qsTr("%1 to copy").arg(s.adds));
                if (s.updates > 0) parts.push(qsTr("%1 to update").arg(s.updates));
                if (s.moves > 0) parts.push(qsTr("%1 to move").arg(s.moves));
                if (s.removes > 0) parts.push(qsTr("%1 to remove").arg(s.removes));
                if (s.playlists > 0) parts.push(qsTr("%1 playlists").arg(s.playlists));
                if (s.unchanged > 0) parts.push(qsTr("%1 unchanged").arg(s.unchanged));
                let space = qsTr("%1 to write").arg(s.toWrite);
                if (s.freeAfter !== "")
                    space += qsTr(" · %1 free after").arg(s.freeAfter);
                return parts.join(" · ") + "\n" + space;
            }
            wrapMode: Text.Wrap
            color: Theme.textPrimary
        }
        Label {
            objectName: "planSpaceError"
            Layout.fillWidth: true
            visible: Sync.planSummary.insufficientSpace === true
            text: qsTr("Not enough space on the device: remove something from the selection, or free up space.")
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
        Label {
            Layout.fillWidth: true
            readonly property var notes: (Sync.planSummary.missing ?? []).map(n => qsTr("Not in the library any more: %1").arg(n))
                                         .concat(Sync.planSummary.warnings ?? [])
            visible: notes.length > 0
            text: notes.slice(0, 4).join("\n") + (notes.length > 4 ? "\n" + qsTr("… and %1 more").arg(notes.length - 4) : "")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            visible: (Sync.planSummary.untracked ?? 0) > 0 && Sync.state === Sync.Ready
            text: qsTr("%1 other files on the device weren't put there by this app: they're left alone.").arg(Sync.planSummary.untracked ?? 0)
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }

        // Progress
        ColumnLayout {
            Layout.fillWidth: true
            visible: Sync.state === Sync.Syncing
            spacing: 2
            ProgressBar {
                objectName: "syncProgress"
                Layout.fillWidth: true
                value: Sync.progress.fraction ?? 0
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("%1 · file %2 of %3 · %4").arg(Sync.progress.stage ?? "")
                          .arg(Math.min((Sync.progress.filesDone ?? 0) + 1, Sync.progress.filesTotal ?? 0))
                          .arg(Sync.progress.filesTotal ?? 0).arg(Sync.progress.bytesText ?? "")
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: Sync.progress.currentFile ?? ""
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontMicro
                color: Theme.textMuted
                elide: Text.ElideMiddle
            }
        }

        // Result
        Label {
            objectName: "syncResult"
            Layout.fillWidth: true
            visible: Sync.state === Sync.Done
            readonly property var r: Sync.result
            text: {
                if (r.ok === undefined)
                    return "";
                const counts = qsTr("%1 copied, %2 updated, %3 moved, %4 removed, %5 playlists (%6 written)")
                    .arg(r.added).arg(r.updated).arg(r.moved).arg(r.removed).arg(r.playlists).arg(r.written);
                if (r.canceled)
                    return qsTr("Sync canceled. Done so far: %1.").arg(counts);
                if (r.error !== "")
                    return qsTr("Sync stopped: %1\nDone so far: %2.").arg(r.error).arg(counts);
                return (r.failed > 0 ? qsTr("Sync finished with %1 problems: ").arg(r.failed) : qsTr("Sync finished: ")) + counts + ".";
            }
            wrapMode: Text.Wrap
            font.bold: true
            color: r.ok === false && !r.canceled ? Theme.errorText : Theme.textPrimary
        }
        Label {
            Layout.fillWidth: true
            visible: Sync.state === Sync.Done && (Sync.result.warnings ?? []).length > 0
            text: (Sync.result.warnings ?? []).slice(0, 4).join("\n")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }

        ListView {
            id: planView
            objectName: "planList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Sync.planActions
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: actionRow
                required property string kind
                required property string label
                required property string title
                required property string detail
                required property string path
                required property string sizeText

                width: ListView.view.width
                focusPolicy: Qt.NoFocus
                hoverEnabled: false
                topPadding: 3
                bottomPadding: 3
                contentItem: RowLayout {
                    spacing: Theme.spaceSM
                    Tag {
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignHCenter
                        text: actionRow.label
                        color: actionRow.kind === "remove" ? Theme.errorText
                             : actionRow.kind === "forget" ? Theme.textMuted : Theme.accent
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: actionRow.title
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: actionRow.detail !== "" ? actionRow.detail : actionRow.path
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                            elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        text: actionRow.sizeText
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
