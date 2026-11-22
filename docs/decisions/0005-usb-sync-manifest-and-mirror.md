# 0005 — USB sync mirrors a per-device selection, and only ever touches files it wrote

- **Status:** accepted and implemented (2026-11-22, M9): `src/core/syncplanner.cpp`
  (`SyncPlanner::diff`), `src/core/usbsync.cpp`, `src/core/database.cpp` (schema v5,
  `device_sync_items`, `device_manifest`); tests `tst_syncplanner`, `tst_usbsync`
- **Decided by:** Claude Code during M9 (planned with Gemini 4 Neon); the user approved the plan
  and chose playlists in a subfolder and genres as selectable entries. The user can revisit
- **Revisit if:** a sync deletes or rewrites something it shouldn't, files get copied again
  for no reason, or the user wants one-off "copy these files" exports instead of a mirror

## Context

M9 syncs library music to USB sticks and DAPs: "only the changes copy" on a re-sync. The
device may also hold the user's own files (other music, documents), FAT/exFAT ignore case and
keep local times with 2 s resolution, and a stick can be unplugged mid-copy.

## Decision

1. **What's synced: a saved selection per device** (`device_sync_items`): playlists, albums,
   artists and genres from the browse pane, resolved to tracks when a sync is planned, so
   later additions to an album or playlist come along. Artist = the albums whose album artist
   it is; genre = tracks tagged with it.
2. **Mirror, but only of our own files.** `device_manifest` records every file the app wrote
   (path, source file size + mtime, how it was made, the device file's size + mtime). A sync
   adds what's missing, rewrites what changed (source edited, profile changed, or changed on
   the device), moves files when only the layout changed, and removes what's no longer
   selected. Files not in the manifest are never touched; their names are avoided
   ("Name (2).mp3"). A file of ours the user changed is left in place when deselected.
3. **Device times** compare within 2 s, also when shifted by whole quarter hours (FAT keeps
   local time: daylight saving or another time zone shifts every file); the size is the main
   signal. Paths compare case-folded.
4. **Writing:** hidden temp file `.mmm-part-*` → fsync → rename, then the manifest row is
   committed. A cancel or unplug loses at most the file in progress; leftovers are deleted
   by the next sync. Order: removals, moves, writes, playlists.
5. **Transcodes and art changes** go through a cache (`~/.cache/music-md-manager/transcode`,
   2 GB, least recently used first) keyed by source path + size + mtime + output settings.
   Art on device files: "Keep as is" copies files untouched; an MP3 transcode keeps the source's
   embedded cover and gets the folder art only when the file has none (user's choice, M9).
   "Shrink to 500 px" uses the folder art if there is one (decisions/0003), else the embedded one.
6. **Playlists:** `<musicDir>/Playlists/<name>.m3u8`, UTF-8, paths relative (`../Artist/…`),
   rewritten only when their content changes.
7. **A plan is always shown first**; changing the selection, profile, library or mount drops it.

## Costs

- Files copied to the device by other means are "the user's": the app won't adopt or update them.
- If the database is lost, the app forgets what it wrote: the next sync treats those files as
  the user's and writes "(2)" copies beside them.
- Removing an entry from a device's selection deletes its files on the next sync.

## How to revisit

- **Mirror vs. add-only:** skip `Remove` actions in `SyncPlanner::diff` (or make it a profile option).
- **Adopting existing files:** match untracked files by tags/size into the manifest (follow-up).
- **Playlist location / format:** `SyncPlanner::playlistPath` and the `M3u::format` call in `diff`.
- **Change detection:** `unchangedOnDevice` and `sameDeviceTime` in `syncplanner.cpp`.
