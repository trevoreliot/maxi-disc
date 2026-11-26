// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// App shell (docs/ui-draft): header, one of three screens (main menu, library, export),
// the queue docked on the right, and the transport bar at the bottom.
//
// Keyboard: KeyRouter (C++) turns key presses into actions; handleAction() sends global
// ones (search, queue, play/pause, Esc) here and movement to whichever pane has focus:
// the queue if `queueFocused`, otherwise the current screen. Each screen tracks its own
// panes and returns false from handleAction() when "move right" runs off its right edge.
ApplicationWindow {
    id: root

    enum Screen { MenuScreen, LibraryScreen, ExportScreen }

    property int currentScreen: Main.MenuScreen
    property bool queueOpen: false
    property bool queueFocused: false

    readonly property bool hasLibrary: Library.folders.length > 0
    readonly property bool searching: topChrome.searchText.trim() !== ""
    readonly property alias libraryView: libraryScreen
    readonly property alias mainMenu: menuScreen
    readonly property alias exportView: exportScreen
    readonly property alias queuePanel: queueDock
    readonly property alias contextMenu: appContextMenu
    // The menu being asked for comes from the keyboard (`,`): its first item gets the cursor.
    property bool menuFromKeyboard: false
    // The latest "Go to album" (an older one answering late doesn't take over the screen).
    property int goToAlbumTicket: 0
    readonly property alias tagEditorDialog: tagDialog
    readonly property alias artworkFinderDialog: artDialog
    readonly property alias profileEditorDialog: profileDialog
    readonly property alias renameMdDialog: mdRenameDialog
    readonly property alias confirmDialog: confirm
    readonly property alias listenBrainzDialog: listenBrainz
    readonly property alias discordDialog: discord
    readonly property alias chrome: topChrome
    readonly property alias toast: toast

    // Minimised or hidden: Playback stops analysing (Q3) and animating for nobody.
    readonly property bool windowShown: visible && visibility !== Window.Minimized && visibility !== Window.Hidden
    onWindowShownChanged: Playback.windowVisible = windowShown
    Component.onCompleted: Playback.windowVisible = windowShown

    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    color: Theme.bg
    // Fusion takes its colours from the window palette (not the application palette);
    // every control and popup in the window inherits this.
    palette {
        window: Theme.bg
        windowText: Theme.textPrimary
        base: Theme.surfaceInset
        alternateBase: Theme.surface
        text: Theme.textPrimary
        placeholderText: Theme.textMuted
        button: Theme.surfaceElevated
        buttonText: Theme.textPrimary
        brightText: Theme.focusRing
        highlight: Theme.accent
        highlightedText: Theme.onAccent
        link: Theme.accent
        light: Theme.borderStrong
        midlight: Theme.border
        mid: Theme.border
        dark: Theme.surfaceInset
        shadow: "black"
        toolTipBase: Theme.surfaceElevated
        toolTipText: Theme.textPrimary
        disabled.windowText: Theme.textMuted
        disabled.text: Theme.textMuted
        disabled.buttonText: Theme.textMuted
        disabled.button: Theme.surface
    }
    title: Playback.hasTrack ? Playback.title + " — " + AppBridge.displayName : AppBridge.displayName

    function showScreen(s: int) {
        currentScreen = s;
        queueFocused = false;
    }

    function toggleQueue() {
        queueOpen = !queueOpen;
        queueFocused = queueOpen;
    }

    // A click in a pane gives it the keyboard (and takes it from the search field).
    function paneClicked(queue: bool) {
        queueFocused = queue;
        if (topChrome.searchHasFocus)
            root.contentItem.forceActiveFocus();
    }

    // Takes keyboard focus out of the search field (results stay up).
    function leaveSearchField() {
        root.contentItem.forceActiveFocus();
        if (searching) {
            showScreen(Main.LibraryScreen);
            libraryScreen.focusedPane = LibraryView.Content;
        }
    }

    // Esc, innermost first. Never quits.
    function goBack() {
        if (queueOpen && queueFocused) {
            queueOpen = false;
            queueFocused = false;
        } else if (currentScreen === Main.LibraryScreen && searching) {
            topChrome.clearSearch();
        } else if (currentScreen === Main.LibraryScreen && libraryScreen.back()) {
            // handled inside the library
        } else if (currentScreen === Main.ExportScreen && exportScreen.back()) {
            // closed the sync plan
        } else if (currentScreen !== Main.MenuScreen) {
            showScreen(Main.MenuScreen);
        }
    }

    // Goes to an album (a menu's "Go to album"): the library, the album open, the search cleared.
    function goToAlbum(albumId: int, info: var) {
        if (info.title === undefined)
            return;
        if (searching)
            topChrome.clearSearch();
        showScreen(Main.LibraryScreen);
        libraryScreen.openAlbum(albumId, info.title, info.artist, info.year, info.coverUrl, true);
        libraryScreen.focusedPane = LibraryView.Content;
    }

    function handleAction(action: int) {
        // An open right-click menu takes every key (none leaks to what's behind it).
        if (appContextMenu.opened) {
            appContextMenu.handleAction(action);
            return;
        }
        if (action === KeyRouter.ContextMenu) {
            menuFromKeyboard = true;
            if (queueFocused)
                queueDock.openContextMenu();
            else if (currentScreen === Main.LibraryScreen)
                libraryScreen.openContextMenu();
            else if (currentScreen === Main.ExportScreen)
                exportScreen.openContextMenu();
            menuFromKeyboard = false;
            return;
        }
        // The artwork finder takes the keys while it's open (it opts in with routeKeys);
        // only play/pause and track skipping still reach the player.
        if (artDialog.opened && artDialog.handleAction(action))
            return;
        if (artDialog.opened && action !== KeyRouter.PlayPause && action !== KeyRouter.NextTrack
                && action !== KeyRouter.PreviousTrack)
            return;

        switch (action) {
        case KeyRouter.FocusSearch:
            if (hasLibrary)
                topChrome.focusSearch();
            return;
        case KeyRouter.LeaveTextInput:
            leaveSearchField();
            return;
        case KeyRouter.PlayPause:
            Playback.playPause();
            return;
        case KeyRouter.NextTrack:
            Playback.next();
            return;
        case KeyRouter.PreviousTrack:
            Playback.previous();
            return;
        case KeyRouter.ToggleQueue:
            toggleQueue();
            return;
        case KeyRouter.Back:
            goBack();
            return;
        case KeyRouter.FindMissingArt:
            if (hasLibrary)
                artDialog.openMissing();
            return;
        case KeyRouter.FindArt:
            if (currentScreen === Main.LibraryScreen && !queueFocused && libraryScreen.currentAlbumId() > 0)
                artDialog.openAlbum(libraryScreen.currentAlbumId());
            return;
        case KeyRouter.Skip:
        case KeyRouter.Retry:
            return; // artwork finder only
        case KeyRouter.SendQueueToDevice:
            SendToDevice.sendQueue();
            return;
        }

        if (queueFocused) {
            if (action === KeyRouter.MoveLeft)
                queueFocused = false;
            else
                queueDock.handleAction(action);
            return;
        }
        let handled = false;
        if (currentScreen === Main.MenuScreen)
            handled = menuScreen.handleAction(action);
        else if (currentScreen === Main.LibraryScreen)
            handled = libraryScreen.handleAction(action);
        else
            handled = exportScreen.handleAction(action);
        if (!handled && action === KeyRouter.MoveRight && queueOpen)
            queueFocused = true;
    }

    KeyRouter {
        id: keys
        window: root
        onTriggered: (action) => root.handleAction(action)
    }

    header: TopChrome {
        id: topChrome
        title: root.currentScreen === Main.MenuScreen ? qsTr("Main Menu")
             : root.currentScreen === Main.LibraryScreen ? qsTr("1  Music Library") : qsTr("2  Export")
        showBack: root.currentScreen !== Main.MenuScreen
        queueOpen: root.queueOpen
        searchEnabled: root.hasLibrary
        onBackRequested: root.showScreen(Main.MenuScreen)
        onQueueToggled: root.toggleQueue()
        onAddFolderRequested: folderDialog.open()
        onListenBrainzRequested: listenBrainz.open()
        onDiscordRequested: discord.open()
        onSearchAccepted: {
            root.leaveSearchField();
            libraryScreen.searchResults.moveSelection(0);
        }
        // Typing a search from another screen shows the library (where results appear).
        onSearchTextChanged: {
            if (searchText.trim() === "") {
                searchDebounce.stop();
                libraryScreen.searchText = ""; // clearing is instant
            } else {
                searchDebounce.restart();
            }
            if (searchText.trim() !== "" && root.currentScreen !== Main.LibraryScreen)
                root.showScreen(Main.LibraryScreen);
        }
    }

    footer: ColumnLayout {
        spacing: 0

        Pane {
            Layout.fillWidth: true
            visible: Library.errorString !== ""
            padding: 6
            background: Rectangle {
                color: Theme.errorBg
            }

            RowLayout {
                anchors.fill: parent

                Label {
                    Layout.fillWidth: true
                    text: Library.errorString
                    elide: Text.ElideRight
                    color: Theme.errorText
                }
                ToolButton {
                    text: "✕"
                    focusPolicy: Qt.NoFocus
                    onClicked: Library.clearError()
                }
            }
        }

        Pane {
            id: statusBanner
            Layout.fillWidth: true
            visible: false
            padding: 6
            background: Rectangle {
                color: Theme.infoBg
            }

            Label {
                text: Library.statusMessage
                elide: Text.ElideRight
                color: Theme.infoText
            }

            Connections {
                target: Library
                function onStatusMessageChanged() {
                    statusBanner.visible = Library.statusMessage !== "";
                    statusTimer.restart();
                }
            }
            Timer {
                id: statusTimer
                interval: 6000
                onTriggered: statusBanner.visible = false
            }
        }

        TransportBar {
            Layout.fillWidth: true
            onListenBrainzSetupRequested: listenBrainz.open()
            onDiscordSetupRequested: discord.open()
        }
    }

    Timer {
        id: searchDebounce
        interval: 150
        onTriggered: libraryScreen.searchText = topChrome.searchText.trim()
    }

    TagEditorDialog {
        id: tagDialog
        onSaved: albumId => libraryScreen.tagsSaved(albumId, tagDialog.editor.trackCount)
    }

    ArtworkFinderDialog {
        id: artDialog
    }

    RenameMdDialog {
        id: mdRenameDialog
    }

    ConfirmDialog {
        id: confirm
    }

    ListenBrainzDialog {
        id: listenBrainz
    }

    // --- Right-click menus (Q1): the menu, and what its entries ask of the window ---
    AppContextMenu {
        id: appContextMenu
    }

    TextPrompt {
        id: newPlaylistPrompt
        property string kind
        property int itemId: 0
        property list<string> paths: []
        title: qsTr("New playlist")
        placeholderText: qsTr("Playlist name")
        onSubmitted: name => {
            if (kind !== "")
                Library.createPlaylistFromItem(name, kind, itemId);
            else
                Library.createPlaylist(name, paths); // (reports failures itself)
        }
    }

    TextPrompt {
        id: renamePlaylistPrompt
        property int playlistId: 0
        title: qsTr("Rename playlist")
        onSubmitted: name => {
            if (Library.renamePlaylist(playlistId, name) && libraryScreen.playlistId === playlistId)
                libraryScreen.playlistName = name;
        }
    }

    FileDialog {
        id: exportPlaylistDialog
        property int playlistId: 0
        title: qsTr("Export playlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u8"
        nameFilters: [qsTr("Playlists (*.m3u8 *.m3u)")]
        onAccepted: Library.exportPlaylist(playlistId, selectedFile)
    }

    Connections {
        target: ContextMenus
        function onRequested(item: Item, x: real, y: real, entries: var, closed: var) {
            appContextMenu.show(item, x, y, entries, closed, root.menuFromKeyboard);
        }
        function onEditTagsRequested(paths: list<string>, label: string) {
            tagDialog.edit(paths, label);
        }
        function onEditItemTagsRequested(kind: string, id: int, label: string) {
            Library.requestItemPaths(kind, id, "editTags:" + label);
        }
        function onFindArtRequested(albumId: int) {
            artDialog.openAlbum(albumId);
        }
        function onGoToAlbumRequested(albumId: int) {
            Library.requestAlbumInfo(albumId, "goToAlbum:" + (++root.goToAlbumTicket));
        }
        function onNewPlaylistRequested(kind: string, id: int, paths: list<string>, suggestedName: string) {
            newPlaylistPrompt.kind = kind;
            newPlaylistPrompt.itemId = id;
            newPlaylistPrompt.paths = paths;
            newPlaylistPrompt.text = suggestedName;
            newPlaylistPrompt.open();
        }
        function onRenamePlaylistRequested(id: int, name: string) {
            renamePlaylistPrompt.playlistId = id;
            renamePlaylistPrompt.text = name;
            renamePlaylistPrompt.open();
        }
        function onDeletePlaylistRequested(id: int, name: string) {
            confirm.ask({
                title: qsTr("Delete “%1”?").arg(name),
                message: qsTr("The playlist goes; its music stays in the library."),
                action: () => {
                    if (Library.deletePlaylist(id) && libraryScreen.playlistId === id)
                        libraryScreen.playlistId = 0;
                }
            });
        }
        function onExportPlaylistRequested(id: int, name: string) {
            exportPlaylistDialog.playlistId = id;
            exportPlaylistDialog.open();
        }
        function onOpenItemFolderRequested(kind: string, id: int) {
            Library.requestItemPaths(kind, id, "openFolder");
        }
    }

    Connections {
        target: Library
        function onItemPathsReady(kind: string, id: int, paths: list<string>, tag: string) {
            if (paths.length === 0)
                return;
            if (tag === "openFolder")
                Desktop.openCommonFolder(paths);
            else if (tag.startsWith("editTags:"))
                tagDialog.edit(paths, paths.length === 1 ? paths[0] : qsTr("%1 — %2 tracks").arg(tag.slice(9)).arg(paths.length));
        }
        function onAlbumInfoReady(albumId: int, info: var, tag: string) {
            if (tag === "goToAlbum:" + root.goToAlbumTicket)
                root.goToAlbum(albumId, info);
        }
    }

    DiscordDialog {
        id: discord
    }
    // It only asks about the MiniDisc so far: a different disc (unplugged, another recorder)
    // under an open question mustn't get the answer.
    Connections {
        target: MiniDisc
        enabled: confirm.opened
        function onDiscChanged() { confirm.reject(); }
    }

    ProfileEditorDialog {
        id: profileDialog
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose your music folder")
        onAccepted: Library.addFolder(selectedFolder)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            id: screens
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentScreen

            readonly property bool screenActive: !root.queueFocused && !topChrome.searchHasFocus

            MainMenuView {
                id: menuScreen
                active: screens.screenActive && root.currentScreen === Main.MenuScreen
                onTileActivated: (index) => {
                    root.paneClicked(false);
                    root.showScreen(index === 0 ? Main.LibraryScreen : Main.ExportScreen);
                }
            }

            LibraryView {
                id: libraryScreen
                active: screens.screenActive && root.currentScreen === Main.LibraryScreen
                onAddFolderRequested: folderDialog.open()
                onInteracted: root.paneClicked(false)
                onEditTagsRequested: (paths, label) => tagDialog.edit(paths, label)
                onFindArtRequested: id => artDialog.openAlbum(id)
                onFindMissingArtRequested: artDialog.openMissing()
            }

            ExportView {
                id: exportScreen
                active: screens.screenActive && root.currentScreen === Main.ExportScreen
                onInteracted: root.paneClicked(false)
                onEditProfileRequested: id => profileDialog.edit(id)
                onRenameMdRequested: (disc, index) => disc ? mdRenameDialog.renameDisc() : mdRenameDialog.renameTrack(index)
                onConfirmRequested: options => confirm.ask(options)
            }
        }

        QueuePanel {
            id: queueDock
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            visible: root.queueOpen
            paneActive: root.queueFocused
            onInteracted: root.paneClicked(true)
        }
    }

    // What a "send to device" did (M11b).
    Toast {
        id: toast
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spaceMD
    }

    Connections {
        target: SendToDevice
        function onNotice(text: string, isError: bool) {
            toast.show(text, isError);
        }
    }
}
