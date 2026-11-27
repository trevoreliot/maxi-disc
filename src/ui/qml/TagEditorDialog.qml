// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Edits the tags of one or more tracks (TagEditor does the reading and writing).
// With several tracks, fields whose value differs show "(multiple values)" and stay as
// they are unless typed into; title and track number are per-track, so they're hidden.
// Only changed fields are written. Enter saves, Esc cancels.
Dialog {
    id: root

    property string subtitle
    readonly property bool multiple: tagEditor.trackCount > 1
    readonly property alias editor: tagEditor

    // Saved: `albumId` is the album all the tracks are in now (0 = several).
    signal saved(int albumId)

    function edit(paths: list<string>, label: string) {
        subtitle = label;
        tagEditor.paths = paths;
        open();
    }

    // {field: value} for the fields the user changed.
    function changes(): var {
        const out = {};
        const texts = [titleField, artistField, albumField, albumArtistField, genreField, yearField,
                       trackNumberField, trackTotalField, discNumberField, discTotalField,
                       composerField, commentField];
        for (const f of texts) {
            if (f.visible && f.changed)
                out[f.field] = f.text.trim();
        }
        if (compilationBox.changed)
            out.compilation = compilationBox.checkState === Qt.Checked;
        return out;
    }

    function save() {
        if (!tagEditor.loading && !tagEditor.saving)
            tagEditor.apply(changes());
    }

    modal: true
    focus: true // keys go to the dialog, not the KeyRouter
    // A stray click outside mustn't throw away the edits; nothing closes it mid-save.
    closePolicy: tagEditor.saving ? Popup.NoAutoClose : Popup.CloseOnEscape
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, Overlay.overlay ? Overlay.overlay.width - 2 * Theme.spaceXL : 620)
    padding: Theme.spaceLG
    title: multiple ? qsTr("Edit tags · %1 tracks").arg(tagEditor.trackCount) : qsTr("Edit tags")

    onOpened: (multiple ? artistField : titleField).forceActiveFocus()
    onClosed: tagEditor.paths = []

    TagEditor {
        id: tagEditor
        onFinished: (ok, message, albumId) => {
            if (!ok)
                return; // errorString shows what went wrong; the dialog stays open
            root.saved(albumId);
            root.close();
        }
    }

    // A text field for one tag. `changed` = the user changed it from what was loaded.
    component TagField: TextField {
        id: tf

        required property string field
        readonly property bool mixed: tagEditor.mixedFields.includes(field)
        property string initial
        readonly property bool changed: text.trim() !== initial

        function reset() {
            initial = tagEditor.values[field] ?? "";
            text = initial;
        }

        objectName: field + "Field"
        Layout.fillWidth: true
        enabled: !tagEditor.loading && !tagEditor.saving
        placeholderText: mixed ? qsTr("(multiple values)") : ""
        selectByMouse: true
        onAccepted: root.save()

        Connections {
            target: tagEditor
            function onValuesChanged() {
                tf.reset();
            }
        }
    }

    component NumberField: TagField {
        Layout.fillWidth: false
        Layout.preferredWidth: 64
        horizontalAlignment: Text.AlignRight
        inputMethodHints: Qt.ImhDigitsOnly
        validator: RegularExpressionValidator {
            regularExpression: /\d{0,4}/
        }
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
            objectName: "saveButton"
            text: tagEditor.saving ? qsTr("Saving…") : qsTr("Save")
            highlighted: true
            enabled: !tagEditor.loading && !tagEditor.saving && tagEditor.trackCount > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: root.save()
        }
        Button {
            text: qsTr("Cancel")
            enabled: !tagEditor.saving
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: root.close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceMD

        Label {
            Layout.fillWidth: true
            text: root.subtitle
            color: Theme.textMuted
            elide: Text.ElideMiddle
            visible: text !== ""
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: Theme.spaceMD
            rowSpacing: Theme.spaceSM

            FieldLabel { text: qsTr("Title"); visible: !root.multiple }
            TagField {
                id: titleField
                field: "title"
                visible: !root.multiple
                Layout.columnSpan: 3
            }

            FieldLabel { text: qsTr("Artist") }
            TagField { id: artistField; field: "artist"; Layout.columnSpan: 3 }

            FieldLabel { text: qsTr("Album") }
            TagField { id: albumField; field: "album"; Layout.columnSpan: 3 }

            FieldLabel { text: qsTr("Album artist") }
            TagField { id: albumArtistField; field: "albumArtist"; Layout.columnSpan: 3 }

            FieldLabel { text: qsTr("Genre") }
            TagField { id: genreField; field: "genre" }
            FieldLabel { text: qsTr("Year") }
            NumberField { id: yearField; field: "year" }

            FieldLabel { text: qsTr("Track") }
            RowLayout {
                spacing: Theme.spaceSM
                NumberField { id: trackNumberField; field: "trackNumber"; visible: !root.multiple }
                Label { text: root.multiple ? qsTr("total") : qsTr("of"); color: Theme.textMuted }
                NumberField { id: trackTotalField; field: "trackTotal" }
                Item { Layout.fillWidth: true }
            }
            FieldLabel { text: qsTr("Disc") }
            RowLayout {
                spacing: Theme.spaceSM
                NumberField { id: discNumberField; field: "discNumber" }
                Label { text: qsTr("of"); color: Theme.textMuted }
                NumberField { id: discTotalField; field: "discTotal" }
            }

            FieldLabel { text: qsTr("Composer") }
            TagField { id: composerField; field: "composer"; Layout.columnSpan: 3 }

            FieldLabel { text: qsTr("Comment") }
            TagField { id: commentField; field: "comment"; Layout.columnSpan: 3 }

            Item { implicitWidth: 1 }
            CheckBox {
                id: compilationBox

                readonly property bool mixed: tagEditor.mixedFields.includes("compilation")
                property int initialState: Qt.Unchecked
                readonly property bool changed: checkState !== initialState

                objectName: "compilationBox"
                // Fusion fills the box with `base`: the window's inset colour is too dark here.
                palette.base: Theme.surfaceElevated
                Layout.columnSpan: 3
                text: qsTr("Part of a compilation (various artists)")
                enabled: !tagEditor.loading && !tagEditor.saving
                focusPolicy: Qt.StrongFocus

                Connections {
                    target: tagEditor
                    function onValuesChanged() {
                        compilationBox.tristate = compilationBox.mixed;
                        compilationBox.initialState = compilationBox.mixed ? Qt.PartiallyChecked
                            : (tagEditor.values.compilation ? Qt.Checked : Qt.Unchecked);
                        compilationBox.checkState = compilationBox.initialState;
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSM
            visible: tagEditor.loading || tagEditor.saving || tagEditor.errorString !== ""

            BusyIndicator {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                running: tagEditor.loading || tagEditor.saving
                visible: running
            }
            Label {
                objectName: "tagError"
                Layout.fillWidth: true
                text: tagEditor.errorString !== "" ? tagEditor.errorString
                    : tagEditor.loading ? qsTr("Reading tags…") : qsTr("Saving…")
                color: tagEditor.errorString !== "" ? Theme.errorText : Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }
}
