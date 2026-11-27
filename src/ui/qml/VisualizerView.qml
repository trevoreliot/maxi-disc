// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

// Screen 3, MD Visualizer (docs/ui-draft/md-visualizer.jpeg, docs/plans/v1-visualizer-pile.md):
// picking music the way you'd pick a MiniDisc from a pile. The left column is the Library's browse
// dropdown and list; choosing an artist or genre (l, Enter, a click) turns the list into a pile of
// its albums' cases, and h / Esc / the crumb go back to the list. Albums mode is the pile of every
// album, Playlists mode a pile of mix discs. The right side is where a case comes out (V2) and
// the player stands (V3): placeholders for now. The window routes keys here via handleAction().
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
    // The case chosen last (Enter / a click on the cursor's case): V1 shows it in the case
    // area's placeholder; V2 takes it out of the pile.
    property var chosen: null
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

    // The album under the cursor (`a` finds its cover), else 0.
    function currentAlbumId(): int {
        return level === VisualizerView.Pile ? mdPile.currentAlbumId() : 0;
    }

    // `,` / Menu: the menu for what the cursor is on.
    function openContextMenu() {
        if (!hasLibrary)
            return;
        if (level === VisualizerView.List)
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
            return false;
        case KeyRouter.Activate:
            mdPile.chooseCurrent();
            return true;
        case KeyRouter.AddToDevice:
            mdPile.sendCurrent();
            return true;
        }
        return false;
    }

    // Esc inside the screen: from a pile back to its list. False when there's nothing to back
    // out of here (the window then goes to the main menu).
    function back(): bool {
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
                paneActive: root.active && root.level === VisualizerView.List
                onSelected: (m, entry) => root.openEntry(m, entry)
                onModeChanged: root.modeSwitched(browsePane.mode)
                onInteracted: root.interacted()
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
                paneActive: root.active && root.level === VisualizerView.Pile
                onCaseChosen: item => root.chosen = item
                onInteracted: root.interacted()
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

    // Where the case comes out (V2) and the player stands (V3): stacked as drawn when there's
    // room for both, side by side in a short window.
    Item {
        id: stage
        visible: root.hasLibrary
        anchors.left: divider.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceXL

        readonly property bool stacked: height >= 2 * 220 + Theme.spaceLG

        GridLayout {
            anchors.fill: parent
            columns: stage.stacked ? 1 : 2
            rowSpacing: Theme.spaceLG
            columnSpacing: Theme.spaceLG

            Placeholder {
                objectName: "casePlaceholder"
                Layout.fillWidth: true
                Layout.fillHeight: true
                heading: root.chosen ? root.chosen.title : qsTr("Pick a MiniDisc")
                detail: !root.chosen ? qsTr("Choose a case from the pile on the left")
                      : root.chosen.kind === "playlist" ? qsTr("Mix disc") : root.chosen.artist
            }
            Placeholder {
                objectName: "playerPlaceholder"
                Layout.fillWidth: true
                Layout.fillHeight: true
                heading: qsTr("MiniDisc player")
                detail: ""
            }
        }
    }

    component Placeholder: Item {
        id: placeholder

        property string heading
        property string detail

        Shape {
            anchors.fill: parent
            ShapePath {
                fillColor: "transparent"
                strokeColor: Theme.border
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                PathRectangle {
                    x: 1
                    y: 1
                    width: placeholder.width - 2
                    height: placeholder.height - 2
                    radius: Theme.radiusLarge
                }
            }
        }
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.spaceLG
            spacing: Theme.spaceXS

            Label {
                objectName: "placeholderHeading"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: placeholder.heading
                elide: Text.ElideRight
                font.pixelSize: Theme.fontHeading
                color: Theme.textSecondary
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                horizontalAlignment: Text.AlignHCenter
                text: placeholder.detail
                elide: Text.ElideRight
                color: Theme.textMuted
            }
        }
    }
}
