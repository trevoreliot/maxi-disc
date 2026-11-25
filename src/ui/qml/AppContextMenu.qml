// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one right-click menu (Q1): built from a view's entries (see ContextMenus), shown by Main.
// Keys come through the KeyRouter (its list opts in with routeKeys): j / k / arrows move,
// l / Right / Enter open "Add to playlist", h / Left close it, Enter picks, Esc closes.
Menu {
    id: root

    property var closedCallback: null
    property bool fromKeyboard: false
    readonly property Menu openSubmenu: playlistMenu && playlistMenu.opened ? playlistMenu : null
    property Menu playlistMenu: null

    function show(item: Item, x: real, y: real, entries: var, closed: var, keyboard: bool) {
        if (opened)
            close();
        clearItems();
        closedCallback = closed;
        fromKeyboard = keyboard;
        // Nothing to show twice in a row, and no line at either end.
        const list = [];
        for (const e of entries) {
            if (e.separator && (list.length === 0 || list[list.length - 1].separator))
                continue;
            list.push(e);
        }
        while (list.length > 0 && list[list.length - 1].separator)
            list.pop();
        if (list.length === 0) {
            closedCallback = null;
            if (closed)
                closed();
            return;
        }
        for (const e of list) {
            if (e.separator)
                addItem(separatorComponent.createObject(null));
            else if (e.playlists)
                addPlaylistMenu(e);
            else
                addItem(itemComponent.createObject(null, { entry: e }));
        }
        popup(item, x, y);
        if (keyboard)
            currentIndex = nextEnabled(root, -1, 1);
    }

    function clearItems() {
        const sub = playlistMenu;
        playlistMenu = null;
        if (sub) {
            removeMenu(sub);
            sub.destroy();
        }
        while (count > 0) {
            const it = takeItem(0);
            if (it)
                it.destroy();
        }
    }

    function addPlaylistMenu(e: var) {
        const menu = playlistComponent.createObject(null, { entry: e, title: e.text });
        addMenu(menu);
        menu.enabled = e.enabled !== false;
        playlistMenu = menu;
        playlists.reload();
    }

    // Runs an entry after the menu is gone (a dialog it opens takes the focus cleanly).
    function run(fn: var) {
        if (fn)
            Qt.callLater(fn);
    }

    // A menu item (not a line) that can be picked.
    function pickable(menu: Menu, i: int): MenuItem {
        const it = menu.itemAt(i) as MenuItem;
        return it && it.enabled ? it : null;
    }

    function nextEnabled(menu: Menu, from: int, step: int): int {
        if (from < 0 && step < 0)
            from = menu.count; // up from nothing: the last item first
        for (let n = 1; n <= menu.count; ++n) {
            const i = (from + step * n + menu.count * n) % menu.count;
            if (pickable(menu, i))
                return i;
        }
        return -1;
    }

    function openSub(it: MenuItem) {
        it.click(); // a submenu item opens its menu
        if (playlistMenu)
            Qt.callLater(() => { if (root.playlistMenu) root.playlistMenu.currentIndex = root.nextEnabled(root.playlistMenu, -1, 1); });
    }

    // KeyRouter actions while the menu is open (Main gives it every one: nothing leaks behind).
    function handleAction(action: int): bool {
        const sub = openSubmenu;
        const menu = sub ?? root;
        const it = pickable(menu, menu.currentIndex);
        switch (action) {
        case KeyRouter.MoveDown:
        case KeyRouter.MoveUp:
            menu.currentIndex = nextEnabled(menu, menu.currentIndex, action === KeyRouter.MoveDown ? 1 : -1);
            break;
        case KeyRouter.MoveRight:
            if (!sub && it && it.subMenu)
                openSub(it);
            break;
        case KeyRouter.MoveLeft:
            if (sub) {
                sub.close();
                root.contentItem.forceActiveFocus();
            }
            break;
        case KeyRouter.Activate:
            if (it && it.subMenu && !sub)
                openSub(it);
            else if (it)
                it.click();
            break;
        case KeyRouter.Back:
            if (sub) {
                sub.close();
                root.contentItem.forceActiveFocus();
            } else {
                root.close();
            }
            break;
        }
        return true;
    }

    // The keys go through the KeyRouter only from inside the list (routeKeys).
    onOpened: contentItem.forceActiveFocus()

    onClosed: {
        const cb = closedCallback;
        closedCallback = null;
        if (cb)
            cb();
    }

    PlaylistListModel {
        id: playlists
    }

    contentItem: ListView {
        property bool routeKeys: true // the KeyRouter's keys, not the Menu's own
        focus: true
        implicitHeight: contentHeight
        model: root.contentModel
        interactive: Window.window ? contentHeight + root.topPadding + root.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: root.currentIndex
        ScrollIndicator.vertical: ScrollIndicator {}
    }

    Component {
        id: itemComponent

        MenuItem {
            id: menuItem
            required property var entry

            objectName: "contextItem"
            text: entry.text
            enabled: entry.enabled !== false
            onTriggered: root.run(entry.onTriggered)

            contentItem: RowLayout {
                spacing: Theme.spaceLG
                // A choice among a few (`checked` given): ✓ on the current one.
                Label {
                    visible: menuItem.entry.checked !== undefined
                    Layout.preferredWidth: 12
                    text: menuItem.entry.checked ? "✓" : ""
                    color: Theme.accent
                }
                Label {
                    Layout.fillWidth: true
                    text: menuItem.text
                    elide: Text.ElideRight
                    color: menuItem.entry.destructive ? Theme.errorText : menuItem.palette.windowText
                    opacity: menuItem.enabled ? 1 : 0.45
                }
                Label {
                    visible: (menuItem.entry.hint ?? "") !== ""
                    text: menuItem.entry.hint ?? ""
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                    opacity: menuItem.enabled ? 1 : 0.45
                }
            }
        }
    }

    Component {
        id: separatorComponent
        MenuSeparator {}
    }

    // "Add to playlist": New playlist…, then the playlists (not the one the item is).
    Component {
        id: playlistComponent

        Menu {
            id: sub
            required property var entry

            contentItem: ListView {
                property bool routeKeys: true
                focus: true
                implicitHeight: contentHeight
                model: sub.contentModel
                interactive: Window.window ? contentHeight + sub.topPadding + sub.bottomPadding > Window.window.height : false
                clip: true
                currentIndex: sub.currentIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }

            MenuItem {
                objectName: "newPlaylistItem"
                text: qsTr("New playlist…")
                onTriggered: {
                    const e = sub.entry;
                    root.run(e.onNewPlaylist);
                    root.close();
                }
            }
            MenuSeparator {
                visible: playlists.count > 0
            }

            Instantiator {
                model: playlists
                delegate: MenuItem {
                    required property int playlistId
                    required property string name
                    objectName: "playlistItem"
                    text: name
                    visible: playlistId !== sub.entry.excludePlaylist
                    enabled: visible // (j / k skip it too)
                    height: visible ? implicitHeight : 0
                    onTriggered: {
                        const e = sub.entry;
                        const id = playlistId;
                        root.run(() => e.onPlaylist(id));
                        root.close();
                    }
                }
                onObjectAdded: (index, object) => sub.insertItem(index + 2, object)
                onObjectRemoved: (index, object) => sub.removeItem(object)
            }
        }
    }
}
