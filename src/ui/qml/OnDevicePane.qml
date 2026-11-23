// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Export screen's "On Device" view (M9b): the selected device's music, artist → album →
// tracks, with "not in library" / "synced" marks. Read-only. State lives in the OnDevice
// singleton. Keyboard (via ExportView): j/k move, Enter folds / unfolds a header.
Item {
    id: root

    property bool paneActive: false

    signal interacted()

    readonly property bool hasDevice: Devices.currentIndex >= 0
    readonly property bool mounted: hasDevice && Devices.current.mounted === true

    function tracksText(n: int): string {
        return n === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(n);
    }

    function formatDuration(ms: real): string {
        const s = Math.round(ms / 1000);
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
    }

    component Tag: Label {
        leftPadding: 5
        rightPadding: 5
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Theme.fontMicro
        background: Rectangle {
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: Theme.border
        }
    }

    // --- No device / not mounted ---
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spaceLG, 420)
        visible: !root.mounted
        spacing: Theme.spaceSM

        Label {
            objectName: "onDeviceUnmounted"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: !root.hasDevice ? qsTr("Select a device to see what's on it.")
                                  : qsTr("%1 isn't mounted.").arg(Devices.current.name ?? "")
            color: Theme.textSecondary
        }
        Button {
            objectName: "onDeviceMountButton"
            Layout.alignment: Qt.AlignHCenter
            visible: root.hasDevice
            enabled: !(Devices.current.busy ?? false) && Sync.state !== Sync.Syncing
            text: qsTr("Mount (m)")
            focusPolicy: Qt.NoFocus
            onClicked: Devices.mount(Devices.current.objectPath)
        }
    }

    // --- The device's music ---
    ColumnLayout {
        anchors.fill: parent
        visible: root.mounted
        spacing: Theme.spaceSM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSM
            Label {
                objectName: "onDeviceSummary"
                text: {
                    const m = OnDevice.model;
                    let s = root.tracksText(m.trackCount) + " · " + m.totalSizeText;
                    if ((Devices.current.spaceText ?? "") !== "")
                        s += " · " + Devices.current.spaceText;
                    return s;
                }
                color: Theme.textPrimary
            }
            Label {
                objectName: "onDeviceNotInLibrary"
                visible: OnDevice.model.notInLibraryCount > 0
                text: qsTr("· %1 not in library").arg(OnDevice.model.notInLibraryCount)
                color: Theme.accent
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: OnDevice.scannedDir !== "" ? qsTr("in %1/").arg(OnDevice.scannedDir) : qsTr("whole device")
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
            }
            Button {
                objectName: "wholeDeviceButton"
                visible: OnDevice.canScanWholeDevice
                flat: true
                focusPolicy: Qt.NoFocus
                text: OnDevice.wholeDevice ? qsTr("Only %1/").arg(Devices.currentProfile.musicDir ?? "")
                                           : qsTr("Whole device")
                onClicked: OnDevice.wholeDevice = !OnDevice.wholeDevice
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: OnDevice.browsing || OnDevice.paused
            spacing: Theme.spaceSM
            ProgressBar {
                Layout.fillWidth: true
                visible: OnDevice.browsing
                indeterminate: OnDevice.total < 0
                value: OnDevice.total > 0 ? OnDevice.done / OnDevice.total : 0
            }
            Label {
                text: OnDevice.paused ? qsTr("Syncing: the list refreshes when it's done.")
                    : OnDevice.total < 0 ? qsTr("Looking for music…")
                    : qsTr("Reading tags %1 / %2").arg(OnDevice.done).arg(OnDevice.total)
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
        }

        Label {
            objectName: "onDeviceError"
            Layout.fillWidth: true
            visible: OnDevice.errorString !== ""
            text: OnDevice.errorString
            wrapMode: Text.Wrap
            color: Theme.errorText
        }
        Label {
            Layout.fillWidth: true
            visible: OnDevice.fellBack
            text: qsTr("The profile's music folder isn't on the device: showing the whole device.")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
        Label {
            objectName: "onDeviceEmpty"
            Layout.fillWidth: true
            visible: !OnDevice.browsing && !OnDevice.paused && OnDevice.errorString === "" && OnDevice.model.trackCount === 0
            text: OnDevice.scannedDir !== "" ? qsTr("No music in %1/.").arg(OnDevice.scannedDir) : qsTr("No music on this device.")
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }

        ListView {
            id: list
            objectName: "onDeviceList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: OnDevice.model
            currentIndex: OnDevice.model.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: NavRow {
                id: row
                required property int index
                required property int kind
                required property string label
                required property string detail
                required property bool expanded
                required property int trackCount
                required property string sizeText
                required property int notInLibraryCount
                required property int trackNumber
                required property real durationMs
                required property string codec
                required property bool inLibrary
                required property bool synced

                width: ListView.view.width
                leftPadding: Theme.spaceSM + row.kind * Theme.spaceLG
                topPadding: row.kind === OnDeviceModel.TrackRow ? 2 : 5
                bottomPadding: row.kind === OnDeviceModel.TrackRow ? 2 : 5
                current: index === OnDevice.model.currentIndex
                paneActive: root.paneActive
                onClicked: {
                    OnDevice.model.currentIndex = index;
                    root.interacted();
                }
                onDoubleClicked: OnDevice.model.toggle(index)

                contentItem: RowLayout {
                    spacing: Theme.spaceSM
                    Label {
                        visible: row.kind !== OnDeviceModel.TrackRow
                        text: row.expanded ? "▾" : "▸"
                        color: Theme.textMuted
                        TapHandler {
                            onTapped: OnDevice.model.toggle(row.index)
                        }
                    }
                    Label {
                        visible: row.kind === OnDeviceModel.TrackRow
                        Layout.preferredWidth: 22
                        horizontalAlignment: Text.AlignRight
                        text: row.trackNumber > 0 ? String(row.trackNumber).padStart(2, "0") : ""
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.detail !== "" && row.kind === OnDeviceModel.TrackRow
                              ? row.label + "  ·  " + row.detail
                              : row.detail !== "" ? row.label + "  (" + row.detail + ")" : row.label
                        font.bold: row.kind === OnDeviceModel.ArtistRow
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                    }
                    Tag {
                        objectName: "notInLibraryTag"
                        visible: row.kind === OnDeviceModel.TrackRow ? !row.inLibrary : row.notInLibraryCount > 0
                        text: row.kind === OnDeviceModel.TrackRow ? qsTr("not in library")
                                                                   : qsTr("%1 not in library").arg(row.notInLibraryCount)
                        color: Theme.accent
                    }
                    Tag {
                        visible: row.kind === OnDeviceModel.TrackRow && row.synced
                        text: qsTr("synced")
                        color: Theme.textMuted
                    }
                    Label {
                        visible: row.kind === OnDeviceModel.TrackRow
                        text: row.codec
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    Label {
                        visible: row.kind === OnDeviceModel.TrackRow && row.durationMs > 0
                        text: root.formatDuration(row.durationMs)
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    Label {
                        text: row.kind === OnDeviceModel.TrackRow ? row.sizeText : root.tracksText(row.trackCount) + " · " + row.sizeText
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
