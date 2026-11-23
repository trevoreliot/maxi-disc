// SPDX-License-Identifier: GPL-3.0-or-later
#include "keyrouter.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>

#include <array>

namespace {

// Printable keys, matched on the typed character (so they follow the keyboard layout).
// Only when no text field has focus, and without Ctrl/Alt/Meta.
struct TextBinding
{
    char16_t ch;
    KeyRouter::Action action;
};
constexpr std::array kTextBindings{
    TextBinding{u'h', KeyRouter::MoveLeft},
    TextBinding{u'j', KeyRouter::MoveDown},
    TextBinding{u'k', KeyRouter::MoveUp},
    TextBinding{u'l', KeyRouter::MoveRight},
    TextBinding{u'/', KeyRouter::FocusSearch},
    TextBinding{u'q', KeyRouter::ToggleQueue},
    TextBinding{u' ', KeyRouter::PlayPause},
    TextBinding{u'e', KeyRouter::EditTags},
    TextBinding{u'E', KeyRouter::EditAllTags},
    TextBinding{u'v', KeyRouter::ToggleMark},
    TextBinding{u'a', KeyRouter::FindArt},
    TextBinding{u'A', KeyRouter::FindMissingArt},
    TextBinding{u's', KeyRouter::Skip},
    TextBinding{u'r', KeyRouter::Retry},
    TextBinding{u'm', KeyRouter::MountToggle},
    TextBinding{u'i', KeyRouter::ExportAdd},
    TextBinding{u'x', KeyRouter::ExportRemove},
    TextBinding{u'p', KeyRouter::ExportPlan},
    TextBinding{u'S', KeyRouter::ExportSync},
    TextBinding{u'o', KeyRouter::ToggleOnDevice},
    TextBinding{u'n', KeyRouter::RenameTrack},
    TextBinding{u'N', KeyRouter::RenameDisc},
    TextBinding{u'R', KeyRouter::RefreshDevice},
};

// Keys matched on key code + exact modifiers, in a context: outside text fields, inside
// one, or both.
enum class Where { Outside, InText, Both };
struct KeyBinding
{
    Qt::Key key;
    Qt::KeyboardModifiers modifiers;
    KeyRouter::Action action;
    Where where;
};
const std::array kKeyBindings{
    KeyBinding{Qt::Key_F, Qt::ControlModifier, KeyRouter::FocusSearch, Where::Both},
    KeyBinding{Qt::Key_Escape, Qt::NoModifier, KeyRouter::LeaveTextInput, Where::InText},
    KeyBinding{Qt::Key_Escape, Qt::NoModifier, KeyRouter::Back, Where::Outside},
    KeyBinding{Qt::Key_Left, Qt::NoModifier, KeyRouter::MoveLeft, Where::Outside},
    KeyBinding{Qt::Key_Down, Qt::NoModifier, KeyRouter::MoveDown, Where::Outside},
    KeyBinding{Qt::Key_Up, Qt::NoModifier, KeyRouter::MoveUp, Where::Outside},
    KeyBinding{Qt::Key_Right, Qt::NoModifier, KeyRouter::MoveRight, Where::Outside},
    KeyBinding{Qt::Key_Return, Qt::NoModifier, KeyRouter::Activate, Where::Outside},
    KeyBinding{Qt::Key_Enter, Qt::NoModifier, KeyRouter::Activate, Where::Outside},
    KeyBinding{Qt::Key_Delete, Qt::NoModifier, KeyRouter::ExportRemove, Where::Outside},
    KeyBinding{Qt::Key_Left, Qt::ControlModifier, KeyRouter::PreviousTrack, Where::Outside},
    KeyBinding{Qt::Key_Right, Qt::ControlModifier, KeyRouter::NextTrack, Where::Outside},
};

} // namespace

KeyRouter::KeyRouter(QObject *parent)
    : QObject(parent)
{
}

KeyRouter::~KeyRouter()
{
    if (m_window)
        m_window->removeEventFilter(this);
}

void KeyRouter::setWindow(QQuickWindow *window)
{
    if (window == m_window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    Q_EMIT windowChanged();
}

KeyRouter::Action KeyRouter::actionFor(int key, Qt::KeyboardModifiers modifiers, const QString &text,
                                       bool inTextInput)
{
    // The keypad flag only says where Enter/arrows came from: treat them like the main keys.
    const Qt::KeyboardModifiers mods = modifiers & ~Qt::KeypadModifier;
    for (const KeyBinding &b : kKeyBindings) {
        const bool context = b.where == Where::Both || (b.where == Where::InText) == inTextInput;
        if (b.key == key && b.modifiers == mods && context)
            return b.action;
    }
    if (inTextInput || text.size() != 1 || (mods & ~Qt::ShiftModifier) != Qt::NoModifier)
        return NoAction;
    for (const TextBinding &b : kTextBindings) {
        if (text.at(0) == QChar(b.ch))
            return b.action;
    }
    return NoAction;
}

bool KeyRouter::isEditableText(const QQuickItem *item)
{
    return item && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        && !item->property("readOnly").toBool();
}

bool KeyRouter::ownsKeysItself(const QQuickItem *item)
{
    if (!item)
        return false;
    // An open ComboBox keeps focus itself and handles its popup's keys.
    if (item->inherits("QQuickComboBox")) {
        const auto *popup = item->property("popup").value<QObject *>();
        if (popup && popup->property("visible").toBool())
            return true;
    }
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (p->inherits("QQuickPopupItem"))
            return !wantsRoutedKeys(item);
    }
    return false;
}

bool KeyRouter::wantsRoutedKeys(const QQuickItem *item)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (p->property("routeKeys").toBool())
            return true;
        if (p->inherits("QQuickPopupItem"))
            return false;
    }
    return false;
}

bool KeyRouter::eventFilter(QObject *watched, QEvent *event)
{
    const bool press = event->type() == QEvent::KeyPress;
    // Keys with modifiers (Ctrl+F) first come as a ShortcutOverride: accepting it keeps them
    // from triggering a shortcut somewhere else (e.g. a text field's actions), so the
    // KeyPress follows.
    if ((!press && event->type() != QEvent::ShortcutOverride) || watched != m_window)
        return QObject::eventFilter(watched, event);

    QQuickItem *focus = m_window->activeFocusItem();
    if (ownsKeysItself(focus))
        return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    const Action action = actionFor(key->key(), key->modifiers(), key->text(), isEditableText(focus));
    if (action == NoAction)
        return false;
    event->accept();
    if (press)
        Q_EMIT triggered(action);
    return true;
}
