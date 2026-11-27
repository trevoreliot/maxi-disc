# V3 MD Visualizer: the MiniDisc player — design

Plan for PLAN.md's **V3 The MiniDisc player** (Phase 6b). Gemini 4 Neon drafted it (2026-11-26).
Claude then checked it against the code, and changes from Neon's draft are listed at the end. The
PLAN.md "Done when" text is the contract. This file is the how. If the two disagree, PLAN.md wins:
fix this file.

## Structure

```
VisualizerView.qml
├── left column: BrowsePane + crumb + MdPile (as V1/V2)
└── MdDeck.qml (new; replaces the stage Item + GridLayout)
    ├── MdCaseStage.qml (V2, extended: an empty case while its cartridge is in the player; Play / Eject buttons)
    ├── MdPlayer.qml (new: the player)
    │   ├── side panel (Matrix4x4 shear) → MdSideLcd.qml (new)
    │   └── MdCartridge (the loaded disc; its SpinningDisc follows Playback)
    └── flyCart: MdCartridge (z above both; visible only while a cartridge travels)
```

- **`MdDeck`:**
  - Lays out the case area and the player: stacked when `height >= 500 || width < 520`, with 52 %
    for the case and 48 % for the player; otherwise side by side, half and half.
  - Owns the insert / eject phases, the flying cartridge (in its own coordinates, rects mapped from
    the case area and the player just before each step) and the loaded disc.
  - VisualizerView keeps its V2 pile logic (`takeOut`, `putBackCase`, `pileIndexOf`) and reaches the
    case area through `deck.caseStage`. `caseArea` stays as an alias for the tests.
- **Scale:** the player's body is about 1.12 × the case height, so the cartridge in the player is the
  same size as in its case. It doesn't grow or shrink in flight. The minimum case height drops from
  150 to 120 px for 960 × 600 with the queue open.

## The player (flat 2D, after the reference photos; no maker's marks)

Everything is in proportion to the body height `H`.

- **Face, seen straight on (1.01 H × 1.00 H):**
  - A glossy control bezel down the left (0.12 H), with a chrome seam.
  - The lid (0.89 H wide, rounded right corners).
  - A recessed bay under it (0.78 H × 0.83 H), with a spindle ring.
- **OPEN slide:** a silver tab on the top edge near the left corner. It slides right when used, and
  it's Eject.
- **Keys down the bezel:**
  - DISPLAY (cycles elapsed → remaining → title), VOL − and VOL + (round keys);
  - ■ stop and ❚❚ pause (silver keys);
  - the jog lever at the bottom-left corner. Its top end (or wheel up) is previous, its middle is
    play / pause, its bottom end (or wheel down) is next. The wheel is debounced (120 angle units).
  - Each key is drawn to scale, with a hit target of at least 22 × 22 px.
  - T MARK, REC and HOLD are left off.
- **Side panel (0.18 H):** the left edge seen obliquely. An upright item gets an affine shear,
  `Qt.matrix4x4(1,0,0,0, k,1,0,−k·sideW, 0,0,1,0, 0,0,0,1)` with k = 0.35, which both renderers
  draw. A parallelogram `Shape` gives it a top edge.
- **`MdSideLcd`:** inside the side panel (so the shear tilts it), upright:
  - **The two-bar meter:** two columns of 10 segments. One is the bass (bands 0–9), the other mid
    and treble (10–23), each `0.65 · peak + 0.35 · mean` of its bands. That's honest: the analyser
    is mono, so there's no L / R. Blank while the analyser is off.
  - **The track number:** `Playback.queue.currentIndex + 1`.
  - **The readout,** rotated 90°: elapsed, −remaining, or the title. The title scrolls only while
    playing, visible and longer than the slot. Text is at least 10 px, in the bold mono font.
