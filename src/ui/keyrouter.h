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
        ExportAdd,      // Export: add the browse row under the cursor to the device's selection
        ExportRemove,   // Export: remove the selection entry under the cursor
        ExportPlan,     // Export: plan a sync of the selected device
        ExportSync,     // Export: run the plan that's shown
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
