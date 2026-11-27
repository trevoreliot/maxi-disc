# V2 MD Visualizer: taking a case from the pile — design

Plan for PLAN.md's **V2 Taking a case from the pile** (Phase 6b). Gemini 4 Neon drafted it
(2026-11-26). Claude then checked it against the code, and changes from Neon's draft are listed
at the end. The PLAN.md "Done when" text is the contract. This file is the how. If the two
disagree, PLAN.md wins: fix this file.

## Decisions

- **Flat 2D geometry only.** No perspective `Rotation`. The software renderer then draws in test
  screenshots exactly what the GPU draws.
  - **Taking out:** a box seen straight on as it rotates 90° about its horizontal axis. The spine
    face's height is spine · cos θ and the front's is front · sin θ, stacked edge to edge. The front
    rises from the spine's top edge, because the case opens at the top where the cartridge comes out.
  - **Flip:** the case squashes horizontally to nothing about its vertical centre, swaps front and
    back, and opens out again. A shading scrim and a thickness strip on the turning edge keep it from
    looking paper-thin.
  - **Fixed-size faces:** each face is a fixed-size item scaled with `transform: Scale`, so nothing
    is re-laid-out, re-wrapped or re-decoded while it moves.
- **Panes:** list / pile | case. Taking a case out moves the keyboard to it.
  - **From the case:** h goes back to the left column, and the case stays out. l returns false
    (queue).
  - **From the pile:** l goes to the case that's out; Enter on the cursor's case focuses it if it's
    the one out.
  - **Swapping:** choosing another case puts the first back (animated), then takes the new one out.
  - **Left the pile meanwhile:** the case returns to the pile column's left edge and fades out.
- **Keys:**
  - **New action:** `KeyRouter::FlipCase` on `f` (free; `tst_keys` updated).
  - **On the front:** Enter flips, as the drawing has it ("click to flip to back with track list").
    That gives a drill-down: Enter takes the case out, Enter flips it, j/k pick a track, Enter plays.
  - **On the back:** Enter plays from the cursor's track: `playTrackRequested(model, row)` →
    `Playback.playFromModel`. V3 puts the insert animation in front of it. This changes PLAN.md's
    "once V3 exists"; the key is never dead.
  - **Pile and case:** `,` opens the album's or playlist's menu, `i` sends the case, `a` finds art
    for the open album.
  - **Esc:** puts the case back and returns the keyboard to the pile. With no case out, it works as
    in V1.
- **Pressing again during an animation:**
  - **The same action** (Enter while it comes out, `f` during a flip, Esc while it goes back, or a
    click on the moving case) jumps to the end and is used up.
  - **A different action** (`f` or Esc while it comes out, j/k during a flip to the back, pile keys
    while it goes back) jumps to the end and is then carried out.
- **Mouse:**
  - **Left-click:** the front flips, and Flip / Put back buttons sit beside the case's lower right
    corner. On the back, a click on a track selects it and a double-click plays it.
  - **Right-click** anywhere on the case: the album or playlist menu.

## Proportions **(changed)**

- **The cartridge** is 68 × 72 mm, taller than wide, and the case around it is about 74 × 78 mm:
  `caseW = round(caseH * 0.95)`.
- **The cartridge peeks** `round(caseH * 0.34)` out of the top edge.
- **Size:** `caseH = clamp(floor(availH / 1.36), floor(availW / 0.95))`, limited to 150–340 px, so
  a big window gets a big case.
- **Buttons:** Flip and Put back sit beside the case.

## Components

```
VisualizerView.qml
├── left column: BrowsePane + crumb + MdPile (MdCase → MdCaseSpine)
├── stage GridLayout
│   ├── MdCaseStage.qml   (the case slot: state machine, TrackListModel, buttons, V1's placeholder when Idle)
│   └── Placeholder       (the player: V3)
└── overlay (anchors.fill, z above both, accepts no input itself)
    └── MdCaseActor.qml   (the one case that moves: spine face + flip container)
        ├── MdCaseSpine.qml   (shared with MdCase, so the hand-off is pixel-exact)
        └── flip container
            ├── cartridge clip → MdCartridge.qml (z below the case; slides out of the top)
            ├── MdCaseFront.qml
            └── MdCaseBack.qml
```

- **`MdCaseSpine.qml`:** MdCase's case body, extracted so the pile and the actor draw the same
  thing.
