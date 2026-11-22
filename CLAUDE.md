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
  - `Theme` (C++ QML singleton): colours, spacing, radii, font sizes. Use its tokens, not literals.
    Fusion ignores the application palette: `Main.qml` maps Theme onto the window `palette`, so
    standalone components in tests render with default colours (only `Main` screenshots are themed).
- `src/app/` — `main.cpp` only: app setup and loads `MusicMdManager/Main`.
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
- Last.fm / Discord keys and secrets are supplied at runtime. **Never commit them.**
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
  is on the same thread). The app smoke test passes `--no-mpris`.
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
- QML: `TestCase` is invisible by default; set `visible: true` or mouse events go nowhere.
  No translations are loaded, so don't use `qsTr("%n item(s)")` plural forms (prints "(s)").
  Ids are `int` in QML (row ids stay < 2^31).
  `someQStringList.indexOf(x)` in QML compiles (qmlcachegen) to C++ that fails `-Wconversion`:
  use `.includes(x)`.
- Tests must never touch the user's real library: ctest sets `XDG_DATA_HOME`/`XDG_CACHE_HOME`
  to `build/*/test-home`, and the app smoke test uses `--database` + `--no-scan`.
- Tests must never reach the internet: network clients get a base URL (`setBaseUrl`) pointed at
  `tests/core/testhttpserver.h` (loopback server). ctest also sets `MMM_MUSICBRAINZ_URL` /
  `MMM_COVERARTARCHIVE_URL` to a closed local port, so anything that slips through fails offline.
  moc can't parse raw strings containing `//` (URLs): write such JSON fixtures as plain literals.
- SPDX header `GPL-3.0-or-later` on every source file.
