// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Header on every screen: back to the menu + screen title, search (/ or Ctrl+F),
// scan status, queue toggle, and the app menu (folders, rescan, ReplayGain).
ToolBar {
    id: root

    property string title
    property bool showBack: false
    property bool queueOpen: false
    property bool searchEnabled: true
    readonly property alias searchText: searchField.text
    readonly property alias searchHasFocus: searchField.activeFocus

    signal backRequested()
    signal queueToggled()
    signal addFolderRequested()
    signal listenBrainzRequested() // the ListenBrainz dialog (M14)
    signal discordRequested() // the Discord dialog (M15)
    signal searchAccepted() // Enter in the search field

    function focusSearch() {
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function clearSearch() {
        searchField.text = "";
    }

    implicitHeight: 44
    background: Rectangle {
        color: Theme.surfaceChrome

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceSM
        anchors.rightMargin: Theme.spaceSM
        spacing: Theme.spaceSM

        ToolButton {
            objectName: "backButton"
            visible: root.showBack
            text: qsTr("‹ Menu")
            focusPolicy: Qt.NoFocus
            onClicked: root.backRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Back to the main menu (Esc)")
        }

        Label {
            objectName: "screenTitle"
            Layout.leftMargin: root.showBack ? 0 : Theme.spaceSM
            text: root.title
            font.pixelSize: Theme.fontHeading
            font.bold: true
        }

        Item {
            Layout.fillWidth: true
        }

        TextField {
            id: searchField
            objectName: "searchField"
            Layout.preferredWidth: 300
            placeholderText: qsTr("Search…  ( / )")
            enabled: root.searchEnabled
            selectByMouse: true
            onAccepted: root.searchAccepted()
        }

        BusyIndicator {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            running: Library.scanning
            visible: running
        }
        Label {
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
            text: Library.scanning
                ? (Library.scanTotal > 0 ? qsTr("Scanning %1 / %2").arg(Library.scanProcessed).arg(Library.scanTotal)
                                         : qsTr("Checking for changes…"))
                : qsTr("%1 tracks").arg(Library.trackCount)
        }

        ToolButton {
            objectName: "queueButton"
            text: qsTr("Queue (%1)").arg(Playback.queue.count)
            focusPolicy: Qt.NoFocus
            checkable: true
            checked: root.queueOpen
            onClicked: root.queueToggled()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Show or hide the queue (q)")
        }

        ToolButton {
            text: "☰"
            focusPolicy: Qt.NoFocus
            onClicked: appMenu.open()

            Menu {
                id: appMenu
                y: parent.height

                MenuItem {
                    text: qsTr("Add music folder…")
                    onTriggered: root.addFolderRequested()
                }
                MenuItem {
                    text: qsTr("Rescan library")
                    enabled: Library.folders.length > 0
                    onTriggered: Library.rescan()
                }
                MenuItem {
                    objectName: "listenbrainzMenuItem"
                    text: qsTr("ListenBrainz…")
                    onTriggered: root.listenBrainzRequested()
                }
                MenuItem {
                    objectName: "discordMenuItem"
                    text: qsTr("Discord…")
                    onTriggered: root.discordRequested()
                }
                MenuSeparator {}
                Menu {
                    title: qsTr("ReplayGain")

                    MenuItem {
                        text: qsTr("Album (keeps an album's dynamics)")
                        checkable: true
                        checked: Playback.replayGainMode === 2
                        onTriggered: Playback.replayGainMode = 2
                    }
                    MenuItem {
                        text: qsTr("Track")
                        checkable: true
                        checked: Playback.replayGainMode === 1
                        onTriggered: Playback.replayGainMode = 1
                    }
                    MenuItem {
                        text: qsTr("Off")
                        checkable: true
                        checked: Playback.replayGainMode === 0
                        onTriggered: Playback.replayGainMode = 0
                    }
                }
            }
        }
    }
}
