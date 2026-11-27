// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Screen 3, MD Visualizer (docs/ui-draft/md-visualizer.jpeg, docs/plans/v1-visualizer-pile.md):
// picking music the way you'd pick a MiniDisc from a pile. The left column is the Library's browse
// dropdown and list; choosing an artist or genre (l, Enter, a click) turns the list into a pile of
// its albums' cases, and h / Esc / the crumb go back to the list. Albums mode is the pile of every
// album, Playlists mode a pile of mix discs. On the right, the case area (V2, MdCaseStage): Enter
// or a click on the cursor's case takes it out, turned to its front with the cartridge part-way
// out; it flips to its track list and goes back into its gap in the pile. Below it the player
// will stand (V3). Panes: list / pile | case; the window routes keys here via handleAction().
Item {
    id: root

    enum Level { List, Pile }

    property bool active: false // this screen has keyboard focus
    property int level: VisualizerView.List
    // The pile's filter (0 = any), and the crumb back to the list.
    property int artistId: 0
    property int genreId: 0
    property string crumb
    property bool pileMixesArtists: true
    // Which pile is showing ("artists:5", "genres:0", "albums", "playlists"): a different one
    // starts with the cursor on its top case; the same one again keeps the cursor.
    property string pileKey
    // The case that's out (MdPile's item shape), or null.
    readonly property var chosen: caseStage.item
    // The case that's out has the keyboard (else the left column does).
    property bool caseFocused: false
    // Where the case that's out came from (it goes back there if that pile still shows), and a
    // case chosen while another was out (taken out once that one is back).
    property string takenPileKey
    property int takenIndex: -1
    // { item, index, pileKey } of the case to take out once the one that's out is back.
    property var pending: null
    // Shown at least once: until then the pile has no model, so the screen, alive but hidden
    // from startup, builds no cases and asks for no colours.
    property bool opened: false

    readonly property bool hasLibrary: Library.folders.length > 0
    readonly property int mode: browsePane.mode
    // Artists and Genres have a list to choose from; Albums and Playlists are a pile straight away.
    readonly property bool hasListLevel: mode === BrowsePane.Artists || mode === BrowsePane.Genres
    readonly property bool mixDiscs: mode === BrowsePane.Playlists
    readonly property alias browse: browsePane
    readonly property alias pile: mdPile
    readonly property alias crumbRow: crumbButton
    // The case area and the player (inside MdDeck).
    readonly property MdCaseStage caseStage: deck.caseStage
    readonly property MdCaseStage caseArea: deck.caseStage
    readonly property MdDeck deckArea: deck

    signal addFolderRequested()
    // The user clicked inside this screen (the window takes keyboard focus off the queue).
    signal interacted()

    // The browse list's choice: into that entry's pile.
    function openEntry(m: int, entry: var) {
        if (m === BrowsePane.Artists) {
            showPile("artists:" + (entry ? entry.artistId : 0), entry ? entry.artistId : 0, 0);
            crumb = entry ? entry.name : qsTr("All artists");
            pileMixesArtists = !entry;
        } else if (m === BrowsePane.Genres) {
            showPile("genres:" + (entry ? entry.genreId : 0), 0, entry ? entry.genreId : 0);
            crumb = entry ? entry.name : qsTr("All genres");
            pileMixesArtists = true;
        } else {
            return; // Albums / Playlists: the pile is showing already
        }
        level = VisualizerView.Pile;
    }

    function showPile(key: string, artist: int, genre: int) {
        artistId = artist;
        genreId = genre;
        if (key === pileKey)
            return;
        pileKey = key;
        // After the bindings that follow (the pile's model, on a mode switch) have settled.
        Qt.callLater(mdPile.resetCursor);
    }

    // `m`: the new mode (this screen's own `mode` binding may not have caught up yet).
    function modeSwitched(m: int) {
        if (m === BrowsePane.Artists || m === BrowsePane.Genres) {
            level = VisualizerView.List;
            return;
        }
        if (m === BrowsePane.Albums)
            showPile("albums", 0, 0);
        else
            showPile("playlists", artistId, genreId);
        pileMixesArtists = true;
        level = VisualizerView.Pile;
    }

    function backToList() {
        if (hasListLevel)
            level = VisualizerView.List;
    }

    // A case from the pile (at `index` in it): out to the case area. The latest choice wins: a
    // case moving finishes first; the one that's out already only takes the keyboard; another one
    // is taken out once that one is back.
    function takeOut(it: var, index: int) {
        pending = null;
        if (deck.animating)
            deck.finishAnimation();
        if (caseStage.animating)
            caseStage.finishAnimation(); // (a case going back is back; nothing queued follows it)
        if (caseStage.isOpen && it.kind + ":" + it.id === caseStage.takenKey) {
            caseFocused = true;
            return;
        }
        if (deck.hasDisc) { // a disc is in the player: another case only comes out after an Eject
            deck.showHint();
            return;
        }
        if (caseStage.isOpen) {
            pending = { item: it, index: index, pileKey: pileKey };
            putBackCase(true);
            return;
        }
        const here = pileIndexOf(it, index, pileKey);
        let origin = Qt.rect(0, 0, 0, 0);
        if (here >= 0) {
            mdPile.ensureVisible(here);
            origin = mdPile.caseRect(here, caseStage);
        }
        if (origin.width <= 0)
            origin = edgeRect();
        takenPileKey = pileKey;
        takenIndex = here;
        caseFocused = true;
        caseStage.takeCase(it, origin);
    }

    // `index` if the pile showing is `key`'s and the case there is still `it`; else -1.
    function pileIndexOf(it: var, index: int, key: string): int {
        if (level !== VisualizerView.Pile || key !== pileKey || index < 0 || index >= mdPile.count)
            return -1;
        const r = mdPile.itemAt(index);
        return r && r.kind === it.kind && r.id === it.id ? index : -1;
    }

    // The case back into its gap, if its pile still shows; else to the column's edge, fading.
    // `forSwap`: another case is queued to come out next; otherwise (Esc, Put back) nothing is.
    function putBackCase(forSwap: bool): bool {
        if (!forSwap)
            pending = null;
        if (!caseStage.isOpen)
            return false;
        if (caseStage.cartridgeRemoved)
            return true; // its cartridge is in the player: only Eject brings it home
        if (caseStage.phase === MdCaseStage.PuttingBack) {
            caseStage.finishAnimation(); // Esc again: it's back at once
            return true;
        }
        if (caseStage.animating)
            caseStage.finishAnimation();
        const here = pileIndexOf(caseStage.item, takenIndex, takenPileKey);
        let dest = Qt.rect(0, 0, 0, 0);
        if (here >= 0) {
            mdPile.ensureVisible(here);
            dest = mdPile.caseRect(here, caseStage);
        }
        caseFocused = false;
        if (dest.width > 0)
            caseStage.putBack(dest, false);
        else
            caseStage.putBack(edgeRect(), true);
        return true;
    }

    // Play the case that's out from `row`: into the player (or, its cartridge in there already,
    // straight to that track).
    function playFromCase(model: TrackListModel, row: int) {
        // A row clicked while something moves: finish it first (an eject that puts the case
        // away uses the click up).
        if (deck.animating)
            deck.finishAnimation();
        if (caseStage.animating)
            caseStage.finishAnimation();
        if (!caseStage.settled)
            return;
        caseFocused = true;
        if (caseStage.cartridgeRemoved || !deck.insert(model, row))
            Playback.playFromModel(model, row);
    }

    function caseReturned() {
        const next = pending;
        pending = null;
        caseFocused = false;
        if (next)
            takeOut(next.item, pileIndexOf(next.item, next.index, next.pileKey));
    }

    // A case-sized strip at the left column's edge, in the case area's coordinates.
    function edgeRect(): rect {
        const p = column.mapToItem(caseStage, Theme.spaceMD, column.height / 2);
        return Qt.rect(p.x, p.y, column.width - 2 * Theme.spaceMD, mdPile.caseHeight);
    }

    function sendItem(it: var) {
        SendToDevice.sendItem(it.kind, it.id, it.artist ? it.title + " – " + it.artist : it.title);
    }

    // The album under the cursor, or out with the keyboard on it (`a` finds its cover), else 0.
    function currentAlbumId(): int {
        if (caseFocused && caseStage.isOpen)
            return caseStage.item.kind === "album" ? caseStage.item.id : 0;
        return level === VisualizerView.Pile ? mdPile.currentAlbumId() : 0;
    }

    // `,` / Menu: the menu for what the cursor is on.
    function openContextMenu() {
        if (!hasLibrary)
            return;
        if (caseFocused && caseStage.isOpen)
            caseStage.openContextMenuForCase();
        else if (level === VisualizerView.List)
            browsePane.openContextMenuForCurrent();
        else
            mdPile.openContextMenuForCurrent();
    }

    // h/j/k/l, Enter, i. Returns false when "move right" runs off the right edge (the window
    // then moves focus to the queue, if it's open).
    function handleAction(action: int): bool {
        if (!hasLibrary) {
            if (action === KeyRouter.Activate)
                root.addFolderRequested();
            return action !== KeyRouter.MoveRight;
        }
        // A cartridge travelling (insert, eject): the same key again only finishes it; another one
        // finishes it and goes ahead (unless that started the case's own return: used up).
        if (deck.animating) {
            if (action === KeyRouter.Eject) {
                deck.eject(); // it finishes an eject itself, and cuts an insert short without playing
                return true;
            }
            const repeatDeck = deck.isRepeat(action);
            deck.finishAnimation();
            if (repeatDeck || caseStage.animating)
                return true;
        }
        // A case moving: the key that started it again only finishes it (Enter only counts when
        // the case has the keyboard); any other finishes it and then goes ahead, unless finishing
        // it started the next case of a swap coming out: that key is used up.
        if (caseStage.animating) {
            const repeat = caseStage.isRepeat(action) && (caseFocused || action === KeyRouter.FlipCase);
            caseStage.finishAnimation();
            if (repeat || caseStage.animating)
                return true;
        }
        if (action === KeyRouter.Eject) {
            deck.eject();
            return true;
        }
        if (action === KeyRouter.PlayCase) {
            if (caseStage.settled)
                caseStage.playCase();
            return true;
        }
        if (caseFocused && caseStage.isOpen)
            return caseAction(action);
        if (action === KeyRouter.FlipCase) {
            if (caseStage.isOpen) {
                caseFocused = true;
                caseStage.flipCase();
            }
            return true;
        }
        if (level === VisualizerView.List) {
            switch (action) {
            case KeyRouter.MoveDown:
            case KeyRouter.MoveUp:
                browsePane.moveSelection(action === KeyRouter.MoveDown ? 1 : -1);
                return true;
            case KeyRouter.MoveRight:
            case KeyRouter.Activate:
                browsePane.commit(); // → selected → openEntry()
                return true;
            case KeyRouter.MoveLeft:
                return true;
            case KeyRouter.AddToDevice: {
                const item = browsePane.currentItem();
                if (item)
                    SendToDevice.sendItem(item.kind, item.id, item.name);
                else
                    SendToDevice.sendItems([]);
                return true;
            }
            }
            return false;
        }
        switch (action) {
        case KeyRouter.MoveDown:
        case KeyRouter.MoveUp:
            mdPile.moveSelection(action === KeyRouter.MoveDown ? 1 : -1);
            return true;
        case KeyRouter.MoveLeft:
            backToList();
            return true;
        case KeyRouter.MoveRight:
            if (!caseStage.isOpen)
                return false;
            caseFocused = true;
            return true;
        case KeyRouter.Activate:
            mdPile.chooseCurrent(); // → caseChosen → takeOut()
            return true;
        case KeyRouter.AddToDevice:
            mdPile.sendCurrent();
            return true;
        }
        return false;
    }

    // Keys while the case has them: h back to the left column, j/k along its track list, Enter
    // flips the front / plays from the back's track, f flips, i sends it.
    function caseAction(action: int): bool {
        switch (action) {
        case KeyRouter.MoveLeft:
            caseFocused = false;
            return true;
        case KeyRouter.MoveRight:
            return false;
        case KeyRouter.MoveDown:
        case KeyRouter.MoveUp:
            caseStage.moveTrack(action === KeyRouter.MoveDown ? 1 : -1);
            return true;
        case KeyRouter.Activate:
            caseStage.activate();
            return true;
        case KeyRouter.FlipCase:
            caseStage.flipCase();
            return true;
        case KeyRouter.AddToDevice:
            sendItem(caseStage.item);
            return true;
        }
        return false;
    }

    // Esc inside the screen: a case that's out goes back into the pile; else from a pile back to
    // its list. False when there's nothing to back out of here (the window then goes to the menu).
    function back(): bool {
        if (caseStage.isOpen && caseStage.cartridgeRemoved) {
            // Its cartridge is in the player: Esc only hands the keyboard back to the pile.
            if (caseFocused) {
                caseFocused = false;
                return true;
            }
        } else if (caseStage.isOpen) {
            return putBackCase(false);
        }
        if (level === VisualizerView.Pile && hasListLevel) {
            backToList();
            return true;
        }
        return false;
    }

    onVisibleChanged: if (visible) opened = true

    AlbumListModel {
        id: pileAlbums
        artistId: root.artistId
        genreId: root.genreId
    }
    PlaylistListModel {
        id: pilePlaylists
    }

    EmptyLibrary {
        anchors.fill: parent
        visible: !root.hasLibrary
        onAddFolderRequested: root.addFolderRequested()
    }

    Rectangle {
        id: column
        visible: root.hasLibrary
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(280, Math.min(400, Math.round(root.width * 0.32)))
        color: Theme.surface

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            BrowsePane {
                id: browsePane
                Layout.fillWidth: true
                Layout.fillHeight: root.level === VisualizerView.List
                Layout.preferredHeight: root.level === VisualizerView.List ? -1 : headerHeight
                autoSelect: false
                listsShown: root.level === VisualizerView.List
                showPlaylistTools: false
                contextMenus: true
                paneActive: root.active && root.level === VisualizerView.List && !root.caseFocused
                onSelected: (m, entry) => root.openEntry(m, entry)
                onModeChanged: root.modeSwitched(browsePane.mode)
                onInteracted: {
                    root.caseFocused = false;
                    root.interacted();
                }
            }

            ItemDelegate {
                id: crumbButton
                objectName: "pileCrumb"
                Layout.fillWidth: true
                visible: root.level === VisualizerView.Pile && root.hasListLevel
                leftPadding: Theme.spaceLG
                text: "‹  " + root.crumb
                font.bold: true
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Back to the list (h, Esc)")
                onClicked: {
                    root.caseFocused = false;
                    root.interacted();
                    root.backToList();
                }
            }

            MdPile {
                id: mdPile
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.level === VisualizerView.Pile
                model: !root.opened ? null : root.mixDiscs ? pilePlaylists : pileAlbums
                kind: root.mixDiscs ? "playlist" : "album"
                showArtist: !root.hasListLevel || root.pileMixesArtists
                paneActive: root.active && root.level === VisualizerView.Pile && !root.caseFocused
                takenKey: root.caseStage.takenKey
                onCaseChosen: item => root.takeOut(item, mdPile.currentIndex)
                onInteracted: {
                    root.caseFocused = false;
                    root.interacted();
                }
            }
        }
    }

    Rectangle {
        id: divider
        visible: root.hasLibrary
        anchors.left: column.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.border
    }

    // The case area and the player (V3): stacked as drawn when there's room, side by side in a
    // short window; the cartridge travels between them.
    MdDeck {
        id: deck
        objectName: "deck"
        visible: root.hasLibrary
        anchors.left: divider.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceLG
        casePaneActive: root.active && root.caseFocused
        animationsEnabled: root.visible && Playback.windowVisible
        onReturnCaseRequested: root.putBackCase(false)
        onExternalLoaded: {
            root.pending = null;
            root.caseStage.drop();
            root.caseFocused = false;
        }
        onInteracted: root.interacted()
    }

    Connections {
        target: root.caseStage
        function onPlayTrackRequested(model: TrackListModel, row: int) {
            root.playFromCase(model, row);
        }
        function onPutBackRequested() {
            root.putBackCase(false);
        }
        function onEjectRequested() {
            deck.eject();
        }
        function onReturned() {
            root.caseReturned();
        }
        function onInteracted() {
            if (root.caseStage.phase !== MdCaseStage.PuttingBack)
                root.caseFocused = true;
            root.interacted();
        }
    }
}
