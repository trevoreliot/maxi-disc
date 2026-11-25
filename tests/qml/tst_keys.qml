// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtTest
import MusicMdManager

// KeyRouter: which keys become which actions, and when it stays out of the way
// (text fields, popups, open combo boxes). Real key events through the window.
TestCase {
    id: tc
    name: "KeyRouter"
    when: windowShown
    width: 400
    height: 300
    visible: true

    KeyRouter {
        id: router
        window: tc.Window.window
    }
    SignalSpy {
        id: spy
        target: router
        signalName: "triggered"
    }

    Item {
        id: plain
        focus: true
    }
    TextField {
        id: field
        y: 40
        width: 200
    }
    ComboBox {
        id: combo
        y: 90
        model: ["a", "b", "c"]
    }
    Popup {
        id: popup
        focus: true
        width: 100
        height: 60
        contentItem: Item {
            focus: true
        }
    }

    // A popup that opts in to the app's bindings (like the artwork finder).
    Popup {
        id: routedPopup
        focus: true
        width: 200
        height: 80
        contentItem: Item {
            property bool routeKeys: true
            focus: true

            TextField {
                id: routedField
                width: 150
            }
        }
    }

    function init() {
        popup.close();
        routedPopup.close();
        combo.popup.close();
        field.text = "";
        plain.forceActiveFocus();
        spy.clear();
    }

    // Sends one key press (and release) and returns the action, or -1 if none fired.
    function actionOf(key, modifiers) {
        spy.clear();
        if (typeof key === "string")
            keyClick(key, modifiers === undefined ? Qt.NoModifier : modifiers);
        else
            keyClick(key, modifiers === undefined ? Qt.NoModifier : modifiers);
        return spy.count === 0 ? -1 : spy.signalArguments[0][0];
    }

    function test_bindings_data() {
        return [
            { tag: "h", key: "h", action: KeyRouter.MoveLeft },
            { tag: "j", key: "j", action: KeyRouter.MoveDown },
            { tag: "k", key: "k", action: KeyRouter.MoveUp },
            { tag: "l", key: "l", action: KeyRouter.MoveRight },
            { tag: "left", key: Qt.Key_Left, action: KeyRouter.MoveLeft },
            { tag: "down", key: Qt.Key_Down, action: KeyRouter.MoveDown },
            { tag: "up", key: Qt.Key_Up, action: KeyRouter.MoveUp },
            { tag: "right", key: Qt.Key_Right, action: KeyRouter.MoveRight },
            { tag: "return", key: Qt.Key_Return, action: KeyRouter.Activate },
            { tag: "enter", key: Qt.Key_Enter, action: KeyRouter.Activate },
            { tag: "escape", key: Qt.Key_Escape, action: KeyRouter.Back },
            { tag: "slash", key: "/", action: KeyRouter.FocusSearch },
            { tag: "ctrl+f", key: Qt.Key_F, mods: Qt.ControlModifier, action: KeyRouter.FocusSearch },
            { tag: "q", key: "q", action: KeyRouter.ToggleQueue },
            { tag: "space", key: Qt.Key_Space, action: KeyRouter.PlayPause },
            { tag: "ctrl+left", key: Qt.Key_Left, mods: Qt.ControlModifier, action: KeyRouter.PreviousTrack },
            { tag: "ctrl+right", key: Qt.Key_Right, mods: Qt.ControlModifier, action: KeyRouter.NextTrack },
            { tag: "e", key: "e", action: KeyRouter.EditTags },
            { tag: "E", key: "E", action: KeyRouter.EditAllTags },
            { tag: "v", key: "v", action: KeyRouter.ToggleMark },
            { tag: "a", key: "a", action: KeyRouter.FindArt },
            { tag: "A", key: "A", action: KeyRouter.FindMissingArt },
            { tag: "s", key: "s", action: KeyRouter.Skip },
            { tag: "r", key: "r", action: KeyRouter.Retry },
            { tag: "m", key: "m", action: KeyRouter.MountToggle },
            { tag: "i", key: "i", action: KeyRouter.AddToDevice },
            { tag: "I", key: "I", action: KeyRouter.SendQueueToDevice },
            { tag: "M", key: "M", action: KeyRouter.CycleRecordMode },
            { tag: "d", key: "d", action: KeyRouter.DeleteMdTracks },
            { tag: "D", key: "D", action: KeyRouter.EraseMd },
            { tag: "J", key: "J", action: KeyRouter.MoveMdTrackDown },
            { tag: "K", key: "K", action: KeyRouter.MoveMdTrackUp },
            { tag: "x", key: "x", action: KeyRouter.ExportRemove },
            { tag: "p", key: "p", action: KeyRouter.ExportPlan },
            { tag: "S", key: "S", action: KeyRouter.ExportSync },
            { tag: "delete", key: Qt.Key_Delete, action: KeyRouter.ExportRemove },
            { tag: "o", key: "o", action: KeyRouter.ToggleOnDevice },
            { tag: "n", key: "n", action: KeyRouter.RenameTrack },
            { tag: "N", key: "N", action: KeyRouter.RenameDisc },
            { tag: "R", key: "R", action: KeyRouter.RefreshDevice },
            { tag: "X", key: "X", action: KeyRouter.ClearQueue },
            { tag: "comma", key: ",", action: KeyRouter.ContextMenu },
            { tag: "menu", key: Qt.Key_Menu, action: KeyRouter.ContextMenu },
            { tag: "shift+f10", key: Qt.Key_F10, mods: Qt.ShiftModifier, action: KeyRouter.ContextMenu },
            { tag: "f10", key: Qt.Key_F10, action: -1 },
            { tag: "unbound z", key: "z", action: -1 },
            { tag: "ctrl+j", key: Qt.Key_J, mods: Qt.ControlModifier, action: -1 },
            { tag: "alt+h", key: Qt.Key_H, mods: Qt.AltModifier, action: -1 },
        ];
    }

    function test_bindings(data) {
        compare(actionOf(data.key, data.mods), data.action);
    }

    function test_textFieldTypesNormally() {
        field.forceActiveFocus();
        verify(field.activeFocus);
        for (const ch of ["h", "j", "k", "l", "/", "q"])
            compare(actionOf(ch), -1);
        compare(actionOf(Qt.Key_Space), -1);
        compare(field.text, "hjkl/q ");
        compare(actionOf(Qt.Key_Left), -1); // moves the text cursor
        compare(actionOf(Qt.Key_Return), -1);
        compare(actionOf(Qt.Key_F, Qt.ControlModifier), KeyRouter.FocusSearch);
        compare(actionOf(Qt.Key_Escape), KeyRouter.LeaveTextInput);
    }

    function test_readOnlyFieldIsNotTextInput() {
        field.readOnly = true;
        field.forceActiveFocus();
        compare(actionOf("j"), KeyRouter.MoveDown);
        compare(field.text, "");
        field.readOnly = false;
    }

    function test_popupOwnsKeys() {
        popup.open();
        tryCompare(popup, "opened", true);
        compare(actionOf("j"), -1);
        compare(actionOf(Qt.Key_Escape), -1); // Qt closes the popup instead
        tryCompare(popup, "visible", false);
        plain.forceActiveFocus();
        compare(actionOf("j"), KeyRouter.MoveDown);
    }

    function test_routedPopupGetsBindings() {
        routedPopup.open();
        tryCompare(routedPopup, "opened", true);
        compare(actionOf("j"), KeyRouter.MoveDown);
        compare(actionOf("s"), KeyRouter.Skip);
        compare(actionOf(Qt.Key_Return), KeyRouter.Activate);
        compare(actionOf(Qt.Key_Escape), KeyRouter.Back); // the popup doesn't close itself
        verify(routedPopup.visible);
        // A text field inside it still types normally.
        routedField.forceActiveFocus();
        compare(actionOf("j"), -1);
        compare(routedField.text, "j");
        routedField.text = "";
        routedPopup.close();
        tryCompare(routedPopup, "visible", false);
    }

    function test_openComboOwnsKeys() {
        combo.forceActiveFocus();
        compare(actionOf("j"), KeyRouter.MoveDown); // closed: the router still drives
        combo.popup.open();
        tryCompare(combo.popup, "opened", true);
        compare(actionOf(Qt.Key_Down), -1);
        compare(actionOf(Qt.Key_Escape), -1);
        tryCompare(combo.popup, "visible", false);
    }
}
