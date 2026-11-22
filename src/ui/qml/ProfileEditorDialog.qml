// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sets up or edits a device's sync profile (ProfileEditor does the work): start from a
// built-in profile, then the music folder, the folder layout (with a live example path),
// which formats the device plays as they are, what everything else becomes, and artwork.
// Enter in a text field saves, Esc cancels. The dialog owns its keys (text fields).
Dialog {
    id: root

    readonly property alias profileEditor: editor

    function edit(deviceId: string) {
        editor.load(deviceId);
        open();
    }

    function save() {
        if (editor.canSave && editor.save())
            close();
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 640)
    height: Math.min(implicitHeight, Overlay.overlay ? Overlay.overlay.height - 2 * Theme.spaceXL : implicitHeight)
    padding: Theme.spaceLG
    title: editor.isNew ? qsTr("Set up %1").arg(editor.deviceName) : qsTr("Edit profile · %1").arg(editor.deviceName)

    onOpened: nameField.forceActiveFocus()

    ProfileEditor {
        id: editor
    }

    component FieldLabel: Label {
        color: Theme.textSecondary
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    header: Label {
        text: root.title
        padding: Theme.spaceLG
        bottomPadding: 0
        font.pixelSize: Theme.fontTitle
        font.bold: true
        elide: Text.ElideRight
    }

    footer: DialogButtonBox {
        Button {
            objectName: "removeProfileButton"
            text: qsTr("Remove profile")
            visible: !editor.isNew
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: {
                if (editor.remove())
                    root.close();
            }
        }
        Button {
            objectName: "saveProfileButton"
            text: qsTr("Save")
            highlighted: true
            enabled: editor.canSave
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: root.save()
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: root.close()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: Theme.spaceMD

            RowLayout {
                spacing: Theme.spaceSM
                Label {
                    text: qsTr("Start from")
                    color: Theme.textSecondary
                }
                Button {
                    objectName: "presetFlashButton"
                    text: qsTr("USB flash drive")
                    highlighted: editor.presetId === "usb-flash"
                    focusPolicy: Qt.NoFocus
                    onClicked: editor.applyPreset("usb-flash")
                }
                Button {
                    objectName: "presetDapButton"
                    text: qsTr("Generic DAP")
                    highlighted: editor.presetId === "generic-dap"
                    focusPolicy: Qt.NoFocus
                    onClicked: editor.applyPreset("generic-dap")
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spaceMD
                rowSpacing: Theme.spaceSM

                FieldLabel { text: qsTr("Name") }
                TextField {
                    id: nameField
                    objectName: "profileNameField"
                    Layout.fillWidth: true
                    text: editor.name
                    onTextEdited: editor.name = text
                    onAccepted: root.save()
                }

                FieldLabel { text: qsTr("Music folder") }
                TextField {
                    objectName: "musicDirField"
                    Layout.fillWidth: true
                    text: editor.musicDir
                    placeholderText: qsTr("(the device's root)")
                    onTextEdited: editor.musicDir = text
                    onAccepted: root.save()
                }

                FieldLabel {
                    text: qsTr("Folder layout")
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                    Layout.topMargin: Theme.spaceXS
                }
                // Field, help, error and example in one cell: a hidden item in a GridLayout
                // would shift every cell after it.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXS

                    TextField {
                        objectName: "layoutField"
                        Layout.fillWidth: true
                        text: editor.pathLayout
                        font.family: Theme.fontMono
                        onTextEdited: editor.pathLayout = text
                        onAccepted: root.save()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("{albumartist} {artist} {album} {title} {year} {track:02} {disc} · [ … ] is left out when empty")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "layoutError"
                        Layout.fillWidth: true
                        visible: editor.layoutError !== ""
                        text: editor.layoutError
                        color: Theme.errorText
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: exampleColumn.implicitHeight + 2 * Theme.spaceSM
                        color: Theme.surfaceInset
                        radius: Theme.radiusSmall
                        border.color: Theme.border

                        ColumnLayout {
                            id: exampleColumn
                            anchors.fill: parent
                            anchors.margins: Theme.spaceSM
                            spacing: 2
                            Label {
                                objectName: "examplePath"
                                Layout.fillWidth: true
                                text: editor.example
                                font.family: Theme.fontMono
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                Layout.fillWidth: true
                                text: editor.exampleNote
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textMuted
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                FieldLabel {
                    text: qsTr("Plays as is")
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                    Layout.topMargin: Theme.spaceSM
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSM

                    Repeater {
                        model: editor.allCodecs
                        delegate: CheckBox {
                            required property string modelData
                            objectName: "codec_" + modelData
                            text: modelData
                            checked: editor.playableCodecs.includes(modelData)
                            onToggled: editor.setCodecEnabled(modelData, checked)
                        }
                    }
                }

                FieldLabel { text: qsTr("Convert the rest to") }
                ComboBox {
                    objectName: "transcodeBox"
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("MP3 V0 (VBR, ~245 kbps)"), value: "mp3_v0" },
                        { text: qsTr("MP3 320 kbps CBR (for players that mishandle VBR)"), value: "mp3_cbr_320" }
                    ]
                    currentIndex: indexOfValue(editor.transcodeTarget)
                    onActivated: editor.transcodeTarget = currentValue
                }

                FieldLabel { text: qsTr("Embedded artwork") }
                ComboBox {
                    objectName: "artworkBox"
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("Keep as is"), value: "keep" },
                        { text: qsTr("Shrink to 500 px JPEG (most DAPs)"), value: "shrink_500" },
                        { text: qsTr("Remove"), value: "strip" }
                    ]
                    currentIndex: indexOfValue(editor.artworkPolicy)
                    onActivated: editor.artworkPolicy = currentValue
                }
            }

            Label {
                Layout.fillWidth: true
                visible: editor.errorString !== ""
                text: editor.errorString
                color: Theme.errorText
                wrapMode: Text.Wrap
            }
        }
    }
}
