// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Screen 2, Export (docs/ui-draft/export.jpeg). A shell for now: device detection (M8),
// USB / DAP sync (M9) and MiniDisc (M10–M12) fill it in. The browse pane is real, so the
// layout and keyboard model are already in place.
Item {
    id: root

    property bool active: false // this screen has keyboard focus

    readonly property alias browse: browsePane

    signal interacted()

    function handleAction(action: int): bool {
        switch (action) {
        case KeyRouter.MoveDown:
            browsePane.moveSelection(1);
            return true;
        case KeyRouter.MoveUp:
            browsePane.moveSelection(-1);
            return true;
        case KeyRouter.Activate:
            browsePane.commit();
            return true;
        case KeyRouter.MoveLeft:
            return true;
        }
        return false; // MoveRight: nothing to the right yet (the queue, if open)
    }

    function back(): bool {
        return false;
    }

    // A device action that isn't built yet: visibly inert.
    component StubButton: Button {
        enabled: false
        opacity: 0.45
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
    }
    component SectionTitle: Label {
        font.pixelSize: Theme.fontHeading
        font.bold: true
        color: Theme.textPrimary
    }
    component Badge: Label {
        leftPadding: 6
        rightPadding: 6
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1
        color: Theme.accent
        background: Rectangle {
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: Theme.accentMuted
        }
    }

    SplitView {
        anchors.fill: parent

        handle: Rectangle {
            implicitWidth: 1
            color: Theme.border
        }

        // Left: devices, then the browse pane.
        ColumnLayout {
            SplitView.preferredWidth: Math.round(root.width * 0.25)
            SplitView.minimumWidth: 190
            SplitView.maximumWidth: 380
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                color: Theme.surface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMD
                    spacing: Theme.spaceSM

                    SectionTitle {
                        text: qsTr("Available Devices")
                    }
                    Label {
                        objectName: "noDevicesLabel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: qsTr("No devices detected.\nUSB drives, DAPs and NetMD recorders will show up here.")
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            BrowsePane {
                id: browsePane
                objectName: "exportBrowse"
                Layout.fillWidth: true
                Layout.fillHeight: true
                showPlaylistTools: false
                paneActive: root.active
                onInteracted: root.interacted()
            }
        }

        // Right: what will be exported, and the device's options.
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spaceLG
                spacing: Theme.spaceSM

                SectionTitle {
                    text: qsTr("Tracks to Export")
                }
                Badge {
                    text: qsTr("PREVIEW")
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spaceLG
                Layout.rightMargin: Theme.spaceLG
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                text: qsTr("Choosing music to copy to a USB drive or DAP, or to record to a MiniDisc, arrives with device support.")
                color: Theme.textMuted
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false // a child fills height; the row itself stays 180
                Layout.preferredHeight: 180
                Layout.margins: Theme.spaceLG
                spacing: Theme.spaceLG

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    text: qsTr("No device selected")
                    color: Theme.textSecondary
                }

                // Device options (lower right in the drawing): MiniDisc or USB / DAP.
                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spaceSM
                        spacing: Theme.spaceSM

                        TabBar {
                            id: optionTabs
                            Layout.fillWidth: true
                            TabButton {
                                text: qsTr("MD options")
                                focusPolicy: Qt.NoFocus
                            }
                            TabButton {
                                text: qsTr("USB / DAP")
                                focusPolicy: Qt.NoFocus
                            }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: optionTabs.currentIndex

                            GridLayout {
                                columns: 2
                                columnSpacing: Theme.spaceSM
                                rowSpacing: Theme.spaceSM
                                StubButton { text: qsTr("Wipe disc") }
                                StubButton { text: qsTr("Arrange tracks") }
                                StubButton { text: qsTr("Rename") }
                                StubButton { text: qsTr("Sync") }
                                Label {
                                    Layout.columnSpan: 2
                                    text: qsTr("NetMD support: M10–M12")
                                    font.pixelSize: Theme.fontMicro
                                    color: Theme.textMuted
                                }
                            }
                            GridLayout {
                                columns: 2
                                columnSpacing: Theme.spaceSM
                                rowSpacing: Theme.spaceSM
                                StubButton { text: qsTr("Plan sync") }
                                StubButton { text: qsTr("Sync") }
                                Label {
                                    Layout.columnSpan: 2
                                    text: qsTr("Device profiles: M8 · USB sync: M9")
                                    font.pixelSize: Theme.fontMicro
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
