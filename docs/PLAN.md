# music-md-manager — Build Plan (C++ / Qt Quick)

Work ONE milestone per session, in order. A milestone is done only when its
"Done when" criteria pass (build + tests with sanitizers, zero warnings, qmllint clean)
and the user has run the "User test". Tick the box, commit, then stop.

## Scope

**In v1**
- Library: watch folders, read tags, browse by artist / album / genre, search, edit tags
- Playback: FLAC, MP3 (any bitrate), WAV, ALAC, AAC, Ogg/Opus; gapless; ReplayGain; queue and playlists
- Artwork: embedded or folder art first, then fetch missing covers from MusicBrainz + Cover Art Archive
- USB sync: mass-storage devices, per-device profiles, transcode on sync, incremental updates, .m3u playlists
- MiniDisc: NetMD upload in SP, LP2 and LP4, disc and track titling, built into the app
- Desktop/online: MPRIS media keys, ListenBrainz scrobbling, Discord "now playing" status

**Not in v1**
- MTP phone sync
- Streaming services, podcasts, lyrics, smart playlists, a plugin API
- Windows or macOS builds
- Hi-MD (NetMD only)
- netmd-exploits features (factory mode, track dumping)

## Decisions

| Topic | Decision |
| --- | --- |
| GUI | C++20 + Qt 6 Qt Quick / QML (animated UI planned for a later polish pass) |
| MiniDisc recorder | Sony MZ-NE410 (NetMD portable). User's Web MiniDisc Pro AppImage is the behaviour reference |
| USB targets | USB flash drive + generic mass-market DAP; FAT32/exFAT mass storage |
| Transcoding | Copy unchanged if device supports the format and has room. Flash drive: as-is (MP3 V0 if FLAC unsupported). Generic DAP: FLAC -> MP3 V0; profile option MP3 320 CBR for players that mishandle VBR. No AAC/Opus for these devices |
| DAP artwork | On sync, shrink embedded art to 500 px baseline JPEG |
| Artwork storage | Both: cover.jpg in album folder and embedded in files |
| Library size | < 1 TB (~30k+ tracks). Design for 100k tracks: first scan < 10 min, browse/search < 100 ms |
| Online services | ListenBrainz scrobbling, Discord Rich Presence. Requires the user's ListenBrainz token and a Discord application ID (supplied by user at runtime/config, never committed) |
| Album art | Folder file beats embedded; the finder saves both, 1200 px CAA JPEG, no file in folders shared with other albums. See `docs/decisions/0003-album-art-precedence-and-storage.md` |
| Devices / profiles | UDisks2 (USB or SD-card volumes with a filesystem, not system disks); profiles in the library DB keyed by filesystem UUID. See `docs/decisions/0004-device-detection-and-profiles.md` |
| USB sync | Per-device selection (playlists, albums, artists, genres) mirrored to the device; only files the app wrote (manifest) are ever replaced/moved/removed; playlists in `<musicDir>/Playlists/*.m3u8`. See `docs/decisions/0005-usb-sync-manifest-and-mirror.md` |
| GStreamer stalls | Recover (engine thread + abandon/replace a stalled pipeline) rather than fix GStreamer internals. See `docs/decisions/0001-gstreamer-stall-recovery.md` (evidence + how to pursue upstream) |
| Licence | GPL-3.0-or-later (Qt LGPLv3 + linux-minidisc). Verified in M10: libnetmd files are LGPL-2.1-or-later / GPL-2.0-or-later (third_party/libnetmd/UPSTREAM.md) |
| NetMD | Vendored, patched libnetmd behind a `NetMdBackend` interface; one worker thread; the disc header parsed by the app. See `docs/decisions/0006-netmd-via-vendored-libnetmd.md` |

## Tech stack

| Area | Library | Notes |
| --- | --- | --- |
| GUI | Qt 6 Qt Quick + Qt Quick Controls (QML) | ListView / GridView on C++ QAbstractListModel; must handle 100k rows |
| Build / tests | CMake + Ninja, qt_add_qml_module, ccache; Qt Test, Qt Quick Test, qmllint | ASan/UBSan in debug builds |
| Playback | GStreamer 1.x (playbin3) from C++ | Not Qt Multimedia (gapless + ReplayGain). Gapless via about-to-finish; rgvolume for ReplayGain |
| Tags | TagLib 2.x (C++ API) | Tags + embedded pictures |
| Database | SQLite 3 (C API or Qt SQL) | WAL mode; FTS5 search (verify FTS5 availability in M2) |
| Folder watching | QFileSystemWatcher + rescan on startup | Fall back to periodic rescan if inotify limits are hit |
| Background work | QThreadPool / QtConcurrent | Results via queued signals |
| HTTP / JSON | QNetworkAccessManager + QJsonDocument | MusicBrainz: 1 req/s, descriptive User-Agent |
| Artwork source | MusicBrainz + Cover Art Archive | Optional later: Chromaprint/AcoustID |
| USB detection | UDisks2 over QtDBus | Mount/unmount events |
| Transcoding | GStreamer encodebin | Per device profile |
| Media keys | MPRIS over QtDBus | |
| MiniDisc transport | libnetmd (linux-minidisc) + libusb | Wrap C API in one C++ class; document udev rule |
| ATRAC encoding | atracdenc | LP2/LP4; SP is encoded by the recorder |
| ListenBrainz | QNetworkAccessManager; JSON | User token (validate-token); a listen after 50% or 4 min for tracks > 30 s; offline queue in SQLite |
| Discord | QLocalSocket to $XDG_RUNTIME_DIR/discord-ipc-N (+ Flatpak / Snap / Vesktop dirs) | No SDK. SET_ACTIVITY (Listening) with track, artist, album, time bar; the user's own application ID (decisions/0009) |
| Secrets | QtKeychain | ListenBrainz user token |
| Packaging (later) | AppImage (linuxdeploy Qt plugin) or Flatpak | |

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│ Qt Quick / QML UI (src/ui)                                          │
│ library browser · now playing · artwork review · sync/MD views      │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ models, Q_PROPERTYs, signals
┌───────────────────────────────▼─────────────────────────────────────┐
│ Core (src/core) — C++ and Qt Core only, no QML/GUI                  │
│  Player        Library        Artwork       USB sync     MiniDisc   │
│  playback      scanner,tags   find missing  profiles     TOC,titles │
│  MPRIS,LB      database,FTS   fetch, embed  diff, copy,  SP/LP      │
│  Discord                                    transcode    upload     │
└──────┬──────────────┬─────────────┬────────────┬────────────┬───────┘
   GStreamer     SQLite,TagLib  QtNetwork   UDisks2/QtDBus  libnetmd,libusb
   PipeWire/ALSA file watcher   MB + CAA    GStreamer enc.  atracdenc
