// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The MD Visualizer's pile (V1, docs/plans/v1-visualizer-pile.md): MiniDisc cases lying flat,
// first on top, one per album (an AlbumListModel's rows) or, with kind "playlist", one mix disc per
// playlist (a PlaylistListModel's). j/k move the cursor (its case slides out); a click moves it,
// a click on the cursor's case (or Enter, via chooseCurrent()) chooses it: caseChosen(item), which
// V2 turns into taking the case out. `item` has one shape for both kinds:
// { kind: "album" | "playlist", id, title, artist, year, coverUrl, count, color, detail (what its spine shows) }.
Item {
    id: root

    property var model: null
    property string kind: "album"
    property bool showArtist: true
    property bool paneActive: false
    // V2: the case that's out of the pile ("album:12" / "playlist:3"); a gap stays where it was.
    property string takenKey: ""
    property int contextTarget: -1 // the case a right-click menu is open for

    property alias currentIndex: list.currentIndex
    readonly property alias count: list.count
    readonly property alias view: list
    // An MD case's spine is about 1 : 10: a list row, with the text reading normally.
    readonly property int caseHeight: Math.max(26, Math.min(34, Math.round(width / 10)))

    signal caseChosen(var item)
    signal interacted()

    function moveSelection(delta: int) {
        if (list.count === 0)
            return;
        const i = Math.max(0, Math.min(list.count - 1, list.currentIndex + delta));
        if (i === list.currentIndex)
            return;
        list.currentIndex = i;
        ensureVisible(i);
    }

    // Keeps two cases either side of `i` in view (as far as the pile allows).
    function ensureVisible(i: int) {
        if (list.count === 0)
            return;
        list.positionViewAtIndex(Math.min(list.count - 1, i + 2), ListView.Contain);
        list.positionViewAtIndex(Math.max(0, i - 2), ListView.Contain);
    }

    // A new pile: the cursor on the top case.
    function resetCursor() {
        list.currentIndex = list.count > 0 ? 0 : -1;
        list.positionViewAtBeginning();
    }

    function itemAt(index: int): var {
        if (!model || index < 0 || index >= list.count)
            return null;
        const r = model.get(index);
        const d = list.itemAtIndex(index) as MdCase;
        const known = kind === "playlist" ? Theme.mdBlankColor(r.playlistId) : SpineColors.color(r.coverUrl);
        const c = d ? d.caseColor : known.valid ? known : Theme.spineDefault;
        if (kind === "playlist")
            return { kind: "playlist", id: r.playlistId, title: r.name, artist: "", year: 0, coverUrl: "", count: r.availableCount, color: c,
                     detail: trackCount(r.availableCount) };
        return { kind: "album", id: r.albumId, title: r.title, artist: r.artist, year: r.year, coverUrl: r.coverUrl, count: 0, color: c,
                 detail: showArtist ? r.artist : "" };
    }

    function currentItem(): var {
        return itemAt(list.currentIndex);
    }

    // The album under the cursor (`a` finds its cover), else 0.
    function currentAlbumId(): int {
        const it = currentItem();
        return it && it.kind === "album" ? it.id : 0;
    }

    function chooseCurrent() {
        const it = currentItem();
        if (it)
            root.caseChosen(it);
    }

    // The case's rectangle in `target`'s coordinates (where V2's take-out starts).
    function caseRect(index: int, target: Item): rect {
        const d = list.itemAtIndex(index) as MdCase;
        return d ? d.body.mapToItem(target, 0, 0, d.body.width, d.body.height) : Qt.rect(0, 0, 0, 0);
    }

    function displayName(it: var): string {
        return it.artist ? it.title + " – " + it.artist : it.title;
    }

    // `i`: the cursor's case to the device selected on the Export screen.
    function sendCurrent() {
        const it = currentItem();
        if (it)
            SendToDevice.sendItem(it.kind, it.id, displayName(it));
        else
            SendToDevice.sendItems([]);
    }

    function contextEntries(index: int): var {
        const it = itemAt(index);
        if (!it)
            return [];
        return it.kind === "playlist" ? ContextMenus.playlistEntries(it.id, it.title, it.count)
                                      : ContextMenus.albumEntries(it.id, it.title);
    }

    function openContextMenu(index: int, item: Item, x: real, y: real) {
        const entries = contextEntries(index);
        if (entries.length === 0)
            return;
        contextTarget = index;
        ContextMenus.open(item, x, y, entries, () => { root.contextTarget = -1; });
    }

    // `,` / Menu: the cursor's case (brought into view first: the wheel may have scrolled it off).
    function openContextMenuForCurrent() {
        if (list.currentIndex >= 0)
            ensureVisible(list.currentIndex);
        const d = list.currentItem as MdCase;
        if (d)
            openContextMenu(list.currentIndex, d, Math.min(d.width * 0.35, 220), d.height);
    }

    function clickCase(index: int) {
        root.interacted();
        if (index === list.currentIndex) {
            chooseCurrent();
            return;
        }
        list.currentIndex = index;
        ensureVisible(index);
    }

    function trackCount(n: int): string {
        return n === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(n);
    }

    ListView {
        id: list
        objectName: "mdPile"
        anchors.fill: parent
        anchors.topMargin: Theme.spaceSM
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.model
        delegate: pileCase
        ScrollBar.vertical: ScrollBar {}

        // A reload can shorten the pile under the cursor.
        onCountChanged: {
            if (count === 0)
                return;
            if (currentIndex >= count)
                currentIndex = count - 1;
            else if (currentIndex < 0)
                currentIndex = 0;
        }
    }

    // One delegate for both kinds, reading its row through `model`: `kind` and the view's model
    // change one after the other when the dropdown switches between albums and playlists, and a
    // delegate made for one kind must not fail on the other's rows in between (the view then
    // rebuilds them all for the new model).
    Component {
        id: pileCase
        MdCase {
            id: caseDelegate
            required property int index
            required property var model
            readonly property bool isMix: root.kind === "playlist"

            width: list.width
            height: root.caseHeight
            caseKind: root.kind
            caseId: (isMix ? model.playlistId : model.albumId) ?? 0
            label: (isMix ? model.name : model.title) ?? ""
            detail: isMix ? root.trackCount(model.availableCount ?? 0) : root.showArtist ? model.artist ?? "" : ""
            cover: isMix ? "" : model.coverUrl ?? ""
            current: ListView.isCurrentItem
            paneActive: root.paneActive
            menuTarget: root.contextTarget === index
            taken: root.takenKey === root.kind + ":" + caseId
            onClicked: root.clickCase(index)
            onContextMenuRequested: (x, y) => root.openContextMenu(caseDelegate.index, caseDelegate, x, y)
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spaceLG
        visible: list.count === 0 && !(root.model && root.model.loading)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        text: root.kind === "playlist" ? qsTr("No playlists yet. Save the queue as one, or import an .m3u file in the Library.")
                                       : qsTr("No albums here")
        color: Theme.textMuted
    }

    FocusBar {
        active: root.paneActive
    }
}
