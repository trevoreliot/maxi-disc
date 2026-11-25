# CLAUDE.md

Music library manager (playback, tagging, artwork, USB sync, NetMD MiniDisc upload).
C++20 + Qt 6 Qt Quick/QML, Linux only. GPL-3.0-or-later.

**The plan is `docs/PLAN.md`. Read it first.** Work ONE milestone per session, in order.
A milestone is done only when its "Done when" criteria pass AND the user has run its
"User test". Then tick the box, commit, and stop. Put new ideas or scope creep under
"Follow-ups" in the plan instead of growing the current milestone.

## Build & test

```sh
cmake --preset debug            # Debug + ASan/UBSan, Ninja, build/debug
cmake --build --preset debug
ctest --preset debug            # unit tests, Qt Quick tests, app smoke test, qmllint
./build/debug/src/app/music-md-manager

cmake --preset release && cmake --build --preset release   # RelWithDebInfo, no sanitizers
```

Done-when gate for every milestone: debug preset builds with **zero warnings**
(`-Werror` is on), `ctest --preset debug` is fully green, qmllint clean.

## Layout

- `src/core/` — `mmm_core` static lib. **C++ and Qt Core only (plus non-GUI Qt modules
  like Sql/Network/DBus). No QML, no Gui.** Must be testable headless. Namespace `mmm`.
