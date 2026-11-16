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
- `src/app/` — `main.cpp` only: app setup and loads `MusicMdManager/Main`.
- `tools/mmm-play/` — terminal player on `mmm::Player` (manual testing / debugging):
  `./build/debug/tools/mmm-play/mmm-play FILE...` (space, ←/→, +/-, n/p, q).
- `tools/mmm-tags/` — prints what `TagReader` sees: `mmm-tags [--cover DIR] [--db FILE] PATH...`
  (files or folders, recursive).
- `tests/core/` — Qt Test, one `tst_*.cpp` per class, `QTEST_GUILESS_MAIN`.
- `tests/qml/` — Qt Quick Test, `tst_*.qml` files, picked up by `tst_qml`.
- `docs/` — plan and design notes (udev rules, device notes, etc.).

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
- Database: one `mmm::Database` per thread (Qt SQL connections are thread-bound).
  Schema changes = **append** a migration step in `database.cpp` (`kMigrations`); never
  edit a released step. Write methods use SAVEPOINTs so callers can batch in one outer
  transaction. Call `finish()` on queries with `RETURNING` (or leave scope) before
  RELEASE, or SQLite errors with "SQL statements in progress".
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
- SPDX header `GPL-3.0-or-later` on every source file.