- **The lid:** a smoked tint (`Theme.mdPlayerLidTint`, ~65 % dark) over the bay's cartridge, plus a
  sheen and an edge.
  - The cartridge's `SpinningDisc` turns underneath. `MdCartridge` gets opt-in `discPlaying`,
    `discPaused` and `discShown` (default off, so the case's cartridge stays still).
  - Opening: the lid is hinged on the right. `Scale { origin.x: lidW; xScale: 1 − 0.86·lidOpen }`,
    with an inner rim appearing as it opens.

## Insert and eject (MdDeck)

`enum DeckPhase { Empty, PullingOut, FlyingIn, DroppingIn, ClosingLid, Loaded, OpeningLid,
LiftingOut, FlyingBack, ReturningCase }`. As in MdCaseStage, one `NumberAnimation` drives
`run()` / `stepDone()`, every step completes at once with animations off, and `finishAnimation()`
completes the running sequence. A sequence also finishes if `stacked` changes mid-flight.
**(Changed:)** steps overlap, so music starts about 1 s after Play:

1. **PullingOut** (300 ms): the case's cartridge slides out to 2.2× its peek, clear of the case.
2. **FlyingIn** (450 ms): `flyCart` takes over: the case area hides its cartridge
   (`cartridgeRemoved`) and the flying one goes from the case's exit rect to above the bay. **The
   lid opens at the same time.** Started from the back of the case, it turns label-up in flight
   (xScale).
3. **DroppingIn** (300 ms): into `bayRect()`. **On landing, playback starts**
   (`Playback.playFromModel(model, row)`), and the player shows its own cartridge.
4. **ClosingLid** (250 ms), then **Loaded**.

**Eject:**
1. `Playback.stop()` at once.
2. **OpeningLid** (300 ms).
3. **LiftingOut** (250 ms).
4. **FlyingBack** (450 ms) to above the empty case, with the lid closing at the same time.
5. **ReturningCase:** the cartridge goes into the case (`cartridge` 2.2 → 0), then the case goes back
   into its gap (V2's `putBack`).

An "external" disc (see below) lifts out and fades instead, since there's no case to return to.

**Empty case:** while its cartridge is in the player, the case stays out, without its cartridge,
and still flips.
- **Its buttons:** Flip, and **Eject (O)**.
- **A track on its back:** Enter or a double-click goes straight to `playFromModel`, with no insert
  animation (the disc is in already).
- **Esc** moves the keyboard to the pile and doesn't put the case back. Its disc is in the player,
  and only Eject brings it home.

**Taking cases out:** while the player holds a disc, the pile can be browsed but no case comes out.
Enter or a click on a case shows a hint under the player for 2 s: "Eject the disc first (O)". It's
a Timer, not an animation.

## Following Playback

The user's rule (2026-11-26): the MD Visualizer mimics the state of what the music library is doing.
The player always shows what's playing.

- **Loading a disc** records `loadedSource: "visualizer"`, the item, and `loadedTrackIds` (a JS set
  of the model's track ids).
- **On `currentTrackId` changes** (ignored mid-sequence):
  - **No track** (stopped, queue ended): nothing changes. The disc stays in, still.
  - **The id is one of the disc's:** it stays. A mix disc keeps its shell and label across albums.
  - **Otherwise** (Library, MPRIS, the queue): the player shows `Playback`'s album as an external
    disc at once, with no animation and the lid shut. Any case that's out goes back to the pile, also
    at once. That keeps the one rule: while a disc is in the player, another case only comes out
    after an Eject.
- **Play / pause with a disc in but nothing playing** (`!Playback.hasTrack`): the jog's middle and
  Space play the disc again. That's `playFromModel` for a visualizer disc, or
  `playItem("album", id)` for an external one.

## Keys (KeyRouter; the player is not a fourth pane)

The player's keys are for the mouse, with tooltips. The keyboard reaches everything through these:

| Action | Key | Where |
|---|---|---|
| `Eject` (new) | `O` (shift+o), and the Eject key | Visualizer, from any pane (the OPEN slide) |
| `PlayCase` (new) | Shift+Enter | case focused and full: insert, play from track 1 |
| `VolumeDown` / `VolumeUp` (new) | `-` / `=` and `+` | global (Main) |
| PlayPause / Next / Previous | Space, Ctrl+→ / ← | global, as now |
| FlipCase | `f` | full or empty case |
| Back | Esc | full case: back to the pile; empty case: the keyboard to the pile |

Buttons on a full case: **Play (⇧↵)**, Flip (f), Put back (Esc). On an empty one: Flip (f),
**Eject (O)**.

## Tests (`tests/qml/tst_mdplayer.qml`; offscreen, fakesink, Kid A/01.flac plays)

1. **Empty at start:** the lid shut, nothing running.
2. **Insert from the front:** Kid A, Shift+Enter. It's animating; pressing again finishes it, giving
   Loaded, `cartridgeRemoved`, the lid shut, and Playback playing `Kid A/01.flac`.
3. **Insert from the back:** OK Computer, flipped, Enter on row 2. `playTrackRequested` / insert
   with row 2 (only Kid A's file exists, so check what was asked for, not the sound).
4. **The keys:**
   - ❚❚ pauses and the jog's middle plays.
   - VOL ± step and clamp.
   - DISPLAY cycles 0 → 1 → 2 → 0.
   - ■ stops and the disc stays in.
   - The jog's ends go next / previous.
5. **Locked pile:** while loaded, the pile moves, but Enter takes nothing out and shows the hint.
   Eject (by the OPEN slide, and by `O`) stops playback, returns the cartridge and the case, and then
   a case comes out again.
6. **Started elsewhere:** `Playback.playItem("album", kidA)` shows Kid A at once, with nothing
   animating; a case that was out goes back.
7. **A mix disc** keeps its shell while the current track is one of its own.
8. **The LCD:** the meter is blank with the analyser off. With it on, the bass and treble bars follow
   `levels` (set by the test).
9. **Hidden:** with `Playback.windowVisible` false while playing, the disc, the scrolling title and
   the deck all stop.
10. **Layouts:** 1280 × 800 and 960 × 600, with and without the queue. Nothing overlaps.
11. **Screenshots:** `visualizer-player-loaded.png`, `visualizer-player-lcd.png` (the side LCD) and
    `visualizer-player-open.png` (the lid open).
12. **Keys:** `tst_keys.qml` gets `O`, Shift+Enter, `-`, `=` and `+`.

## Files

- **New:** `src/ui/qml/MdDeck.qml`, `MdPlayer.qml`, `MdSideLcd.qml`; `tests/qml/tst_mdplayer.qml`.
- **Changed:**
  - `src/ui/qml/VisualizerView.qml`, `MdCaseStage.qml`, `MdCartridge.qml`, `Main.qml` (volume
    keys);
  - `src/ui/keyrouter.{h,cpp}` (actions);
  - `src/ui/theme.h` (`mdPlayer*` / `mdLcd*` tokens);
  - `src/ui/CMakeLists.txt` (the QML files);
  - `tests/qml/tst_keys.qml`.
- The QML tests are found by `tst_qml` automatically: no new test target.

## Risks

- **Small sheared, rotated text on the software renderer:** bold, ≥ 10 px, gentle shear. Judge it
  from the LCD screenshot and on the real display.
- **Resizing or leaving mid-flight:** finish the sequence on `stacked` changes and when animations
  turn off.
- **`Playback.stop()` and `hasTrack`:** never unload on stop. Replaying uses the remembered model or
  album id.
- **Small windows:** minimum hit targets.
- **A playlist edited while its disc is in:** the snapshot of track ids keeps it as recorded, like a
  real MD.

## Changes from Neon's draft

- **Paths:** the key router and Theme are `src/ui/keyrouter.*` and `src/ui/theme.h`, QML is under
  `src/ui/qml/`, and the key table is tested in `tests/qml/tst_keys.qml`. There is no
  `tst_keyrouter`, no `src/input/`, no `qml/visualizer/`, and no new CMake test target.
- **Theme:** there's one palette (no light palette).
- **Insert timing:** steps overlap (the lid opens during the flight; playback starts on landing), so
  Play-to-music is about 1 s instead of about 1.9 s. Eject is about 1.4 s.
- **Blocked case:** taking a case out while a disc is in shows a 2-second hint instead of nothing
  happening.
- **`+` raises the volume too** (Neon had only `=`).
