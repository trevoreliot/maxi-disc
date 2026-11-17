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
- Desktop/online: MPRIS media keys, Last.fm scrobbling, Discord "now playing" status

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
| Online services | Last.fm scrobbling, Discord Rich Presence. Requires a Last.fm API key + secret and a Discord application ID (supplied by user at runtime/config, never committed) |
| Licence | GPL-3.0-or-later (Qt LGPLv3 + linux-minidisc GPL). Verify linux-minidisc is GPL-2.0-or-later before M10 |

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
| Last.fm | QNetworkAccessManager; QCryptographicHash (MD5 signing) | Desktop auth flow; scrobble after 50% or 4 min for tracks > 30 s; offline queue in SQLite |
| Discord | QLocalSocket to $XDG_RUNTIME_DIR/discord-ipc-0 | No SDK. Also check Flatpak Discord socket path. SET_ACTIVITY with track, artist, album, elapsed |
| Secrets | QtKeychain | Last.fm session key |
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
│  MPRIS,Last.fm database,FTS   fetch, embed  diff, copy,  SP/LP      │
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
- Last.fm and Discord listen to the Player's track-change signals; playback never waits on the network.
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
- [ ] **M5b UI redesign + Vim/Evil keyboard navigation** — Scheduled right after M5 so later
  milestones' screens (tag editor, artwork, sync, MiniDisc) are built to the final layout once.
  Done when: the shell/layout follows the user's drawings (to be supplied in `docs/design/`);
  a keyboard focus model across panes and lists; Vim/Evil-style keys active whenever a text field
  doesn't have focus: `/` focuses search (Ctrl+F kept), `h`/`j`/`k`/`l` move left/down/up/right
  (between panes and within lists/grids), Enter activates, Esc backs out / leaves search; key
  bindings defined in one place (C++ or a single QML component) so more can be added (e.g. `gg`/`G`,
  `n`/`N` next/prev search hit, Space play/pause) without touching every view; qmllint clean and
  QML tests for the key handling. Visual polish/animation stays in "Later". User test: navigate
  browse → album → play → queue using only the keyboard; the UI matches the drawings.
- [ ] **M6 Tag editor** — Done when: edit single and multiple tracks; writes to files and DB. User test: edit a test copy of an album, then verify in another app.

### Phase 3: Artwork
- [ ] **M7 Artwork finder** — Done when: finds albums with no art; MusicBrainz lookup; Cover Art Archive download; saves cover.jpg and embeds; review dialog. User test: fix 20 albums missing art.

### Phase 4: USB sync
- [ ] **M8 Device detection + profiles** — Done when: detects USB mounts; per-device profile (folder layout, playable formats, transcode target); built-in profiles for USB flash drive and generic DAP. User test: plug in a thumb drive and create a profile.
- [ ] **M9 Sync engine** — Done when: sync plan (add / update / remove) shown before running; copy with progress; transcode cache; playlists written. User test: sync a playlist, change it, sync again; only the changes copy.

### Phase 5: MiniDisc
- [ ] **M10 NetMD basics** — Done when: libnetmd linked; detect the MZ-NE410; read disc TOC and titles; rename disc and tracks; udev rule documented. User test: read and retitle a scratch disc.
- [ ] **M11 SP upload** — Done when: decode to 44.1 kHz 16-bit PCM via GStreamer; upload as SP; progress and cancel. User test: burn a short album in SP, then play it on the deck.
- [ ] **M12 LP2 / LP4 upload** — Done when: atracdenc encoding; mode chosen per job; disc-space estimate before burning. User test: burn in LP2 and LP4; check sound and titles.
- [ ] **M13 (Optional, deferred) netmd-js extras** — Port device-quirk fixes only if the MZ-NE410 needs them. netmd-exploits features wait until requested.

### Phase 6: Online services (depend only on M5; may be pulled earlier)
- [ ] **M14 Last.fm scrobbling** — Done when: connect account in settings; now-playing updates; scrobbles follow Last.fm rules; queued scrobbles retry when back online; unit tests on scrobble timing. User test: play a few tracks, including some offline; all appear on Last.fm.
- [ ] **M15 Discord status** — Done when: shows track, artist, album and progress while playing; clears on pause or quit; reconnects if Discord restarts; on/off toggle. User test: Discord profile shows what's playing and clears on stop.

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
  - **Open (next task, before M5b): rare hang on rapid play→stop.** ~1 in 100+ test runs
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
