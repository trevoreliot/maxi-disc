// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// Main menu (docs/ui-draft/main-menu.jpeg): 1 Music Library, 2 Export, 3 MD Visualizer
// (not designed yet: a disabled placeholder the keyboard skips).
Item {
    id: root

    property bool active: false // this screen has keyboard focus
    property int currentTile: 0
    readonly property int enabledTiles: 2 // tiles 0..1; tile 2 is the placeholder

    signal tileActivated(int index)

    // Keyboard: h/l between tiles, Enter opens. Returns false for "move right" past the
    // last tile so the window can move focus on (to the queue, if open).
    function handleAction(action: int): bool {
        switch (action) {
        case KeyRouter.MoveLeft:
            currentTile = Math.max(0, currentTile - 1);
            return true;
        case KeyRouter.MoveRight:
            if (currentTile >= enabledTiles - 1)
                return false;
            ++currentTile;
            return true;
        case KeyRouter.Activate:
            tileActivated(currentTile);
            return true;
        case KeyRouter.MoveUp:
        case KeyRouter.MoveDown:
            return true;
        }
        return false;
    }

    function back(): bool {
        return false;
    }

    RowLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space2XL, 960)
        height: Math.min(parent.height - 2 * Theme.space2XL, 320)
        spacing: Theme.spaceXL

        MenuTile {
            objectName: "libraryTile"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 200
            number: 1
            label: qsTr("Music Library")
            iconKind: "library"
            subtitle: Library.folders.length === 0 ? qsTr("Add a music folder to begin")
                    : qsTr("%1 albums · %2 tracks").arg(Library.albumCount).arg(Library.trackCount)
            current: root.currentTile === 0
            paneActive: root.active
            onActivated: {
                root.currentTile = 0;
                root.tileActivated(0);
            }
        }
        MenuTile {
            objectName: "exportTile"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 200
            number: 2
            label: qsTr("Export")
            iconKind: "export"
            subtitle: qsTr("USB · DAP · MiniDisc (preview)")
            current: root.currentTile === 1
            paneActive: root.active
            onActivated: {
                root.currentTile = 1;
                root.tileActivated(1);
            }
        }
        MenuTile {
            objectName: "visualizerTile"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 200
            number: 3
            label: qsTr("MD Visualizer")
            iconKind: "visualizer"
            subtitle: qsTr("Coming soon")
            interactive: false
            toolTip: qsTr("A retro MiniDisc-style player view. Not designed yet.")
        }
    }
}
