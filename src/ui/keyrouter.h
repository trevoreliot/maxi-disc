// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

class QQuickItem;

// The app's keyboard bindings, in one place. Filters a window's key presses and turns the
// bound ones into semantic actions (`triggered`); views never look at raw keys.
//
// - While an editable text field has focus, only Ctrl+F (FocusSearch) and Esc
//   (LeaveTextInput) are taken; everything else types normally.
// - While focus is inside a popup (menu, dialog) or an open ComboBox, nothing is taken:
//   Qt handles the popup's own keys (including Esc to close it). Popups must therefore
//   take focus when they open (`focus: true`), or keys go to the views underneath.
//   Exception: a popup whose focused content sits inside an item with `routeKeys: true`
//   (e.g. the artwork finder) gets the normal bindings; Main forwards them to it.
// - Otherwise: h/j/k/l and the arrow keys move, Enter activates, Esc backs out, `/` and
//   Ctrl+F focus search, `q` toggles the queue, Space plays/pauses, Ctrl+←/→ skip tracks,
//   `a` / `A` find cover art (current album / all missing), `s` / `r` skip / retry (finder),
//   `m` mounts / unmounts the selected device (Export).
//
// New bindings go in kTextBindings / kKeyBindings in keyrouter.cpp.
class KeyRouter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)

public:
    enum Action {
        NoAction,
        MoveLeft,
        MoveDown,
        MoveUp,
        MoveRight,
        Activate,
        Back,
        FocusSearch,
        LeaveTextInput,
        ToggleQueue,
        PlayPause,
        NextTrack,
        PreviousTrack,
        EditTags,    // tag editor for the marked tracks, else the cursor track
        EditAllTags, // ... for the marked tracks, else all listed tracks
        ToggleMark,  // mark/unmark the cursor track (album page)
        FindArt,        // artwork finder for the current album
        FindMissingArt, // artwork finder for every album without art
        Skip,           // artwork finder: skip this album
        Retry,          // artwork finder: search again
        MountToggle,    // Export: mount / unmount the selected device
        AddToDevice,    // Library / Export: send what's under the cursor (or marked) to the selected device
        ExportRemove,   // Export: remove the selection entry under the cursor
        ExportPlan,     // Export: plan a sync of the selected device
        ExportSync,     // Export: run the plan that's shown
        ToggleOnDevice, // Export: right pane "Tracks to Export" ↔ "On Device"
        RenameTrack,    // Export, MiniDisc: rename the track under the cursor
        RenameDisc,     // Export, MiniDisc: rename the disc
        RefreshDevice,  // Export: read the MiniDisc (or the device) again
        ClearQueue,     // Export, MiniDisc: empty the "to record" list
        SendQueueToDevice, // anywhere: send the play queue to the selected device
        CycleRecordMode,   // Export, MiniDisc: SP → LP2 → LP4
        DeleteMdTracks,    // Export, MiniDisc: delete the marked tracks (or the cursor's), after asking
        EraseMd,           // Export, MiniDisc: wipe the disc, after asking
        MoveMdTrackDown,   // Export, MiniDisc: move the cursor's track down a place
        MoveMdTrackUp,     // Export, MiniDisc: ... up
        ContextMenu,       // the right-click menu for what the cursor is on (`,`, Menu, Shift+F10)
        FlipCase,          // MD Visualizer: turn the case that's out to its back (track list) and back
        Eject,             // MD Visualizer: the player's OPEN slide (stop, the cartridge back in its case)
        PlayCase,          // MD Visualizer: the case that's out into the player, from its first track
        VolumeDown,        // anywhere: a step quieter
        VolumeUp,          // anywhere: a step louder
    };
    Q_ENUM(Action)

    explicit KeyRouter(QObject *parent = nullptr);
    ~KeyRouter() override;

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);

    // The action bound to a key press; `inTextInput` = an editable text field has focus.
    static Action actionFor(int key, Qt::KeyboardModifiers modifiers, const QString &text, bool inTextInput);

Q_SIGNALS:
    void triggered(KeyRouter::Action action);
    void windowChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool isEditableText(const QQuickItem *item);
    static bool ownsKeysItself(const QQuickItem *item);
    static bool wantsRoutedKeys(const QQuickItem *item);

    QPointer<QQuickWindow> m_window;
};