```

- The core is unit-testable without a display. QML only binds to what the core exposes.
- Scans, fetches, copies, transcodes, MD uploads and network calls run on QThreadPool
  workers and report progress through queued signals. No worker touches QML objects.
- USB sync and MiniDisc share one "job" abstraction (plan, run, progress, cancel);
  MiniDisc is just another sync target.
- ListenBrainz and Discord listen to the Player's track-change signals; playback never waits on the network.
- External control: everything the user can do to playback (and, later, the queue and library
  search) must be reachable from outside the GUI over D-Bus: MPRIS for standard controls (M5)
  plus an app-specific interface for the rest (M16). Keep these operations as core methods
  the UI *and* D-Bus call, never logic living only in QML. This is what the Emacs client uses.

## Milestones

### Phase 1: Foundation
- [x] **M0 Project skeleton** — Done when: CMake builds a Qt Quick window; `ctest` runs; ASan debug build works; .gitignore, CLAUDE.md, docs/PLAN.md in place. User test: app opens and closes cleanly.
- [x] **M1 Playback engine** — Done when: `Player` class wraps playbin3: play, pause, stop, seek, volume, position signals. User test: play a FLAC, a 320k MP3 and a WAV from the command line.
- [x] **M2 Tag reader + DB schema** — Done when: reads tags + embedded art via TagLib; SQLite schema with migrations; unit tests on sample files. User test: tests pass on a few of the user's own files.

### Phase 2: Library
- [x] **M3 Library scanner** — Done when: background scan of chosen folders into SQLite; incremental rescan; folder monitor. User test: scan the real library; note time and track count.
- [x] **M4 Library UI** — Done when: QML artist / album / track browser on C++ list models, album grid with art (async, cached thumbnails), search box (FTS5). User test: browse and search feel instant on the full library; profile with the QML Profiler.
- [x] **M5 Queue, gapless, playlists** — Done when: play queue, gapless transitions, ReplayGain, .m3u import/export, MPRIS. User test: a live or classical album plays with no gaps; media keys work.
- [x] **M5b UI redesign + Vim/Evil keyboard navigation** — Scheduled right after M5 so later
  milestones' screens (tag editor, artwork, sync, MiniDisc) are built to the final layout once.
  Done when: the shell/layout follows the user's drawings (`docs/ui-draft/`, local only: not in git);
  a keyboard focus model across panes and lists; Vim/Evil-style keys active whenever a text field
  doesn't have focus: `/` focuses search (Ctrl+F kept), `h`/`j`/`k`/`l` move left/down/up/right
  (between panes and within lists/grids), Enter activates, Esc backs out / leaves search; key
  bindings defined in one place (C++ or a single QML component) so more can be added (e.g. `gg`/`G`,
  `n`/`N` next/prev search hit, Space play/pause) without touching every view; qmllint clean and
  QML tests for the key handling. Visual polish/animation stays in "Later". User test: navigate
  browse → album → play → queue using only the keyboard; the UI matches the drawings.
- [x] **M6 Tag editor** — Done when: edit single and multiple tracks; writes to files and DB. User test: edit a test copy of an album, then verify in another app.

### Phase 3: Artwork
- [x] **M7 Artwork finder** — Done when: finds albums with no art; MusicBrainz lookup; Cover Art Archive download; saves cover.jpg and embeds; review dialog. User test: fix 20 albums missing art.

### Phase 4: USB sync
- [x] **M8 Device detection + profiles** — Done when: detects USB mounts; per-device profile (folder layout, playable formats, transcode target); built-in profiles for USB flash drive and generic DAP. User test: plug in a thumb drive and create a profile.
- [x] **M9 Sync engine** — Done when: sync plan (add / update / remove) shown before running; copy with progress; transcode cache; playlists written. User test: sync a playlist, change it, sync again; only the changes copy.
- [x] **M9b On-device browser** — Done when: the Export screen's right pane has a toggle between "Tracks to Export" and "On Device"; On Device lists the selected device's music (artist → album → tracks, sizes, free space) from M9's device scan, marks tracks not in the library, and offers Mount when unmounted; read-only. User test: plug in a thumb drive with music on it and browse what's there.

### Phase 5: MiniDisc
- [x] **M10 NetMD basics** — Done when: libnetmd linked; detect the MZ-NE410; read disc TOC and titles; rename disc and tracks; udev rule documented. User test: read and retitle a scratch disc.
- [x] **M11 SP upload** — Done when: decode to 44.1 kHz 16-bit PCM via GStreamer; upload as SP; progress and cancel. User test: burn a short album in SP, then play it on the deck.
- [x] **M11b Send music to devices** — Done when: `i` on the Library screen sends what's under the cursor to the device selected on Export (browse row = artist / genre / album / playlist; grid = album; album page = marked tracks, else the cursor track; playlist page / search = cursor track); `I` sends the play queue from any screen; "Send to <device>" buttons on the album page (marked, else the album), playlist page (the playlist), search results and the queue panel, disabled with the reason when no device can take it; USB selections take single tracks; a toast says what every send did (Export's own adds too). User test: with the MZ-NE410 selected, send an artist, an album, one song and 3 marked songs from Library and the play queue with `I`; the toasts' counts match "To record", no duplicates. On a USB stick: send a playlist and 2 marked tracks, remove one with `x`, restart: the selection is kept.
- [x] **M12 LP2 / LP4 upload** — Done when: atracdenc encoding; mode chosen per job; disc-space estimate before burning. User test: burn in LP2 and LP4; check sound and titles.
- [x] **M12b MiniDisc track management** — Done when: delete tracks (the marked ones, else the cursor's), move tracks (`J` / `K`) and wipe the disc, on the recorder's worker; the disc's groups (in the disc title) stay consistent after deletes and moves; deleting asks first (`y` / Enter), wiping needs `WIPE` typed; refusals (write-protected) and partial deletes are reported and the disc re-read. User test: on a scratch disc with a group (e.g. made in Web MiniDisc): move tracks within, out of and into the group (check the groups on the deck), cancel a delete, delete one track and 2–3 marked tracks, wipe the disc (the deck shows it blank, full time free).
- [x] **M12c Encode while uploading** — Done when: the next track is decoded / encoded while the current one uploads (LP encoding no longer adds to the recording time); cancel and a failed track still stop cleanly. User test: record 4+ tracks in LP2; it takes about the upload time alone; cancel one run part-way.
- [ ] **M13 (Optional, deferred) netmd-js extras** — Port device-quirk fixes only if the MZ-NE410 needs them. netmd-exploits features wait until requested.

### Phase 6: Online services (depend only on M5; may be pulled earlier)
- [x] **M14 ListenBrainz scrobbling** (was Last.fm: dropped before it shipped, decisions/0008) — Done when: connect account in settings; now-playing updates; listens follow the half-or-4-minutes rule; queued listens retry when back online; unit tests on listen timing. User test: play a few tracks, including some offline; all appear on ListenBrainz.
- [x] **M14b ListenBrainz token in pass** (decisions/0008, revisited) — Done when: without a working keyring and with pass set up, the token is kept in pass (`music-md-manager/listenbrainz/token`, value on stdin, one `pass` call at a time); the dialog says where it's kept; gpg's own error is shown when reading fails; tests use a fake `pass` (never the real store, GPG or keyring). User test: connect ListenBrainz, the entry shows in QtPass; restart: still connected (after a pinentry prompt, perhaps), both toggles on; disconnect: the entry is gone.
- [x] **M15 Discord status** — Done when: shows track, artist, album and progress while playing; clears on pause or quit; reconnects if Discord restarts; on/off toggle. User test: Discord profile shows what's playing and clears on stop.

### Quality of life (user-requested, between milestones)
- [x] **Q1 Right-click menus: Library and queue** — Done when: right-click (or `,` / Menu / Shift+F10 on the cursor's row) opens a menu fitting what's under it: browse artist / genre / album / playlist rows, album tiles, tracks on the album page / playlist / search, queue rows and the queue's empty space; Play / Play next / Add to queue / Add to playlist ▸ (New playlist… + playlists) / Send to <device> / Find cover… / Edit tags… / Open folder / Go to album / Remove from playlist / Mark, queue Move to play next / up / down / Remove, playlist Rename / Export / Delete; marked tracks act together when the clicked one is marked; j / k / h / l / Enter / Esc drive the menu; nothing leaks behind it. User test: right-click each kind of row and use a few entries each; `,` then j / l / Enter into "Add to playlist"; marked tracks; queue reorder.
- [x] **Q2 Right-click menus: Export and transport** — Done when: the now-playing area (go to album, add to playlist, send, cover, tags, show the file); Export: device rows (mount / unmount, set up / edit profile, open in file manager, refresh; a recorder: show tracks, refresh), browse rows (add to the device, play), sync selection rows (remove), on-device rows (expand / collapse, open in file manager, refresh), MiniDisc tracks (rename, move, mark, delete: a right-clicked unmarked track alone, a marked one with the others), "to record" (remove, clear), the disc (rename, record mode SP / LP2 / LP4, refresh, wipe); `,` by pane; files shown selected in the file manager (FileManager1 ShowItems, else the folder). User test: each Export menu on a USB stick and the MZ-NE410; delete an unmarked track while others are marked; the now-playing menu; "Open containing folder" selects the file in Dolphin.
- [x] **Q3 Real audio analyser** (user asked, 2026-11-25; replaces `AudioAnalyzerStub`; design:
  `docs/plans/q3-q4-analyser-and-disc.md`) — Done when: `Player` always has a GStreamer `spectrum`
  element (1024 bins, a message every 33 ms) in its `audio-filter` bin: after `rgvolume ! rglimiter`
  when ReplayGain is on, on its own when it's off. That puts it before playbin's volume, so the volume
  knob doesn't move the bars. A pure core function (tested headless) turns the linear bins into 24
  log-spaced bands (40 Hz–16 kHz, power summed per band, dB → 0..1). Each result is held on the engine
  thread until its audio is heard (the message's running-time against the pipeline clock − base time),
  then reaches QML as `Playback.levels` through an engine event that carries the stream epoch (stale
  ones are dropped). The look is segmented LED columns, like an MD deck's meter: unlit segments dim,
  lit ones in Theme tokens and brighter near the top, peak caps that hang about 0.5 s and then fall.
  Attack is fast and decay is smooth, animated per frame between messages. The bars fall to rest on
  pause / stop, and once they're at rest nothing redraws. Levels keep coming across gapless advances,
  seeks and Track ↔ Album changes (turning ReplayGain on or off still waits for the next track, as
  today). Right-click → "Turn analyser off / on" (QSettings); while it's off or the window is minimised,
  nothing is posted. The meter is drawn with scene-graph rectangle nodes, so it shows in ctest's
  offscreen screenshots too (the software renderer). Tests: band mapping (a 1 kHz tone lands in its
  band, silence = 0, pink noise roughly flat); `tst_player` (debug and release presets): levels while
  playing, none while paused or off, they continue across a gapless advance and a seek, the volume
  doesn't change them, a replaced engine keeps the setting; a QML test for the segments and the peak
  hold / fall; screenshot checked. User test: play a bass-heavy and a treble-heavy track: the bars
  follow the music in time; pause, seek, skip; turn it off and on; minimise; note the app's CPU % while
  playing, analyser on vs off.
- [x] **Q4 Spinning MiniDisc in the transport bar** (user asked, 2026-11-25; design:
  `docs/plans/q3-q4-analyser-and-disc.md`) — Done when: a bare MiniDisc, as tall as the bar's lower
  row allows (≈ 60 px), sits right after the analyser (media buttons | analyser | disc | volume |
  toggles). It shows the playing track's cover already cut round by the cover provider (a "round"
  variant: the cached square bucket masked to a smooth-edged circle on the provider's thread; no
  shader effects, so it renders in offscreen screenshots too), with a thin rim and the metal MD hub in
  the centre. No cover, or nothing loaded, shows a plain dark disc with a faint sheen and a mark that
  shows it turning. It spins while playing, about one turn every 2.6 s: a `FrameAnimation` adds speed ×
  frame time to the angle, and only the speed is animated (a short spin-up on play, a coast-down on
  pause with no jump in speed, a quick stop on Stop), so it resumes from the angle it stopped at. A
  new album cross-fades the art; tracks of one album never reload it (compared by the cover's key,
  album id + revision, not the URL, which includes the track's path when the art is embedded).
  Hover shows album — artist; left click goes to the album; right click opens the now-playing menu
  (Q2) plus "Stop / Start spinning" (QSettings). Nothing animates while paused, stopped, minimised or
  with spinning off, so the window isn't redrawn every frame for it. Theme tokens only;
  `Main.minimumWidth` raised (≈ 960) so the analyser keeps 160 px in the smallest window. Tests: the
  round variant (tst_uimodels: corners transparent, centre opaque); QML (the angle advances while playing,
  holds after the coast while paused, resumes from it; cover vs plain disc; no reload between tracks
  of one album; the menu entries; the analyser's width at the minimum window size); screenshot
  checked. User test: play an album with art and a track without; pause, resume, stop; skip from one
  album to another; on the real display (not just offscreen) the disc reads as a MiniDisc at that
  size; note the CPU %, spinning on vs off.

### Phase 6b: MD Visualizer (user-requested 2026-11-25; drawing: `docs/ui-draft/md-visualizer.jpeg`, local only)
Main-menu tile 3: picking music the way you'd pick a MiniDisc from a pile and play it on a portable.
Built on the `md-visualizer` branch, one milestone per session as usual; merged into main when V3's
user test passes. Everything is drawn in Qt Quick from shapes and the cover art: 2.5D (Flipable,
perspective `Rotation`), not Qt Quick 3D, unless V1 shows that's needed (then a decision record).
No bitmaps of real products and no Sony / MiniDisc / Walkman logos or wordmarks. The player's
reference photos (`docs/ui-draft/Sony-MZ-M200-*.webp`, local only) are for its shape and layout.
What comes out of a slip case is the cartridge, as in real life: a clear shell (like clear MD
blanks), so the disc inside shows through, with the cover as its label. The offscreen software
renderer draws 3D-rotated items flat, so the real display is part of every user test.
- [x] **V1 Visualizer screen and the pile** — Done when: tile 3 opens a new screen (in Main's
  StackLayout and kept alive like the others; the tile gets a real icon). As drawn, the left column
  has the Library's browse dropdown (Artist / Genre / Album / Playlist) and its list, with the same
  keys (j / k / h / l / Enter, `,`, `i`; `/` searches as everywhere). Choosing an artist or genre
  (l / Enter / a click) turns the list into a pile of its albums: MiniDisc slip cases lying flat,
  spines facing you, title (and artist, when the pile mixes artists) reading normally, each case
  coloured from its cover (a dominant colour, computed off the UI thread and cached), the cursor's
  case slid part-way out towards the case area. h / Esc / the "‹ Artist" crumb go back to the list.
  Albums mode is the pile of every album; Playlists mode is a pile of mix discs: one home-recorded MD
  per playlist (a blank-MD coloured case, its name on a paper label). The pile follows the cursor and
  stays smooth with hundreds of albums. The case area and the player on the right are placeholders.
  Plan: docs/plans/v1-visualizer-pile.md. Tests: C++ for the cover colour; QML for the drill-down,
  each mode's pile and the cursor (incl. a 1,000-case pile); screenshots. User test: browse a few
  artists by keyboard and by mouse; the pile reads as MD cases; scroll a big genre quickly.
- [x] **V2 Taking a case from the pile** — Done when: Enter or a click on the cursor's case slides it
  out of the pile to the case area, turning to face front (the front insert: the cover, title and
  artist); then the cartridge slides part-way out of the case, its label showing the cover. A Flip button
  (and a key) turns the case round to its back: the track list (number, title, duration), with the
  cursor in it (j / k; Enter plays from that track: V3 puts the insert animation in front of it;
  Enter or a click on the front flips, as drawn); a mix disc's back lists the playlist on its
  paper insert. Esc or Back puts the case back in the pile, animated. Each animation takes about half
  a second; pressing again finishes it at once; no work runs on the GUI thread during them.
  Right-click on the case opens the album's (or playlist's) menu (Q1's entries). Tests:
  QML (pile → out → cartridge out → flipped → back; the track list's rows; the keys); screenshots of the
  front and the back. User test: take out, flip and put back several albums, by mouse and keyboard
  only; it feels like handling a real case. Plan: docs/plans/v2-visualizer-case.md.
- [x] **V3 The MiniDisc player** — Done when: below the case area stands a portable MD player drawn
  after the MZ-M200: a near-square body only a little bigger than the cartridge, rounded corners, its
  dark lid made smoked and translucent, so the cartridge and its turning disc (Q4's SpinningDisc) show
  through. The face is seen straight on and the left edge obliquely (an affine shear, which both
  renderers draw), as in the perspective photo. That edge carries the narrow side LCD: track number,
  elapsed time and a two-bar level meter from `Playback.levels` (blank while the analyser is off);
  DISPLAY cycles elapsed / remaining / the scrolling title. Down the face's left edge: DISPLAY,
  VOL − / +, the ■ stop and ❚❚ pause keys, and the jog lever at the bottom corner (click plays; its two
  ends and the mouse wheel go to the previous / next track). The OPEN slide on the top edge is Eject.
  Keys that would do nothing here (T MARK, REC, HOLD) and the maker's marks are left off. Play on the
  case, or Enter on a track on its back: the cartridge leaves the case and drops into the player, the
  lid closes, and the album plays from that track (replacing the queue, like playing an album; a mix
  disc plays its playlist). The
  player's keys drive Playback (they're Playback commands, like the transport bar's). While a
  cartridge is in the player, the pile can still be browsed, but another case only comes out after an
  Eject. Eject (the OPEN slide, and a key) stops playback, opens the lid, and puts the cartridge back
  in its case and the case back in the pile. If playback starts elsewhere (Library, MPRIS, the
  queue), the player shows that album's cartridge, swapped without the pull-out animation. Nothing
  animates while hidden or idle (Q3 / Q4's rules). Tests: QML (insert → playing; the keys → Playback;
  eject → stopped and a case can come out; playback started elsewhere shows its cartridge); screenshot
  of the player with a cartridge in, and of the side LCD. User test: pick an album, flip it, play
  track 3 from the back; pause, skip and set the volume on the player; eject and pick another;
  start something from the Library and come back. Plan: docs/plans/v3-visualizer-player.md.

### Phase 7: Emacs integration (depends on M5)
- [ ] **M16 Doom Emacs client** — Done when: an Emacs package (in `contrib/emacs/`, loadable from
  Doom's `packages.el` via `:recipe` with a local path) uses `dbus.el` to show now playing
  (title, artist, album, position) in a buffer and/or the modeline, and controls play/pause,
  next/previous, seek and volume over MPRIS; an app-specific D-Bus interface
  (`org.musicmdmanager.*`) adds queue listing and library search → enqueue/play. User test:
  control playback and queue an album from Doom Emacs without touching the app window.

### Later
- Animation and visual-polish pass (album-art transitions, animated now-playing view)
- Settings polish; AppImage or Flatpak build; optional MTP support

## Risks
| Risk | Mitigation |
| --- | --- |
| MiniDisc bugs only appear on real hardware | Do it last; scratch discs; compare against Web MiniDisc Pro AppImage |
| libnetmd lacks some netmd-js features | Port one at a time (M13) using netmd-js as reference |
| Memory bugs | RAII, sanitizers, warnings as errors, tests per milestone |
| Tag writing damages files | Test on copies; user keeps a library backup before M6 |
| QML stutters on huge lists | Models in C++, simple delegates, async cached thumbnails |
| Pro usage limits | One milestone per session, fresh sessions, ccache |

## Follow-ups
(Claude Code: append notes here instead of expanding the current milestone.)
- Q3 notes / follow-ups (decisions/0010):
  - `PlayerEngine`'s constructor runs on the creating (UI) thread: it makes playbin3 and the
    audio-filter (registry lookups, `gst_parse_bin_from_description`) before moving to its thread.
    That predates Q3 and has never stalled; if it ever does, build the pipeline in an init slot on the
    engine thread (raised in Neon's review).
  - The meter's floor / top (−66 / −15 dB, `SpectrumMapping`) were measured on two tracks; retune
    if the bars sit too low or stay pinned across the library.
  - After a seek, results wait until playback reports Playing again (Neon's review: a stale base
    time during the re-preroll). It didn't reproduce with fakesink; kept as a safeguard.
- Q4 notes / follow-ups (decisions/0011):
  - The disc (like the transport's small cover) doesn't pick up art changed in place mid-track
    (`CoverLoader::bumpRevision`): Playback would need to announce it. Albums whose tracks embed
    different art show the first track's (the key is the album's).
  - Seen once in ~15 runs: `tst_mpris` passed every test, then crashed at exit in Qt's D-Bus thread
    (`QObject::disconnect` on a null object while the private bus connection is torn down). Not
    reproducible on its own (10/10) or under load (3/3); look at the test's connection teardown order.
  - `Player::stop()` with no source sets playbin3's `uri` to NULL, which logs GStreamer-CRITICALs
    (`gst_uri_is_valid: assertion 'uri != NULL'`) in the QML tests' cleanups. Harmless noise; skip the
    set when there's no source.
- M0: ctest runs offscreen, so GPU-driver leaks (NVIDIA EGL/GBM) only appear on a real display.
  Suppressed in `src/sanitizer/sanitizer_defaults.cpp`; full unwinding costs ~1 s debug startup.
  Revisit if debug runs get too slow during large scans (M3+).
- M1: `Player` is single-track; EOS → Stopped. M5 adds the queue and gapless playback
  (playbin3 `about-to-finish`). `volume` is linear 0–1; the UI should map a cubic/perceptual slider onto it.
- M2: v1 simplifications to revisit when they bite:
  albums are keyed on (album artist, title), so two releases with the same name by the same
  artist merge (consider mb_album_id or folder as a tiebreaker); only the first GENRE is
  stored; orphaned artists/albums/genres aren't pruned on track removal (M3 scanner should).
- M2 user test: `MMM_TEST_MEDIA_DIR=~/Music/some-folder ./build/debug/tests/tst_tagreader userFiles`
  reads every file, checks covers, inserts into an in-memory DB and searches each title.
- M3: orphan pruning is done (the scanner prunes after every scan). Remaining notes:
  - Unreadable files aren't stored, so they're re-read on every scan. Fine for a handful;
    if a library has many, store them in a "failed files" table keyed on path+mtime.
  - Folder art (`cover|folder|front|album` .jpg/.png) is recorded in `albums.cover_path` only
    when the album has none; M7 should decide precedence vs embedded art.
  - A watcher-triggered rescan walks the changed directory's whole subtree. Changes directly
    in a library root therefore rewalk the whole folder (stat only, but slow on a HDD).
  - Watcher fallback: if inotify watches run out, a full rescan runs every 15 minutes.
  - Verified: the watcher does NOT see in-place file modifications (QFileSystemWatcher
    directory watches report entries added/removed/renamed only). Taggers that write a temp
    file and rename are caught; in-place writers are only caught by the next scan. M6 updates
    the DB itself. If needed later: watch track files for IN_MODIFY, or rescan on app focus.
- M4 notes / known gaps:
  - Tracks with no ALBUM tag don't appear in the album browser (search finds them). Consider an
    "Unknown album" bucket per artist.
  - Artist view lists albums where the artist is the *album* artist; "appears on" (guest or
    compilation tracks) isn't shown.
  - Search shows tracks only; clicking a result does nothing yet. M5 adds double-click to play.
    Matching artists/albums could be a later addition.
  - No translations are loaded yet; strings are wrapped in qsTr for later.
  - A scan that changes the library reloads every view; reloads that keep the same rows refresh
    in place, but a changed album list resets the grid's scroll position.
- M5 notes / known limits:
  - Repeat-one (and repeat-all with one track) restarts the track at EOS: a tiny gap by design,
    because playbin3 can't reliably play a stream gaplessly into itself. Album playback is gapless.
  - Turning ReplayGain on/off takes effect at the next track (GStreamer only swaps the filter
    while stopped); Track ↔ Album switches immediately.
  - Not yet: shuffle, saving the queue across restarts, drag-to-reorder in the queue panel
    (`PlayQueue::move` exists), MPRIS TrackList/OpenUri, "add to playlist" from track rows.
  - Found after the M5 commit: (a) short tracks weren't gapless (the track after next was
    needed before the UI thread saw the next start) → Player takes the whole upcoming list;
    (b) a deadlock inside playbin3 when the pipeline state changes while a gapless switch is
    being handled (main thread in set_state(READY) vs a streaming thread in a playbin3 pad
    probe) → the controller defers its corrections ~300 ms. Verified with 74 stress runs.
    Residual risk: a *user* action (double-click, Stop) landing within the same instant as a
    track boundary could still hit it; consider reporting to GStreamer with a minimal repro, or
    deferring all state changes that happen right after an advance.
  - **Resolved by recovery (docs/decisions/0001):** Player now runs GStreamer on an engine
    thread and replaces a stalled engine, so the hangs below can't freeze the app. History:
  - Was open: rare hang on rapid play→stop. ~1 in 100+ test runs
    (seen in tst_playbackcontroller repeatOneLoops, debug; likely also the earlier release hang).
    Main thread: Player::stop → set_state(READY) → gst_pad_stop_task waiting for a stream lock.
    Holder: a `typefindelement` streaming thread blocked in read() inside filesrc, which can't
    block on a regular file, so the fd likely no longer refers to the audio file (double
    close + reuse?). Not reproduced in 30 targeted runs. Next: catch it with GStreamer debug
    symbols / GST_DEBUG, check the fd; independently consider running pipeline state changes
    off the UI thread so any GStreamer stall can't freeze the app.
    Update: a 2nd stall signature caught with tools/dev/catch-stall.sh (fds were fine): a
    typefind streaming thread waiting for a state lock in playbin3's decoder setup during a
    rapid play→queue change; the UI thread kept running but playback never progressed. So at
    least three distinct GStreamer-internal stalls, all triggered by rapid state changes.
    Proposed fix: Player runs GStreamer control calls on its own thread + recovers from a
    stalled state change by abandoning the pipeline and resuming on a fresh one.
- M6 notes / follow-ups:
  - Tag writes replace the file (copy + rename) so a playing track and a crash are safe; hard
    links get split. See docs/decisions/0002 to switch to in-place saves.
  - Multi-track edits set the same value on every track; title and track number are hidden for
    them. Not done: per-track values in one dialog (renumber, case fixes), filename ↔ tags,
    MusicBrainz lookup of tags.
  - Editing a field that held several values (multiple artists / genres) writes one value
    ("A, B" stays one string). Clearing the year leaves ORIGINALDATE, which the reader falls back to.
  - Album compilation flag follows the last edited track (tracks don't store it).
  - Marking tracks (Ctrl/Shift+click, `v`) exists on the album page only; playlists and search
    results edit the cursor track (`e`) or all listed tracks (`E`). No tag editing from the grid
    or the queue yet.
- M7 notes / follow-ups:
  - MusicBrainz asks for a contact in the User-Agent: CMake cache variable `MMM_CONTACT_URL`,
    default the project's GitHub page (no personal e-mail). Forks shipping their own builds
    should set their own (`-DMMM_CONTACT_URL=...`). The repo must be public for it to help.
  - Not done: pick a local image file as the cover; edit the search text in the dialog (for
    badly tagged albums); other sources (Discogs, iTunes, fanart.tv) or AcoustID lookup of
    untagged files; D-Bus access to the finder (M16: `ArtworkFinder` is core, no QML).
  - Replacing art in a folder shared with other albums only embeds it; the album's existing
    folder art (shared) still wins (decisions/0003).
  - Candidates show the 500 px thumbnail straight from the Cover Art Archive (QML's own network
    access, no User-Agent of ours). Fine for previews; route through HttpFetcher if CAA minds.
  - `s` / `r` are global bindings that only the finder uses; `a` works in the library screen
    (album page, grid, Albums browse list, playlist/search cursor track).
  - No retry with backoff across app restarts: a failed search (offline) is just listed as
    failed; `r` retries.
- M8 notes / follow-ups:
  - M9 needs from the profile: `musicDir`, `pathLayout` (`PathLayout::render`, FAT-safe, length
    capped), `DeviceProfiles::decide` (copy or MP3 + extension), `artworkPolicy`. Still to decide
    in M9: playlist folder and path style on the device, file-name collisions (two tracks → one
    path), and a "profile changed since the last sync" check (re-transcode/move files).
  - Not done: "safely remove" (UDisks2 `Drive.PowerOff` after unmounting), unlocking encrypted
    (LUKS) sticks, listing/removing profiles of unplugged devices, custom profile templates,
    {genre} in layouts (TrackRow has no genre yet).
  - Without udisksd the device list stays empty (logged once). `--no-devices` turns detection off.
  - On-device browser (user asked, 2026-11-22; planned as M9b): reuse M9's device scan rather than a
    second reader. Walk `mountPoint/musicDir` on the pool (whole device if the profile's folder is
    missing), read tags with `TagReader`, cache per device in the DB keyed by relative path + size +
    mtime (the same manifest M9 diffs against, so a re-open is instant and only changed files are
    re-read). Show results as they arrive; a toggle in the pane header plus a KeyRouter binding
    (no QML Shortcut). Later ideas: delete from device, "add to library", playlists on the device.
- M9 notes / follow-ups:
  - Sync = core `SyncPlanner` (plan, pure `diff`) → `UsbSync::run` (executor) on `SyncJob`'s
    worker; `Sync` QML singleton + `SyncPane.qml` on the Export screen. Keys: `i` add, `x`/Delete
    remove, `l`/`h` selection ↔ browse, `p` plan, `S` sync, Esc closes the plan.
  - Not done: single tracks or "the whole library" as selection entries; a `cover.jpg` per album
    folder on the device (some DAPs prefer folder art); `.m3u` / CRLF / backslash playlists for old
    car stereos; cache size setting + "clear cache"; other targets (Opus/AAC); ReplayGain applied
    while transcoding; adopting files already on a device into the manifest; auto-sync on plug-in;
    "safely remove" after a sync (see M8 notes); selection editing from the Library screen.
  - Planning stats every source file (fine for a few thousand tracks; a 30k-track selection
    takes a few seconds on a HDD). Transcodes run on up to 4 threads; the copy is sequential.
  - Fixed during the M9 user test: importing .m3u files made on Windows / by a DAP
    (`\Music\Artist\…`, `E:\…`) found nothing; `M3u::read` now maps backslashes and finds
    rooted paths below the folders above the playlist. Check a real file read-only with
    `MMM_TEST_M3U=list.m3u ./build/debug/tests/tst_m3u userPlaylist`.
  - Two syncs can't run at once (one `SyncJob`); changing the selected device while syncing
    keeps the running sync (its progress stays on screen).
- M9b notes / follow-ups:
  - Reads the profile's music folder (whole device without a profile or if the folder is missing;
    "Whole device" button otherwise). Tags are cached per device in `device_file_tags` (schema v6).
  - TagLib's messages about odd files ("Duplicate ID3v2 tags found", ...) go to the `mmm.taglib`
    logging category, hidden by default: `QT_LOGGING_RULES="mmm.taglib.debug=true"` shows them.
  - Not done: delete from device; add a device's tracks to the library; the device's playlists;
    cover art; search / "only not in library" filter; play a device file; marking duplicates on
    the device; reading only while the Export screen is visible (it reads while the view is on).
- M10 notes / follow-ups:
  - Setup + udev: `docs/netmd-setup.md`, `docs/udev/70-netmd.rules`. `MMM_NETMD_DEBUG=1` logs USB.
  - Not done: arrange tracks (move), groups (create/rename/delete), delete track, wipe disc,
    playback controls (netmd_play/stop...), a title-space meter (read the TOC's free title area),
    Japanese full-width titles (the 0x1803 title table), batch retitle from library tags,
    write-protect detection before writing (the refusal is reported after), libusb hotplug
    instead of polling.
  - Untested on hardware in CI by design: the user test is the check for the USB side. It found
    one bug (patched libnetmd rejected the MZ-NE410's 46-byte capacity reply as "no disc").
  - Export keys: l / h move between either left pane and the right pane (h returns where you came
    from); Enter on a recorder goes into its tracks.
  - Wipe disc / arrange tracks (and delete track): became M12b.
- M11 notes / follow-ups:
  - Recording: the MiniDisc's "To record" list (per session, not saved) ← `i` on browse rows;
    `MdUploadPlanner` checks fit / track limit / missing files; `NetMdManager::startUpload` runs one
    track per worker command: `GstPcmEncoder` (S16BE 44.1 kHz stereo temp file in
    `AppPaths::mdRecordDir()`) → `NetMdBackend::uploadTrack` (libnetmd secure session, patched for
    64 KB progress) → title + commit → re-read the disc. A failed track stops the rest.
  - Cancel finishes the track being recorded (NetMD can't stop an upload part-way; a track still
    being decoded stops at once). Esc doesn't cancel (like USB sync): `S` / the button does.
  - Not done: force-stop mid-track (would need a USB reset; risky); streaming encryption
    (libnetmd encrypts a whole track in memory: ~650 MB for a 60-minute track); decoding the next
    track while one uploads (done in M12c); gapless albums as one stream
    with track marks (each track is padded to a 2048-byte frame, up to ~12 ms); title templates
    ("Artist - Title"); reordering the queue; groups for recorded albums.
  - The user test (MZ-NE410) found the USB side's real problems, all in libnetmd (UPSTREAM.md
    14–15): the recorder answers "commit track" with INTERIM while still writing; it can take
    over 1 s to answer a poll after a commit (the 1 s libusb timeout then wedged it until a
    replug: timeouts are 20 s now); libnetmd logged to stdout and named almost every error
    "Unknown Error". After a USB error the backend resets the port on close and the manager
    reopens the recorder itself, so a failed recording doesn't need a replug.
  - Easier picking of what to send (user asked, 2026-11-22): became M11b.
- M12 notes / follow-ups:
  - atracdenc 0.2.3 is vendored and run as a program (decisions/0007, third_party/atracdenc/UPSTREAM.md):
    LP2 = its ATRAC3 default (384-byte frames), LP4 = `atrac3_lp4` (192-byte); `AtracEncoder` keeps the
    RIFF data chunk. `makeDefaultMdEncoder()` picks GstPcmEncoder (SP) or AtracEncoder (LP).
  - Mode per recording: `MiniDisc.recordMode` (SP / LP2 / LP4 buttons in MD options, `M` cycles),
    remembered in QSettings, fixed while recording. Free / needed times are shown in the mode's time.
  - Not done: streaming into atracdenc's stdin (no temp WAV); encoding the next track while one
    uploads (done in M12c); per-track modes in one run; LP2 at 105 kbps
    (atracdenc can, NetMD's 0x90 wire format would need testing); an encode progress bar (atracdenc's
    stdout); install rules (the app has none yet: a package must put atracdenc next to the app or
    set MMM_ATRACDENC); a single track over ~3 hours makes a > 2 GB temporary WAV.
  - atracdenc's RIFF header undercounts its frames (the encoder's delay frames follow): trusting it
    would cut ~46 ms off every track's end, so the data is read to the end of the file.
- Q2 notes / follow-ups:
  - ExportView builds its menus (it has the actions): its panes emit `menuRequested(what, row, item,
    x, y)` from one right-click handler per list (`indexAt`: a row, else the empty space);
    `openContextMenu()` for `,` by pane. Export's BrowsePane uses `entriesFor` (add to the device first).
  - MiniDisc: `deleteTrackRows(rows)` so a right-clicked unmarked track is deleted alone (`d` still
    takes the marked ones); protected tracks can't be renamed / moved / deleted from the menu.
  - `Desktop.showFile` asks the file manager to select the file (`fileManagerShowItems`, passed in by
    main.cpp only; tests record "show:" + the URI); `openFolder` for directories; `OnDevice.pathAt(row)`.
  - Entries can be a choice (`checked`: ✓), e.g. the record mode.
- Q1 notes / follow-ups:
  - One menu (`AppContextMenu`, Main owns it) built from entries the views describe (`ContextMenus`
    singleton: the shared entry builders and the requests Main handles: tag editor, cover finder,
    go to album, new / rename / delete / export playlist, open folder). Keys through the KeyRouter
    (`ContextMenu` action: `,`, Menu, Shift+F10; the menu's list opts in with routeKeys).
  - Backend: `Playback.playItem` / `enqueueItem` (an artist / genre / album / playlist / track read on
    the query pool; requests apply in order, a replace drops pending ones; an empty playlist doesn't
    wipe the queue), `enqueueRows` (marked tracks in list order), `moveToPlayNext`
    (`PlayQueue::moveToNext`); `Library.removeFromPlaylist(id, path, occurrence)`,
    `addItemToPlaylist`, `createPlaylistFromItem`, `requestAlbumInfo` / `requestItemPaths`
    (+ `LibraryQuery::album`); `Desktop` singleton (open a folder; tests record instead).
  - Right-clicking a browse row outlines it without selecting it (what's on the right stays);
    content rows become the cursor's. Marked tracks: one of several marked → the entries are for all
    of them; an unmarked one → just it (marks stay).
- M15 notes / follow-ups:
  - You create your own Discord application (its name is what Discord shows: "Listening to …") and
    paste its ID in ☰ → Discord… (or the toggle when not set up); kept in QSettings (decisions/0009).
  - Core: `DiscordIpc` (finds the socket in the runtime dir, Flatpak / Snap / Vesktop dirs, the temp
    dirs; handshake, PING / PONG, frames split or joined; reconnects 5 → 60 s; a refused ID stops it;
    `MMM_DISCORD_IPC_DIR` overrides where it looks — ctest: an empty dir), `DiscordPresence` (the
    activity: "Song - Artist" (also the one-line status), the album under it, time bar in Unix seconds;
    cleared when paused / stopped; ≤ 5 updates per 20 s, changes within 250 ms merged, ≤ 2 s drift
    not re-sent). UI: `Discord` singleton (fed by PlaybackController + Player, position ticks too),
    `DiscordDialog`, the transport-bar toggle. Tests: `tests/core/fakediscord.h` (a stand-in Discord).
  - Not done: cover art (needs public URLs: Cover Art Archive once release MBIDs are stored); a
    project application ID for packaged builds (build-time, never committed); a "paused" status
    instead of clearing; buttons (e.g. a link to the release on MusicBrainz).
- M14 notes / follow-ups:
  - Built for Last.fm first, then switched to ListenBrainz before it shipped: Last.fm API keys need
    Last.fm Pro from 2026-12-01 (decisions/0008). No Last.fm code is left.
  - You paste your ListenBrainz user token (listenbrainz.org/settings); it's checked and kept in the
    keyring (QtKeychain, `SecretStore`), the user name and on/off in QSettings.
  - Core: `ScrobbleTracker` (the rules, fake-clock tests), `ListenBrainzClient` (JSON, the token in
    the Authorization header; `MMM_LISTENBRAINZ_URL` — ctest points it at a closed port), `Scrobbler`
    (account check, debounced "playing now", the queue in `pending_scrobbles` (v7, ≤ 5000; in
    `scrobbles.db` beside the library's), 50 per request ("single" / "import"), one request a second
    and the server's rate-limit window respected, retries 1 → 30 min, at once when the network is
    back (QNetworkInformation), a refused batch resent one by one (one refused listen dropped per
    try, so a server refusing everything can't empty the queue)). UI: `ListenBrainz` singleton
    (feeds it PlaybackController's `playStarted` / `currentTrackRetagged` and the Player's state),
    `ListenBrainzDialog` (☰ → ListenBrainz…, or the toggle when not connected), the transport-bar
    toggle.
  - Not done: a server address setting (Maloja, Koito and self-hosted ListenBrainz speak the same
    API); MusicBrainz ids in listens (the library doesn't read them yet); love / hate feedback; a
    listen history view; scrobbling files played from devices; plays queued under one account go
    to the next one connected (switching accounts is rare).
- M12c notes / follow-ups:
  - `MdEncodeWorker` on its own thread prepares tracks; `NetMdManager::pumpUpload()` is the one
    scheduler: upload the next track when it's ready, prepare one ahead of the upload (≤ 2 tracks
    on disk). A track that can't be prepared fails in its turn (the one before it still goes on; if
    that one fails, its error wins). Each recording has its own cancel flag and temp dir (shared with
    the threads), so a quick restart after a failure can't revive the old run's encode.
  - New item state "ready" (prepared, waiting). The progress line follows the track on the disc,
    or the next one while it's still being prepared.
  - Not done: streaming into atracdenc (still a temp WAV), lower priority for atracdenc if the
    recorder ever times out during a transfer, preparing more than one track ahead.
- M12b notes / follow-ups:
  - libnetmd patch 17: delete / move / erase wait for the final reply (through INTERIM) and read
    its status (a refusal used to count as done); `netmd_erase_disc` is new (netmd-js's command).
    Like netmd-js / Web MiniDisc, no cache / sync TOC around them; the header write (groups) is.
  - `MdTitle::headerAfterDelete` / `headerAfterMove` (pure, tested) keep the groups right; the worker
    reads the header fresh first (nothing is deleted or moved without it) and writes it only if the
    groups change. A group of one goes with its track (unless into another group); a group whose
    tracks are all deleted goes. A move whose new group numbers wouldn't fit 255 bytes is refused.
  - A move doesn't re-read the disc (the list is moved in place: a full read takes seconds per `J`);
    delete and wipe do (free time changes). `J` presses while one is on its way are dropped.
  - Not done: group editing (create / rename / delete / move a group), moving several marked tracks
    at once, undo, drag-and-drop reordering, a "protected track" warning before deleting one.
- M11b notes / follow-ups:
  - `SendToDevice` (QML singleton) is the one entry point: it routes to `MiniDisc::addItems`
    (per-session "To record") or `Sync::addItems` (persistent USB selection, one transaction via
    `DeviceManager::addSyncItems`) and reports counts in tracks for the toast. Single tracks are
    `SyncItem::Kind::Track` rows (kind "track": no migration).
  - USB adds resolve before storing: gone entries aren't stored, and a single track the selection
    already covers (its album) isn't stored again. Sends run one at a time, in order. Sending to a
    MiniDisc after a recording drops the done tracks first (like Record does).
  - `i` on the album page = the cursor track (like `e`); the whole album is the header button or
    `i` on the grid / browse row. A playlist sent from its page stays a playlist (its m3u is written).
  - Not done: marking on the playlist page and in search results (then `i` / the button would send
    the marked ones); track counts on Export browse rows; an artist → album → tracks picker in Export.
- Duplicate formats (user asked, 2026-11-22): "hide lower-quality duplicates". When an album has
  the same song in several formats (game soundtracks ship FLAC + MP3 side by side), browsing,
  playing, search and USB sync show one copy: lossless over lossy, then higher bitrate. Match on
  album + disc + track number + title (case-insensitive); keep the others in the DB (a "show all
  copies" toggle, and the tag editor still sees them). Core: a `LibraryQuery` option or a
  `preferred_track_id` computed after each scan; tests with generated FLAC + MP3 pairs. Found
  52 such pairs in the user's library (moved to `/mnt/ssd500/Media/Music-duplicates/` with a
  `MOVED-FROM-LIBRARY.txt` list, at the user's request), so this is for new ones.
- Packaging: the app sets its desktop file name (`music-md-manager`) but no .desktop file is
  installed, so Qt logs "qt.qpa.services: Failed to register with host portal … App info not found"
  at startup (harmless). Install `music-md-manager.desktop` (+ icon) with the AppImage/Flatpak work
  (Later), or locally in ~/.local/share/applications.
- M5b notes / follow-ups:
  - Theme should become user-configurable (user asked, 2026-11-21). `Theme` (src/ui/theme.h) is
    a constant C++ singleton today: give its properties NOTIFY signals, load values from QSettings
    or a theme file, and keep Main.qml's palette mapping as the single place roles are set.
  - Stubs in the UI waiting on core work: shuffle (PlayQueue shuffle order + MPRIS `Shuffle`);
    audio analyzer (now Q3); Discord / ListenBrainz toggles
    (M15 / M14); Export screen devices, sync and MD options (M8–M12).
  - MD Visualizer (main-menu tile 3): Phase 6b (V1-V3), on the `md-visualizer` branch.
  - Keyboard ideas not done: switch the browse dropdown mode from the keyboard (it needs a click
    today), `1`/`2` to open main-menu tiles, `gg`/`G`, `n`/`N`, a key-help overlay. All go in
    KeyRouter's tables + Main.handleAction().
  - Export's "tracks to export" can't list an artist's tracks: TrackListModel filters by album,
    playlist or search only (M9 needs an artist filter or a staging model).
  - Queue panel lost drag-to-resize (was a SplitView pane); fixed at 320 px.
