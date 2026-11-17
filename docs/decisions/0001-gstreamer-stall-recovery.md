# 0001 — Recover from GStreamer stalls instead of fixing them

- **Status:** accepted and implemented (2026-11-16): `src/core/playerengine.*`, `Player`'s
  watchdog; tests `tst_player` stalledEngineIsReplaced / destroyWhileStalledDoesNotHang /
  repeatedStallsGiveUp
- **Decided by:** the user, on Claude Code's recommendation, during M5 follow-up work
- **Revisit if:** we want to report/fix the underlying GStreamer bugs, or a GStreamer
  upgrade makes the stalls disappear (then the recovery code is just a safety net)

## Context

While stress-testing gapless playback (M5) we found **three different deadlocks/stalls, all
inside GStreamer's own `playbin3` / decodebin3 machinery**, all triggered by changing the
pipeline state rapidly (play → stop, play → switch track, state change during a gapless
switch). GStreamer 1.28.8, Arch/CachyOS, PipeWire; reproduced with `fakesink sync=true`, so
not audio-device related. Rates are from our test suites (`tst_playbackcontroller`), which
churn state far faster than a person does.

| # | Trigger | What the threads were doing | How often |
|---|---|---|---|
| 1 | `set_state(READY)` while handling a gapless STREAM_START | UI thread in playbin3 `change_state` waiting on a `GCond` while holding playbin3's lock; a `multiqueue:src` streaming thread inside a playbin3 **pad probe** waiting for that lock | ~1 in 3 debug runs of the boundary test before we worked around it |
| 2 | `stop()` right after `next()` (play → READY within ms) | UI thread in `gst_pad_stop_task` waiting for a stream lock; the holder, a `typefindelement` thread, blocked in **`read()` inside filesrc** (a regular file can't block, so the fd probably no longer referred to the audio file: a double close + fd reuse?) | ~1 in 100+ runs |
| 3 | queue change shortly after starting a track | a `typefind` thread in its "have-type" callback → `gst_element_sync_state_with_parent` waiting for a **parent state lock**; other streaming threads queued behind; UI thread alive but playback never progressed | caught once in ~120 runs |

Evidence (backtraces, `/proc` fd listings) was gathered with `tools/dev/catch-stall.sh`.
GStreamer frames were unsymbolized (debuginfod resolved GLib but not GStreamer).

We worked around #1 in our code (never change state while handling an advance: see
`PlaybackController::deferCorrection`), but #2 and #3 are in GStreamer internals and testing
can't enumerate every such race.

## Decision

Make the app **robust to GStreamer stalls** rather than chasing each one:

1. **No GStreamer calls on the UI thread.** `Player` keeps its public API but runs every
   GStreamer call (state changes, seeks, property sets) on its own engine thread.
2. **Stall recovery.** If an engine command doesn't complete, or a requested start never
   reaches PLAYING, within a few seconds, the engine (thread + pipeline) is abandoned and a
   fresh one replays the player's state (source, upcoming tracks, volume, ReplayGain,
   position, playing/paused). A stuck engine is never joined or destroyed (that would block);
   if it un-sticks, it shuts itself down. Repeated stalls in a short time give up with an error
   instead of looping.
3. **A regression test forces a stall** through a test-only hook (`PlayerOptions`) and checks
   that the UI thread never blocks and playback resumes.

## Consequences

- A GStreamer deadlock becomes a few seconds of silence plus a log warning, not a frozen app.
- An abandoned engine's thread may stay blocked until the process exits (bounded by the
  give-up limit). It holds no UI resources.
- `Player`'s internals become asynchronous; its public getters return cached state that's
  updated from the engine. Changing `Player` needs care: see CLAUDE.md.
- The underlying GStreamer bugs remain unreported for now.

## Pursuing the GStreamer bugs later

1. Reproduce: `tools/dev/catch-stall.sh -n 40 -t 60 build/debug/tests/tst_playbackcontroller`
   (on a commit before this decision, or with recovery disabled: the stall still happens, the
   recovery just hides it). Signatures 2 and 3 are rare; #1 was ~1 in 3 with the boundary
   test before `deferCorrection` (commit `0b9dbd9` has the unmitigated code).
2. Symbols: `DEBUGINFOD_URLS` is configured (Arch + CachyOS) but didn't return GStreamer
   symbols; alternatively build GStreamer from source (meson, `-Dbuildtype=debug`) and run
   with `LD_LIBRARY_PATH` pointing at it.
3. Logs: `GST_DEBUG=playbin3:6,uridecodebin3:6,decodebin3:6,urisourcebin:5,filesrc:6,typefind:5`
   with `GST_DEBUG_FILE`, plus `GST_DEBUG_DUMP_DOT_DIR` for pipeline graphs at the stall.
4. A minimal upstream repro would be a C program: playbin3 + `fakesink sync=true` + a few short
   local FLAC/MP3 files, setting `uri` from `about-to-finish`, and a loop doing
   play → (wait for STREAM_START) → set_state(READY) at random small delays. Report at
   https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues with the backtraces.

## Result (2026-11-16)

Stress run after implementing recovery: `tools/dev/catch-stall.sh -n 40 -t 60
build/debug/tests/tst_playbackcontroller` → 120/120 runs clean, no hung process, no failed
test. 3 runs logged a recovery ("playback did not start within 6000 ms") in the two tests that
churn state hardest, and those runs took ~6 s longer than usual (24.7 s vs 18.6 s): real
GStreamer stalls, now survived with a few seconds of delay instead of a hang. So the
underlying bugs still happen (~1 in 40 runs of this suite); recovery is doing its job.