- `src/ui/` — `mmm_ui` QML module (URI `MusicMdManager`). C++ models/bridges exposed
  with `QML_ELEMENT` / `QML_SINGLETON`; QML files under `src/ui/qml/`.
  - `Library` (QML singleton): owns the `LibraryScanner` and a read-query thread pool.
    **Construct it before the QML engine** (main.cpp, tst_qml's Setup); QML gets it via `create()`.
  - `librarymodels.h`: list models load through `Library::runQuery()` on the pool (each pool
    thread has its own `Database`), never on the UI thread. `LibraryListModel` drops stale
    results (generation counter) and refreshes in place when row ids are unchanged.
  - `CoverImageProvider` ("image://cover/…"): folder art, else embedded art; resized to
    128/256/512/1024 buckets and cached as JPEG in `AppPaths::coverCacheDir()`.
  - Read-side SQL lives in core `LibraryQuery` (tested headless, incl. a 100k-track timing test).
  - `Playback` (QML singleton): wraps core `PlaybackController` + owns `MprisService`; persists
    volume/repeat/ReplayGain in QSettings. Construct before the engine, like `Library`.
  - Shell (M5b, after the drawings in `docs/ui-draft/`, local only): `Main.qml` = `TopChrome` header, a `StackLayout` of
    screens (`MainMenuView`, `LibraryView`, `ExportView`), `QueuePanel` docked right, `TransportBar`
    footer. Screens stay alive, so their state (browse selection, open album) survives switching.
  - Keyboard: `KeyRouter` (C++, keyrouter.cpp) is the **one** place keys are bound; it filters the
    window's key events into actions. `Main.handleAction()` handles global ones and forwards
    movement to the queue (if `queueFocused`) or the current screen's `handleAction()`, which tracks
    its own panes and returns false when "move right" runs off its edge. Views expose
    `moveSelection()` / `activateCurrent()` / `paneActive`; list rows use `NavRow`, panes `FocusBar`.
    Don't add QML `Shortcut`s or `Keys` handlers for navigation: add a binding + action instead.
    **Popups must take focus (`focus: true`)**, or the router keeps handling keys (incl. Esc) under them.
  - Tag editing (M6): core `TagWriter` writes only the fields set in `TagChanges` (copy +
    rename, see docs/decisions/0002); `editTracks()` = write + re-read + `Database::updateEditedTrack`
    (keeps folder id, carries folder art to a renamed album, recomputes album year). UI: `Library::editTags`
    (one-thread write pool) → `tracksEdited` (Playback refreshes the queue) + `changed`; `TagEditor` (QML
    type) backs `TagEditorDialog` (Main owns it). Keys: `e` cursor/marked tracks, `E` all, `v` mark (album page).
  - Artwork finder (M7): core `ArtworkFinder` (MusicBrainz search/lookup → Cover Art Archive
    fronts; `HttpFetcher` queues/rate-limits/retries) + `ArtworkSaver` (cover.jpg in folders only
    this album uses + embedded via `TagChanges::frontCover`; decisions/0003). UI: `ArtFinder` (QML
    type, the dialog's model) → `Library::saveAlbumArt` (write pool) / `setAlbumArtSkipped`;
    `ArtworkFinderDialog` (Main owns it). Keys: `A` all missing, `a` current album, in the dialog
    `s` skip / `r` retry. The dialog gets routed keys because its content has `routeKeys: true`
    (KeyRouter's popup opt-in); Main forwards actions to it first. `CoverLoader::bumpRevision()`
    after art changes in place, so QML reloads it.
  - Devices (M8): core `UdisksMonitor` (UDisks2 on the system bus; filter rules in `parse()`) →
    `DeviceManager` (joins devices with `device_profiles`, own Database; mount/save/delete by
    device id) ; `DeviceProfile` + `DeviceProfiles::decide` (copy or MP3) + `PathLayout::render`
    (FAT-safe paths) are what M9 syncs by (decisions/0004). UI: `Devices` QML singleton (the device
    list model; construct before the engine; tests use `.monitor = false` + `setTestDevices`),
    `ProfileEditor` + `ProfileEditorDialog` (Main owns it). Export keys: j/k devices ↔ browse, l / h
    into the right pane and back to the left pane it came from, Enter = set up/edit profile (a recorder:
    into its tracks), `m` mount/unmount.
  - USB sync (M9): core `SyncPlanner` (`plan()` gathers DB + device scan, `diff()` is pure:
    Add/Update/Move/Remove/Forget against `device_manifest`, the files the app wrote; others are
    never touched, decisions/0005) → `UsbSync::run` (temp `.mmm-part-*` + fsync + rename, per-file
    manifest commit; transcodes via `Transcoder` + `TranscodeCache` on a small pool) → `SyncJob`
    (worker thread, own Database, queued signals). `ArtShrinker` resizes art with GStreamer (no
    QtGui in core). UI: `Sync` QML singleton (construct after Devices; tests pass a temp `cacheDir`)
    + `SyncPane.qml`. Export keys: `i` add, `x`/Delete remove, `p` plan, `S` sync, Esc closes the plan.
  - On-device browser (M9b): core `DeviceBrowser` (DeviceScanner + TagReader, tags cached in
    `device_file_tags` by size + mtime; `LibraryMatcher` decides "in library": synced from it, or same
    artist/album/title, or artist/title ±3 s) on its own worker. UI: `OnDevice` QML singleton (reads
    only while `active`, pauses during a sync, refreshes after) + `OnDeviceModel` (artist → album → track
    rows, folding, cursor kept by key) + `OnDevicePane.qml`. `o` switches the Export right pane.
  - NetMD (M10): vendored libnetmd (`third_party/libnetmd`, patches in its UPSTREAM.md; built without
    our flags). Only `libnetmdbackend.cpp` includes it, behind `NetMdBackend`; `NetMdManager` runs every
    command on one worker thread (generation drops stale results; exit waits ≤ 5 s); `NetMdMonitor`
    polls USB (own libusb context). `MdTitle`: ASCII cleaning + the disc header ("0;Title//1-3;Group//").
    UI: `MiniDisc` QML singleton (puts recorders into Devices; follows Devices.current) + `MiniDiscPane`
    + `RenameMdDialog` (Main owns it). Keys: n / Enter rename track, N disc, R refresh. Tests use
    `tests/core/fakenetmd.h` (never USB); the UI tests construct MiniDisc with it.
  - MiniDisc recording (M11): `MiniDisc` keeps a per-session "to record" queue (`MdQueueModel`; `i`
    adds browse rows), `MdUploadPlanner` says if it fits; `NetMdManager::startUpload` → `pumpUpload()`
    schedules both threads (M12c): `MdEncodeWorker` ("netmd-encode") prepares a track with
    `MdAudioEncoder` (`GstPcmEncoder`: S16BE 44.1 kHz stereo) one ahead of the worker's `uploadOne`
    (backend `uploadTrack`: secure session, title, commit → re-read disc). Per recording: a run id,
    its own cancel flag and a shared temp dir; item states Pending → Decoding → Ready → Uploading → Done.
    Cancel = after the current track. Keys (recorder selected): `o` disc ↔ to record, `x`/`X`
    remove/clear, `S` record/cancel. Tests: `FakeEncoder` + the fake recorder's `uploadTrack`
    (fakenetmd.h; their `events` show what overlapped).
  - LP2 / LP4 (M12): vendored atracdenc 0.2.3 (`third_party/atracdenc`, an ExternalProject built with
    core, binary next to the app; decisions/0007) run by core `AtracEncoder` (QProcess; GStreamer → WAV →
    atracdenc → the RIFF data chunk = raw ATRAC3 frames, 384 / 192 bytes); `makeDefaultMdEncoder()` picks
    by mode; `mdWireSpec()` = wire/disc formats (static_asserted in LibNetMdBackend). UI: `MiniDisc.recordMode`
    (QSettings; tests pass `persistSettings = false`), `M` cycles. `tst_atracencoder` runs the real binary.
  - Disc editing (M12b): `NetMdBackend::deleteTrack` / `moveTrack` / `eraseDisc` (libnetmd patch 17);
    `NetMdManager::deleteTracks` / `moveTrack` / `eraseDisc` on the worker rewrite the groups via
    `MdTitle::headerAfterDelete` / `headerAfterMove` (the recorder doesn't). UI: `MdTrackModel` marks,
    `MiniDisc.deleteTracks()` / `moveTrack(±1)` / `eraseDisc()`, `ConfirmDialog` (Main owns it; ExportView
    asks via `confirmRequested`). Keys (disc view): `v` mark, `d` / `x` delete, `J` / `K` move, `D` wipe.
    QML tests put the shared fake back with `fakeRecorder.reset()`.
  - ListenBrainz (M14; replaced Last.fm, decisions/0008): core `ScrobbleTracker` (when a play counts;
    injectable clocks) → `Scrobbler` (token check, playing now, the `pending_scrobbles` queue, retries,
    ≤ 1 request/s) → `ListenBrainzClient` (JSON, `Authorization: Token …`; base URL `MMM_LISTENBRAINZ_URL`,
    ctest: a closed port). The token via `SecretStore`: QtKeychain, or `PassSecretStore` (the `pass` program,
    M14b) when there's no keyring (`makeDefaultSecretStore`; QSettings `secrets/backend`,
    `MMM_SECRET_BACKEND`; tests `MemorySecretStore`, fake `pass` scripts). UI:
    `ListenBrainz` singleton (construct after Playback; tests pass `persistSettings = false`, a memory
    store, an `openUrl` that only records, so no browser opens), `ListenBrainzDialog`, the transport-bar
    toggle (`PillToggle.autoToggle: false`). `PlaybackController::playStarted` / `currentTrackRetagged`
    tell a new play from a tag edit. tst_qml's Setup runs a stand-in ListenBrainz ("GOOD" is alice's token).
  - Discord status (M15): core `DiscordIpc` (QLocalSocket to the Discord app's `discord-ipc-N`; framing,
    handshake, reconnect; `MMM_DISCORD_IPC_DIR` overrides where it looks) + `DiscordPresence` (the
    activity JSON, merging + 5-per-20-s limit; no sockets) → `Discord` singleton (construct after
    Playback; QSettings `discord/appId` / `discord/enabled`; tests: `persistSettings = false`, `ipcDirs`
    = the fake's dir) + `DiscordDialog` + the transport-bar toggle. decisions/0009. Tests use
    `tests/core/fakediscord.h`; tst_qml's Setup runs one (application id 123456789012345678).
  - Send to device (M11b): `SendToDevice` (QML singleton; construct after Devices, Playback, Sync,
    MiniDisc; its constructor has no default argument so QML uses `create()`) → `MiniDisc::addItems` /
    `Sync::addItems` (counts via `DeviceAddResult`) → `notice` → `Toast` in Main. USB selections take
    `SyncItem::Kind::Track`. Keys: `i` (`AddToDevice`) on Library and Export, `I` the play queue
    (anywhere); `SendToDeviceButton` shows the disabled reason as its tooltip.
  - `Theme` (C++ QML singleton): colours, spacing, radii, font sizes. Use its tokens, not literals.
    Fusion ignores the application palette: `Main.qml` maps Theme onto the window `palette`, so
    standalone components in tests render with default colours (only `Main` screenshots are themed).
- `src/app/` — `main.cpp` only: app setup and loads `MusicMdManager/Main`. It's a `QApplication` (links
  Widgets) only so Plasma gives Qt Quick's `FileDialog` the native KDE dialog (multi-select, type-to-find).
- `tools/mmm-play/` — terminal player on `mmm::Player` (manual testing / debugging):
  `./build/debug/tools/mmm-play/mmm-play FILE...` (space, ←/→, +/-, n/p, q).
- `tools/mmm-tags/` — prints what `TagReader` sees: `mmm-tags [--cover DIR] [--db FILE] PATH...`
  (files or folders, recursive).
- `tools/mmm-scan/` — manage library folders and scan them: `mmm-scan --add DIR`, `--scan`,
  `--watch`, `--list`, `--remove DIR`. Uses the app's DB (`AppPaths::defaultDatabasePath()`,
  `~/.local/share/music-md-manager/library.db`) unless `--db FILE`.
- `tests/core/` — Qt Test, one `tst_*.cpp` per class, `QTEST_GUILESS_MAIN`.
- `tests/ui/` — C++ tests that need the UI layer (models, cover provider), plus
  `fixturelibrary.h`: a small DB with real folder + embedded art, shared with the QML tests.
- `tests/qml/` — Qt Quick Test, `tst_*.qml` files, picked up by `tst_qml` (its Setup builds
  the fixture library). Only `Kid A/01.flac` exists on disk (real audio, tags = its DB row): tests that
  edit its tags must put them back. `tst_navigation.qml` drives the whole shell by keyboard (the M5b user
  test path); `tst_keys.qml` covers the key → action table. `MMM_SCREENSHOT_DIR=/tmp/x ./build/debug/tests/tst_qml` saves a PNG
  per view: use it to *look* at UI changes.
- App dev flags: `--database FILE`, `--no-scan`, and hidden `--screenshot FILE [--screenshot-delay MS]`.
- `tools/dev/catch-stall.sh BINARY [FUNCS]` — reruns a test 3-at-a-time and dumps all thread
  backtraces (gdb) if one stalls. Use it on any intermittent hang instead of guessing.
- `docs/` — plan and design notes (udev rules, device notes, etc.).
  `docs/decisions/NNNN-*.md`: decision records (context, evidence, decision, how to revisit).
  Add one for any decision the user may want to revisit later.

## Conventions

- Strict compile flags live on the `mmm_build_flags` INTERFACE target. Link it
  `PRIVATE` into every target we own. Don't loosen flags to make a warning go away. Fix it.
- `QT_NO_KEYWORDS`: write `Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT`, not `signals` / `slots` / `emit`.
- `QT_NO_CAST_FROM_ASCII`: use `QStringLiteral` / `u"..."_s` / `QLatin1StringView`.
- RAII everywhere; no owning raw pointers outside Qt parent/child ownership.
- Long work (scan, fetch, copy, transcode, MD upload, network) runs on QThreadPool;
  results come back via queued signals. Workers never touch QML objects.
- ListenBrainz tokens and Discord keys / secrets are supplied at runtime. **Never commit them.**
- QML files inside the `MusicMdManager` module must not `import MusicMdManager`
  (qmllint flags it); same-module types are visible already.
- Add a test for every new core class. Tests that need media generate it with
  `tests/core/testmedia.h` (GStreamer `audiotestsrc`). Never commit audio files.
- Player tests use `PlayerOptions{.audioSink = "fakesink sync=true"}`: real-time but silent.
- GStreamer: bus messages reach `Player` through a sync handler and a queued
  invocation (no GLib main loop needed). A handler returning `GST_BUS_DROP` **owns** the
  message. Stale messages are dropped through `Player::Private::generation`.
- Playback: **every** playback/queue command goes through `PlaybackController` (UI, MPRIS, and
  later the Emacs D-Bus interface): no playback logic in QML. The queue wins over GStreamer:
  if the gapless track that started isn't the queue's next, the controller plays the queue's.
- `Player` is a facade over `detail::PlayerEngine` (src/core/playerengine.*), which owns the
  playbin3 pipeline on its own thread (docs/decisions/0001). **No GStreamer call may run on the
  caller's thread.** Commands carry a sequence number and are acked; events carry the stream
  epoch and are dropped if stale or from a replaced engine. A command unacked for 3 s (or a
  start not reaching Playing in 6 s) replaces the engine and resumes; >3 recoveries in 30 s →
  error. Abandoned engines are never joined/destroyed while running (graveyard). Test stalls
  with `PlayerOptions::beforeStateChange` (see tst_player's StallInjector).
- Gapless (`Player`): the next URI is set from `about-to-finish` on a streaming thread (mutex).
  Pitfalls already hit: (1) the *first* STREAM_START after a load is the current track even if
  the next was committed already: only later ones are advances; (2) committing overwrites
  playbin3's `uri`, so `stop()` must restore it; (3) never pass the current URI as its own next
  (self-gapless ends early), so loops restart at EOS; (4) short tracks are read instantly, so
  give `Player::setUpcomingSources()` *all* following tracks, not one at a time; (5) **never
  change pipeline state while handling a STREAM_START/advance**: playbin3 can deadlock (main
  thread in set_state(READY) vs a streaming thread in a playbin3 pad probe). Corrections are
  deferred (`PlaybackController::deferCorrection`). Always run the release tests too: debug
  timing hid (1) and (2); the deadlock showed mostly in debug.
- Gate commands: check ctest's **exit code** (`ctest --preset debug && …`), never grep its
  output for "passed" ("Failed" matches too: an M5 commit slipped through that way).
- MPRIS tests run under `dbus-run-session` (private bus); client calls must be async (the service
  is on the same thread). The app smoke test passes `--no-mpris` and `--no-devices`.
- Sync tests (`tst_syncplanner`, `tst_usbsync`, `tst_transcoder`, `tst_artshrinker`) use a temp dir as
  the mounted device and generated media (`testmedia.h`: `generateTone`, `generateImage`).
- UDisks2 tests (`tst_udisksmonitor`, `tst_devicemanager`) also run under `dbus-run-session`
  against `tests/core/fakeudisks.h` (a fake service on the private bus): never the real system
  bus, never real devices. Build its volumes with `FakeUdisks::volume()` (GCC rejects partial
  designated initializers).
- QML tests that send real keys to a `Main` window: wait until it has keyboard focus
  (`win.active && win.activeFocusItem !== null`, re-requesting activation), not just `active`.
- Database: one `mmm::Database` per thread (Qt SQL connections are thread-bound).
  Schema (v2: + playlists, items stored by path) changes = **append** a migration step in `database.cpp` (`kMigrations`); never
  edit a released step. Write methods use SAVEPOINTs so callers can batch in one outer
  transaction. Call `finish()` on queries with `RETURNING` (or leave scope) before
  RELEASE, or SQLite errors with "SQL statements in progress".
- `LibraryScanner`: scans run in `runScan()` on a pool thread with their own `Database`;
  the worker takes everything by value and only talks back through queued calls. Never
  remove tracks after an interrupted walk, and never treat a missing library *root* as
  deleted (unmounted drive). Track `file_mtime` is in **milliseconds**.
- Tests that create many files use hard links (`::link`) to one tagged file: fast, no space.
- moc preprocesses test files: no raw string literals (`R"(...)"`) inside macro
  arguments like `QVERIFY`/`QCOMPARE`. Assign to a variable first.
- Tag tests: GStreamer `mp4mux` writes track-level MP4 tags that TagLib/iTunes don't read,
  so MP4 tag coverage comes from TagLib-written files. MP4 cover art has no picture type.
- Sanitizer defaults (full unwind + LSan suppressions) are compiled into debug
  executables from `src/sanitizer/sanitizer_defaults.cpp`, so a plain run of the binary
  is checked the same way as ctest. Suppress only verified third-party leaks, with a
  comment saying where they were seen. New executables: call `mmm_add_sanitizer_defaults()`.
- ctest runs headless (`offscreen`), so GPU-driver issues only show up in a real run.
  Run the app on the real display as part of each milestone's user test.
- [ ] QML: `TestCase` is invisible by default; set `visible: true` or mouse events go nowhere.
  No translations are loaded, so don't use `qsTr("%n item(s)")` plural forms (prints "(s)").
  Ids are `int` in QML (row ids stay < 2^31).
  `someQStringList.indexOf(x)` in QML compiles (qmlcachegen) to C++ that fails `-Wconversion`:
  use `.includes(x)`.
- Tests must never touch the user's real library: ctest sets `XDG_DATA_HOME`/`XDG_CACHE_HOME`
  to `build/*/test-home`, and the app smoke test uses `--database` + `--no-scan`.
- Tests must never reach the internet: network clients get a base URL (`setBaseUrl`) pointed at
  `tests/core/testhttpserver.h` (loopback server; it reads POST bodies too). ctest also sets
  `MMM_MUSICBRAINZ_URL` / `MMM_COVERARTARCHIVE_URL` / `MMM_LISTENBRAINZ_URL` to a closed local port, so
  anything that slips through fails offline. Tests never use the real keyring or open a browser, and
  never find the real Discord (ctest sets `MMM_DISCORD_IPC_DIR` to an empty dir) or the real pass store
  (ctest: `MMM_SECRET_BACKEND=pass`, `PASSWORD_STORE_DIR` / `GNUPGHOME` empty dirs).
  moc can't parse raw strings containing `//` (URLs): write such JSON fixtures as plain literals.
- SPDX header `GPL-3.0-or-later` on every source file.