- **`MdCartridge.qml` (changed):**
  - A clear shell (`Theme.mdShell*`; a mix disc's shell is tinted with its blank colour) with a
    chamfered corner, a metal shutter (`Theme.mdShutter*`) and a label (the cover; a mix disc's is
    paper with the name in italics).
  - **The disc inside is Q4's `SpinningDisc`,** static (`playing: false`), with
    `interactive: false`. That's new on SpinningDisc: no tap handlers and no tooltip. It shows the
    cover's round picture (`image://coverround/…` and `SpineColors.key()`), so it's the same disc
    the transport bar shows, and V3 makes it spin.
  - On the back the shutter is on the other side and there's no label.
  - Properties: `caseKind`, `caseId`, `title`, `coverUrl`, `accentColor`, `showingBack`.
- **`MdCaseFront.qml`:**
  - The clear sleeve, with a thumb notch and a sheen.
  - **Album:** the cover full bleed, with a scrim holding the title, artist and year. Without art,
    the case colour and a ♪.
  - **Mix disc:** a ruled paper card with a header bar in its blank colour, the name in italics and
    "N tracks".
- **`MdCaseBack.qml`:**
  - The sleeve, with the header (title, total time).
  - A `ListView` of 24 px rows: number, title (elided), duration. Disc sections appear when
    `discCount > 1`.
  - Playlists number their rows 1…n, on paper.
  - The `ListView` has no model until the case has landed (`showRows`), so no rows are built during
    an animation.
  - "Loading…" / "No tracks" when there's nothing to show.
- **`MdCaseStage.qml`:**
  - **Phases:** `enum Phase { Idle, TakingOut, CartridgeOut, Front, FlippingToBack, Back,
    FlippingToFront, PuttingBack }`.
  - **Properties:** `currentItem` (V1's item shape), `phase`, `isOpen`, `animating`, `showingBack`,
    `paneActive`, `trackIndex`, `trackModel`, `caseTargetRect`, `cartridgeTargetRect` (V3),
    `cartridgeHidden` (V3), `animationsEnabled`.
  - **Methods:** `takeCase(item, originRect, pileKey, pileIndex)`, `putBack(destRect, fadeOut)`,
    `flip()`, `finishAnimation()`, `moveTrack(delta)`, `activateCurrent()`.
  - **Signals:** `playTrackRequested(model, row)`, `returnedToPile(key)`,
    `contextMenuRequested(...)`.

## Animation

All durations become 0 when `animationsEnabled` is false, and a running animation jumps to its end.
**(Changed:)** `animationsEnabled` = the screen is visible and `Playback.windowVisible`. It doesn't
depend on whether the screen has the keyboard, because focusing the queue mustn't snap animations.
Nothing runs while the case is settled (Idle / Front / Back).

1. **TakingOut** (480 ms, OutCubic, `takeProgress` 0 → 1).
   - The origin (`mdPile.caseRect(index, overlay)`) and the target are taken once, in the same JS
     turn that sets the pile's `takenKey`.
   - Width and centre interpolate between them, with a small lift (−sin(πp)·18 px).
   - The spine face goes from the pile case's height to 13 % of the case height, times cos θ; the
     front face is case height · sin θ.
   - A shading scrim on each face fades with its angle.
2. **CartridgeOut** (420 ms, OutCubic): the cartridge rises out of the top slit by its peek
   distance. It's clipped so it never shows below the case.
3. **FlippingToBack / ToFront** (460 ms, InOutCubic): `xScale = |cos φ|` about the centre; the faces
   swap at half-way. The cartridge flips with the case.
4. **PuttingBack** (500 ms): the cartridge tucks in (150 ms), then the case folds back to its spine
   and returns to its gap (350 ms). If its pile isn't showing any more, it goes to the column's
   left edge and fades. On arrival `takenKey` clears, and a pending case starts coming out.

**Settled geometry (changed):** a Front / Back case binds to `caseTargetRect`, computed from the
stage's and the slot's x / y / width / height, so window resizes move it. A `mapToItem()` inside a
binding wouldn't notice an ancestor moving.

## Track list

- **When it loads:** `takeCase` sets `trackModel.albumId` or `playlistId` at once, so the query runs
  on the library's thread pool while the case flies.
- **Stale results:** `LibraryListModel` already drops them (generation counter).
- **After it goes back:** the model is cleared in Idle by setting both ids to 0 **(changed: not
  −1)**.
- **The cursor:** `trackIndex` clamps when the count changes.
- **Covers:** `CoverArt` requests a fixed size (the resting size), so moving never re-decodes.

## Theme tokens

- `mdCasePlastic`, `mdCaseBorder`, `mdCaseSheen`, `mdCaseShadow`, `mdInsertScrim`, `mdBackInsertBg`;
- `mdShellFill`, `mdShellBorder`, `mdShellRib`;
- `mdShutterLight`, `mdShutterMid`, `mdShutterDark`, `mdShutterSlot`;
- `mdLabelRule`, `mdPaperHighlight`.

## Files

- **C++:** `src/ui/keyrouter.{h,cpp}` (`FlipCase`) and `src/ui/theme.h` (tokens) **(changed:
  Neon had `src/app/`)**.
- **QML, new:** `MdCaseSpine`, `MdCartridge`, `MdCaseFront`, `MdCaseBack`, `MdCaseActor`,
  `MdCaseStage`.
- **QML, changed:** `MdCase` (uses `MdCaseSpine`), `SpinningDisc` (`interactive`),
  `VisualizerView` (stage, overlay, keys, `playFromModel`), `src/ui/CMakeLists.txt`.
- **Tests:** `tst_keys.qml`, `tst_visualizer.qml`, `tst_disc.qml` (the `interactive: false` disc).

## Tests (offscreen)

1. **Life cycle:** OK Computer goes through TakingOut → CartridgeOut → Front; `f` → Back with 3
   rows (number, title, duration) and the j/k bounds; `f` → Front; Esc → Idle with `takenKey` "".
2. **Finishing at once:**
   - Enter during TakingOut jumps to Front and doesn't flip.
   - `f` during a flip jumps to Back.
   - Esc while going back jumps to Idle and stays in the pile.
   - `f` during TakingOut jumps to Front, then flips.
3. **Panes and swapping:** h and l between the pile and the case; choosing Kid A while OK Computer
   is out puts OK Computer back, then takes Kid A out.
4. **Enter:** on the front it flips; on the back it plays: `playTrackRequested(model, 0)`, and with
   Kid A, Playback plays `Kid A/01.flac`.
5. **Special cases:**
   - The Wall shows two disc sections.
   - Abbey Road (no art) is drawn plainly.
   - A playlist's mix disc has paper on the front, the cartridge and the back.
6. **Right-click:** album entries for an album, playlist entries for a mix disc.
7. **Pile changed while a case is out:** h, another artist's pile, then Esc puts the case back
   cleanly.
8. **Hidden:** with `Playback.windowVisible` false, taking out and flipping finish at once.
9. **Screenshots:** `visualizer-case-front.png`, `visualizer-case-back.png` and
   `visualizer-case-mix-back.png` at the default size and at 960 × 600.

## Risks

- **A 1–2 px jump at the hand-off between the pile and the actor:** the shared `MdCaseSpine`, with
  the origin rect taken in the same turn as `takenKey`.
- **Hitches from building rows or decoding covers mid-animation:** `showRows` and fixed
  `sourceSize`. Only `Scale` transforms animate.
- **A crowded layout at 960 × 600 (side by side):** the size formula, checked with a screenshot.
- **Leaving the screen mid-animation:** `animationsEnabled` turns false, and the animation jumps to
  its end.

## Changes from Neon's draft

- **Paths:** the key router and Theme live in `src/ui/` (lower-case files), not `src/app/`.
- **`animationsEnabled`:** no longer tied to the screen having the keyboard (focusing the queue
  would have snapped animations); only to visibility.
- **Case proportions:** the case is taller than wide (0.95 : 1, like the 68 × 72 mm cartridge it
  holds), not 1.05 : 1. The size cap went from 240 to 340 px.
- **The disc in the cartridge:** Q4's `SpinningDisc`, static and non-interactive, not a new
  drawing. It's one disc in the transport bar, the cartridge and (V3) the player, with the cover
  printed on it as the user chose for Q4.
- **Clicking the cartridge** doesn't play in V2. Playing from the case is V3's gesture: Play, with
  the insert animation.
- **Clearing the track model:** ids set to 0 (none), not −1.
- **Settled position:** bound to the stage's geometry, not to `mapToItem()` in a binding.
