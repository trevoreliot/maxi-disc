// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton

import QtQuick

// Right-click menus (Q1): views describe what their menu holds (entries) and ask for it here;
// Main shows it (AppContextMenu) and does what only it can (dialogs, opening an album).
//
// An entry: { text, hint (the key that does it too, shown greyed), enabled (default true),
// destructive (shown in red, kept at the bottom), separator: true (a line), or
// playlists: true (an "Add to playlist" submenu: onPlaylist(id), onNewPlaylist()) }, and
// onTriggered for a plain item.
QtObject {
    id: root

    // A view's menu, shown at (x, y) in `item`; `closed` runs when it's gone.
    signal requested(Item item, real x, real y, var entries, var closed)
    // What the menus ask of Main.
    signal editTagsRequested(list<string> paths, string label)
    signal editItemTagsRequested(string kind, int id, string label) // paths not loaded here
    signal findArtRequested(int albumId)
    signal goToAlbumRequested(int albumId)
    // A new playlist with an item's tracks (kind + id) or these files.
    signal newPlaylistRequested(string kind, int id, list<string> paths, string suggestedName)
    signal renamePlaylistRequested(int id, string name)
    signal deletePlaylistRequested(int id, string name)
    signal exportPlaylistRequested(int id, string name)
    signal openItemFolderRequested(string kind, int id) // paths not loaded here

    function open(item: Item, x: real, y: real, entries: var, closed: var) {
        if (entries.length > 0)
            requested(item, x, y, entries, closed ?? null);
    }

    function separator(): var {
        return { separator: true };
    }

    function sendText(what: string): string {
        if (!SendToDevice.canSend)
            return qsTr("Send to device");
        return what === "" ? qsTr("Send to %1").arg(SendToDevice.targetName)
                           : qsTr("Send %1 to %2").arg(what).arg(SendToDevice.targetName);
    }

    // "3 tracks" / "1 track" for the labels of marked-track actions.
    function tracksText(n: int): string {
        return n === 1 ? qsTr("1 track") : qsTr("%1 tracks").arg(n);
    }

    // Play / Play next / Add to queue / Add to playlist / Send for what a browse row or an album
    // tile stands for (kind "artist", "genre", "album", "playlist" + id).
    function itemEntries(kind: string, id: int, name: string): var {
        return [
            { text: qsTr("Play"), onTriggered: () => Playback.playItem(kind, id) },
            { text: qsTr("Play next"), onTriggered: () => Playback.enqueueItem(kind, id, true) },
            { text: qsTr("Add to queue"), onTriggered: () => Playback.enqueueItem(kind, id, false) },
            separator(),
            { text: qsTr("Add to playlist"), playlists: true, excludePlaylist: kind === "playlist" ? id : 0,
              onPlaylist: playlistId => Library.addItemToPlaylist(playlistId, kind, id),
              onNewPlaylist: () => root.newPlaylistRequested(kind, id, [], name) },
            { text: sendText(""), hint: "i", enabled: SendToDevice.canSend,
              onTriggered: () => SendToDevice.sendItem(kind, id, name) },
        ];
    }

    // An album (browse Albums row, grid tile): the item entries, then its cover, tags, folder.
    function albumEntries(albumId: int, name: string): var {
        return itemEntries("album", albumId, name).concat([
            separator(),
            { text: qsTr("Find cover…"), hint: "a", onTriggered: () => root.findArtRequested(albumId) },
            { text: qsTr("Edit tags…"), onTriggered: () => root.editItemTagsRequested("album", albumId, name) },
            { text: qsTr("Open folder"), onTriggered: () => root.openItemFolderRequested("album", albumId) },
        ]);
    }

    // A playlist row: the item entries, then rename / export / delete.
    function playlistEntries(playlistId: int, name: string, itemCount: int): var {
        const entries = itemEntries("playlist", playlistId, name);
        for (const e of entries) {
            if (e.text !== undefined && !e.playlists && e.hint !== "i")
                e.enabled = itemCount > 0;
        }
        return entries.concat([
            separator(),
            { text: qsTr("Rename…"), onTriggered: () => root.renamePlaylistRequested(playlistId, name) },
            { text: qsTr("Export…"), enabled: itemCount > 0, onTriggered: () => root.exportPlaylistRequested(playlistId, name) },
            separator(),
            { text: qsTr("Delete…"), destructive: true, onTriggered: () => root.deletePlaylistRequested(playlistId, name) },
        ]);
    }

    // A track in a list (album page, playlist, search). `model` is the list's TrackListModel;
    // `rows` the rows the queue / playlist / tags / send entries are for (the marked ones when
    // the clicked row is one of several marked, else just it); `row` the clicked row.
    // `options`: { onAlbumPage, playlistId (for Remove from playlist), marked (bool),
    // onToggleMark, markedCount, onClearMarks }.
    function trackEntries(model: var, row: int, rows: var, options: var): var {
        const track = model.get(row);
        const many = rows.length > 1;
        const what = many ? tracksText(rows.length) : "";
        const paths = rows.map(r => model.get(r).path);
        const entries = [
            { text: options.playFromHere ? qsTr("Play from here") : qsTr("Play"), onTriggered: () => Playback.playFromModel(model, row) },
            { text: many ? qsTr("Play %1 next").arg(what) : qsTr("Play next"), onTriggered: () => Playback.enqueueRows(model, rows, true) },
            { text: many ? qsTr("Add %1 to queue").arg(what) : qsTr("Add to queue"), onTriggered: () => Playback.enqueueRows(model, rows, false) },
            separator(),
            { text: many ? qsTr("Add %1 to playlist").arg(what) : qsTr("Add to playlist"), playlists: true,
              excludePlaylist: options.playlistId ?? 0,
              onPlaylist: playlistId => Library.appendToPlaylist(playlistId, paths),
              onNewPlaylist: () => root.newPlaylistRequested("", 0, paths, many ? "" : track.title) },
            { text: sendText(what), hint: "i", enabled: SendToDevice.canSend,
              onTriggered: () => many ? SendToDevice.sendItems(rows.map(r => ({ kind: "track", id: model.get(r).trackId, name: model.get(r).title })))
                                      : SendToDevice.sendItem("track", track.trackId, track.title) },
            separator(),
        ];
        if (!options.onAlbumPage)
            entries.push({ text: qsTr("Go to album"), enabled: track.albumId > 0, onTriggered: () => root.goToAlbumRequested(track.albumId) });
        if (options.onToggleMark) {
            entries.push({ text: options.marked ? qsTr("Unmark") : qsTr("Mark"), hint: "v", onTriggered: options.onToggleMark });
            if (options.markedCount > 0)
                entries.push({ text: qsTr("Clear marks (%1)").arg(options.markedCount), onTriggered: options.onClearMarks });
        }
        entries.push(
            { text: many ? qsTr("Edit tags (%1)…").arg(what) : qsTr("Edit tags…"), hint: "e",
              onTriggered: () => root.editTagsRequested(paths, many ? qsTr("%1 marked tracks").arg(rows.length) : track.path) },
            { text: qsTr("Find album cover…"), hint: "a", enabled: track.albumId > 0, onTriggered: () => root.findArtRequested(track.albumId) },
            { text: qsTr("Open containing folder"), onTriggered: () => Desktop.openContainingFolder(track.path) });
        if ((options.playlistId ?? 0) > 0) {
            entries.push(separator(), { text: qsTr("Remove from playlist"), destructive: true,
                                        onTriggered: () => Library.removeFromPlaylist(options.playlistId, track.path, options.occurrence ?? 0) });
        }
        return entries;
    }
}
