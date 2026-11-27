# V1 MD Visualizer: the screen and the pile — design

Plan for PLAN.md's **V1 Visualizer screen and the pile** (Phase 6b). Gemini 4 Neon drafted it
(2026-11-26) for an upright shelf across the top of the screen. Claude then compared it with the
user's drawing and the code, and the user chose the pile instead. Changes from Neon's draft are
listed at the end. The PLAN.md "Done when" text is the contract. This file is the how. If the two
disagree, PLAN.md wins: fix this file.

User's choices (2026-11-26):
- **The pile, drill-down:** as drawn, MiniDisc cases lying flat in the left column, spines facing
  you. Choosing an artist turns the list into that artist's pile.
- **Mix discs:** in Playlists mode each playlist is a home-recorded MD.
- **What comes out of a case is the cartridge** (V2/V3), a clear shell.

## Facts checked before planning

- The drawing (`docs/ui-draft/md-visualizer.jpeg`, photographed sideways) has two columns:
  - Left: "Artist ▾" over three long bars "minidisc 1 / 2 / 3", stacked one above the other.
  - Right: the case ("Album", the cartridge's shutter showing at its top edge, "click to flip to
    back with track list"), with the player under it.
- `BrowsePane` is a `Pane`: a ComboBox, an "Import .m3u…" row (Playlists only), a rule, then a
  `StackLayout` of four ListViews. It owns its own four models.
  - `moveSelection()` starts an 80 ms `commitTimer`, so j/k preview the selection live.
  - A click calls `clickRow()`, which sets the index, emits `interacted()` and calls `commit()`.
- `Main.Screen` is `{ MenuScreen, LibraryScreen, ExportScreen }`, and `StackLayout.currentIndex` is
  `currentScreen`, so a new screen must be appended in the same order in both.
- `tst_navigation.qml`'s `test_menuTiles` asserts that tile 3 is skipped: it must change.
- The fixture library has 5 albums:
  - OK Computer: folder art.
  - Kid A: embedded art.
  - Abbey Road: no art.
  - The Wall: 2 discs.
  - Now 42: a compilation, genre Pop.
- Menu builders: `ContextMenus.albumEntries(albumId, name)` and
  `playlistEntries(playlistId, name, itemCount)`. Sending: `SendToDevice.sendItem(kind, id, name)`.
- `CoverLoader::load(id, size, cacheDir)` caches each bucket as a JPEG on disk. Decoding the cached
  128 px bucket again takes well under a millisecond.

## Screen wiring

- **`Main.qml`:**
  - `enum Screen { MenuScreen, LibraryScreen, ExportScreen, VisualizerScreen }`, and
    `VisualizerView { id: visualizerScreen }` last in the `StackLayout`.
  - The TopChrome title is "3  MD Visualizer".
  - `goBack()` asks `visualizerScreen.back()`, `handleAction` routes to it, and `ContextMenu`
    calls its `openContextMenu()`.
  - `FindArt` also works here: `visualizerScreen.currentAlbumId()`.
  - Alias: `readonly property alias visualizerView: visualizerScreen`.
- **`MainMenuView.qml`:** tile 3 is `interactive`, with subtitle "Pick a MiniDisc"; `enabledTiles: 3`.
  Main maps tile index 2 to `VisualizerScreen`.
- **`MenuIcon.qml` "visualizer":** a cartridge outline (the clipped corner, the shutter, a hub
  circle) in Shapes. No logo.
- **Search:** `/` behaves as everywhere: typing a search shows the Library's results.

## Layout

```
┌ 3  MD Visualizer ───────────────────────────────┐
│ Artist ▾          ┊   (case area: V2)           │
│ ‹ Radiohead       ┊                             │
│ ┌──────────────┐  ┊                             │
│ │ OK Computer  ▕  ┊                             │
│ ├──────────────┴──┐  ← cursor's case slid out   │
│ │ Kid A           ▕                             │
│ ├──────────────┬──┘                             │
│ │ Amnesiac     ▕  ┊   (player: V3)              │
│ └──────────────┘  ┊                             │
└─────────────────────────────────────────────────┘
```

- **Left column:** 32 % of the screen's width, clamped to 280–400 px. With the queue open at the
  960 px minimum, the screen is 640 px and the column 280 px.
- **Right side:**
  - The case area above the player, as drawn, when there's room for both at ≥ 220 px tall.
  - Otherwise side by side (Neon's point: the content at 960 × 600 is only ~460 px tall).
  - V1 fills both slots with placeholders: a dashed outline, a hint ("Pick a MiniDisc", "MiniDisc
    player"), and the chosen case's title once Enter is pressed (V2's hook).

## Left column: the list level and the pile level

`VisualizerView` holds a `ColumnLayout` of:
- `BrowsePane`;
- a crumb row ("‹ Radiohead" / "‹ All artists"), shown at the pile level in Artists / Genres mode;
- `MdPile`, at the pile level.

The screen has two levels:
- **The list level** (Artists / Genres only): BrowsePane's list as today.
- **The pile level:** the chosen entry's albums. Albums mode and Playlists mode have no list level:
  the pile is all albums, or all playlists as mix discs.

`BrowsePane` gets two properties. Their defaults change nothing for Library or Export:
- `livePreview: true`. When false, `moveSelection()` doesn't start `commitTimer`, so `selected` only
  fires on `commit()` (Enter, l or a click). The Visualizer drills in on `selected`; j/k alone don't.
- `listsShown: true`. When false, the list `StackLayout` and the import row are hidden. The pane then
  takes only the ComboBox's height, so the pile fills the rest of the column.

Mode changes from the dropdown go to the list level in Artists / Genres, or straight to the pile in
Albums / Playlists. The pile keeps its cursor while the screen is hidden, as all screens do, and when
you come back out of it and choose the same entry again.

## The pile: `MdPile.qml` + `MdCase.qml` (delegate)

- **The list:** a vertical `ListView`, first item at the top.
  - `cacheBuffer` is about two screens, and delegates are created and destroyed (no `reuseItems`).
    Each one is a few Rectangles and two Texts.
  - `highlightRangeMode: ApplyRange` with a margin of about two cases, so the cursor never touches
    an edge. `positionViewAtIndex(i, ListView.Contain)` after key moves.
  - Models: `AlbumListModel` (artistId / genreId; both 0 means every album), or
    `PlaylistListModel` for mix discs. Tests can also pass a plain `ListModel` with the same roles.
- **Proportions:** an MD case's spine is about 1 : 10. Case height is `clamp(round(width / 10), 26,
  34)`, so 28–34 px: a list row, with the text reading normally.
- **An album case** (Rectangles, Text and Shapes only, affine transforms only):
  - **Body:** the cover's colour, radius 2. A vertical gradient (1 px lighter top edge, a darker
    bottom edge) makes the spine look rounded.
  - **Clear-plastic ends:** ~6 px at each end in a translucent, desaturated tint, with a hairline
    hinge, the way the plastic shows past the insert.
  - **Separators:** 1 px dark lines between cases.
  - **Text:** the title on the left, bold and elided. The artist sits on the right, muted, only when
    the pile mixes artists (All, a genre, Albums mode).
  - **Ink:** `Theme.spineInkDark` or `spineInkLight`, chosen by `SpineColors.prefersDarkText(color)`.
  - **A real pile:** each case is offset by a fixed ±2 px in x, derived from its id.
  - **No art:** `Theme.spineDefault`.
- **A mix disc:**
  - The case is a blank-MD colour, `Theme.mdBlankColors[id % n]`: translucent blue, red, green,
    smoke, clear.
  - A white paper label covers ~70 % of the spine, with the playlist's name in dark ink on it.
  - The track count is on the plastic at the right.
- **The cursor's case:**
  - Slides about 10 % of its width to the right, towards the case area, with a soft shadow under it
    (a translucent Rectangle). This previews V2's take-out.
  - When the pile has the keyboard, the case gets an accent outline, and `FocusBar` marks the pane.
  - 140 ms OutCubic, instant while the window is hidden. Nothing runs once settled.
- **Colour arrival:** a 150 ms `ColorAnimation`, instant while hidden.

## Spine colours: `SpineColors` (C++ QML singleton, `src/ui/spinecolors.{h,cpp}`)

```cpp
class SpineColors : public QObject {
    Q_OBJECT QML_ELEMENT QML_SINGLETON
public:
    SpineColors(QString coverCacheDir, QObject *parent); // before the engine (main.cpp, tst_qml Setup)
    static SpineColors *create(QQmlEngine *, QJSEngine *);
    Q_INVOKABLE QString key(const QString &coverUrl) const;  // CoverLoader::key
    Q_INVOKABLE QColor color(const QString &coverUrl) const; // invalid until known (or no art)
    Q_INVOKABLE void request(const QString &coverUrl);       // async → colorReady(key)
    Q_INVOKABLE void release(const QString &coverUrl);       // the delegate went away
    Q_INVOKABLE static bool prefersDarkText(const QColor &background); // WCAG luminance
    static QColor dominantColor(const QImage &cover);        // pure; tested
Q_SIGNALS:
    void colorReady(const QString &key);
};
```

- **Threads:** its own `QThreadPool` with 2 threads.
  - Pending keys sit in a stack, newest on top. A request whose key has no holders left (every
    `release`d) is skipped. Scrolling fast through 1,000 cases therefore only computes what's still
    on screen, and the visible cases get their colours first.
  - A worker runs `CoverLoader::load(<id part>, 128 px, cacheDir)`, then `dominantColor`, and posts
    the result back with a queued call: store it, then `colorReady(key)`.
  - The destructor empties the stack, sets a stop flag and waits for the pool.
  - A test seam holds the workers, so skipping can be tested deterministically.
- **Cache:** memory only, a `QHash<QString, QRgb>` keyed by `CoverLoader::key` (`<albumId>[.<rev>]`).
  A null image is stored as "none", so it isn't retried. No disk file: the cover's 128 px thumbnail
  is already cached on disk, and computing the colour from it takes about a millisecond.
  `bumpRevision` changes the key, so changed art is picked up without extra invalidation code.
- **Algorithm:**
  1. Scale to 24 × 24.
  2. Build a 36-bin hue histogram. Each pixel's weight is s² · (0.3 + 0.7 · sin(π · v)). Near-black,
     near-white and grey pixels (s < 0.18) are skipped.
  3. Require a total chromatic weight of at least 12, so a few coloured pixels can't colour a black
     and white cover.
  4. Smooth the bins with their neighbours (weight 0.25), pick the best bin, and take the weighted
     mean RGB of it and its neighbours.
  5. Otherwise, use the average of all pixels.
  6. Clamp HSV value to [0.22, 0.86], so black covers don't vanish into the dark theme.
- **Delegates:**
  - `request(coverUrl)` in `Component.onCompleted`, and `release` in `onDestruction`.
  - The case colour is `color(coverUrl)`, set again on `colorReady` when the key matches.

## Keys and mouse (KeyRouter actions only)

| Level | Action | Does |
|---|---|---|
| list | j / k | move the cursor; no live preview |
| list | l / Enter / click a row | into that entry's pile |
| list | h | nothing (left edge) |
| list | Esc | `back()` is false, so Main goes to the menu |
| list | `,` / right-click, `i` | the row's menu, send the row (BrowsePane's own) |
| pile | j / k | move along the pile (the cursor's case slides out) |
| pile | h / Esc / click the crumb | back to the list, cursor on the entry (Artists / Genres) |
| pile | h | nothing in Albums / Playlists mode; Esc there goes to the menu |
| pile | l | returns false, so the queue gets focus if it's open |
| pile | Enter / click the cursor's case / double-click | `caseChosen` (V1: the placeholder shows it; V2: take it out) |
| pile | click a case | cursor to it, pile focused |
| pile | `,` / right-click | `albumEntries` / `playlistEntries` for that case |
| pile | `i`, `a` | send that case; find art (albums) |
| pile | wheel | scrolls |

## Hooks for V2 / V3

- `MdPile.caseChosen(var item)`, where `item` is `{ kind: "album" | "playlist", id, title, artist,
  year, coverUrl, color }`. One shape for albums and mix discs, so V2/V3 handle both the same way.
- `MdPile.caseRect(index)`: the case's rectangle mapped to `VisualizerView`, where V2's slide-out
  starts.
- `MdPile.takenKey`: a case that's out leaves a gap in the pile.
- `VisualizerView.chosen`: drives the V1 placeholder; V2 replaces the placeholder.

## Files

- **New:**
  - `src/ui/spinecolors.{h,cpp}`;
  - `src/ui/qml/VisualizerView.qml`, `MdPile.qml`, `MdCase.qml`;
  - `tests/ui/tst_spinecolors.cpp`;
  - `tests/qml/tst_visualizer.qml`.
- **Changed:**
  - `Main.qml`, `MainMenuView.qml`, `MenuIcon.qml`;
  - `BrowsePane.qml` (`livePreview`, `listsShown`);
  - `theme.h` (`spineDefault`, `spineInkDark`, `spineInkLight`, `spinePlastic`, `spineShadow`,
    `mdBlankColors`, `mdLabelPaper`, `mdLabelInk`);
  - `src/ui/CMakeLists.txt`, `tests/CMakeLists.txt`;
  - `src/app/main.cpp` and `tests/qml/tst_qml.cpp` (construct `SpineColors` with the cover cache dir);
  - `tst_navigation.qml` (tile 3 is reachable);
  - CLAUDE.md layout bullet.

## Tests

- **C++ `tst_spinecolors`:**
  - **`dominantColor`:**
    - a solid colour;
    - a 30 % orange patch on a 70 % neutral cover gives orange;
    - greyscale gives the clamped average;
    - a few coloured specks on black and white don't win;
    - a null image gives invalid;
    - the fixture's OK Computer (folder) and Kid A (embedded) covers give valid colours that
      differ.
  - **`prefersDarkText`:** white, black, mid-grey, yellow and blue.
  - **Async:**
    - two requests for one key give one `colorReady`, then `color()` answers immediately;
    - a request released while the workers are held is never computed;
    - after `bumpRevision`, the new key is computed again.
- **QML `tst_visualizer`:**
  - **Menu:** tile 3 opens by keys (l l Enter) and by click; Esc goes back.
  - **Artists:**
    - Radiohead → a pile of OK Computer and Kid A, without artist text;
    - h goes back with the cursor on Radiohead;
    - "All artists" → 5 cases, with artist text.
  - **Genres:** Pop → Now 42.
  - **Albums:** the pile appears directly; h does nothing; Esc goes to the menu.
  - **Playlists:** a playlist made by the test (deleted in cleanup) appears as a mix disc showing
    its name and count.
  - **Cursor:** the cursor's case is offset; j stops at the bottom.
  - **Colours:** OK Computer and Kid A get non-default colours; Abbey Road keeps the default.
  - **Mouse:** a click on a row drills in; a click on a case moves the cursor; a click on the
    cursor's case sets `chosen`; right-click and `,` give the album's entries.
  - **1,000 cases** (a `ListModel`): holding j to the end and back keeps the delegate count bounded
    (< 80) and the cursor's case in view.
- **Screenshots (`MMM_SCREENSHOT_DIR`):** the menu with the new icon; the pile at 1280 × 800 and at
  960 × 600 with the queue open; the mix discs.

## Risks

- **The pile reading as coloured list rows instead of MD cases.** The user test judges it. The
  levers are the plastic ends, the jitter, the slide-out with its shadow, and the proportions.
- **Many dark covers:** the value clamp and `spineDefault` must still stand out from `Theme.surface`;
  check against the real library.
- **First visit to a big library:** covers never decoded before arrive over seconds. Newest-first
  keeps what's on screen first.
- **BrowsePane is shared:** the new properties default to today's behaviour, and the existing
  Library / Export tests must stay green.

## Changes from Neon's draft

- **Layout:** the upright shelf across the top, with its spines' text turned sideways, became the
  drawn pile with a drill-down (the user's choice). The shear and depth-face geometry, the
  shelf/stage split and the wheel → horizontal mapping went with it.
- **Playlists:** mix discs (the user's choice), so there's no `albumsForPlaylist` query and no
  `AlbumListModel.playlistId`. Neon had put that query in a nonexistent `src/core/library.h`; it
  would have belonged in core `LibraryQuery`.
- **Colours:**
  - No JSON disk cache and no `sourceFingerprint`: recomputing from the cached thumbnail is cheap.
  - `release` replaces `cancel`, so several delegates can share a key.
  - Text colour comes from Theme tokens, not hex literals in C++.
- **Ids:** `int` in QML, per the project rule; Neon had `qint64` / `real`.
- **Keys:** no dead keys (Neon had j/k do nothing on the shelf). Esc follows the Library: back a
  level, then the menu.
- **Albums mode:** the pile is all albums with no list level, so `rowOfAlbumId` isn't needed.
- **Kept from Neon:** the colour algorithm (value clamp raised to 0.22), newest-first work, the case
  and player side by side at 960 × 600, the V2 hooks (`caseChosen`, `caseRect`, a gap for the case
  that's out), and screenshots at the minimum size.
