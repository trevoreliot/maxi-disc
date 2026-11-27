# 0012: The MD Visualizer's pile: cases lying flat in the browse column, colours from the covers in memory

## Context

V1 opens main-menu tile 3 (Phase 6b). The user's drawing (`docs/ui-draft/md-visualizer.jpeg`,
photographed sideways) has the browse dropdown over three long bars, "minidisc 1 / 2 / 3", stacked
one above the other in the left column. The case comes out on the right, with the player under it.
Neon's draft planned an upright shelf of spines across the top of the screen instead. The plan is
`docs/plans/v1-visualizer-pile.md`.

## Evidence

- An MD case's spine is about 1 : 10. Lying flat in a 280–400 px column, that makes 28–34 px: the
  height of a list row, with the text reading normally. Upright, the text has to be turned sideways
  and squeezed, or the spines drawn fatter than real ones.
- The cover loader already keeps a 128 px JPEG of every cover on disk. Decoding it again and
  building a 24 × 24 hue histogram takes well under a millisecond. First-time extraction of embedded
  art (TagLib) is the expensive part, and the cover loader caches that already.
- `BrowsePane`'s `setMode()` commits its current row, and `moveSelection()` commits after an 80 ms
  pause. For a drill-down, either would open a pile without being asked.
- A handler for a child's `onModeChanged` runs before the parent's own bindings on that property
  have updated, so `hasListLevel` still read the old mode. The tests caught this.

## Decision

- **Layout (the user's choice):** a pile with a drill-down, in the left column under the browse
  dropdown.
  - Artists and Genres show their list first; l, Enter or a click opens that entry's pile, and h,
    Esc or the crumb go back.
  - Albums mode is the pile of every album.
  - Playlists mode is a pile of mix discs, one per playlist, in a blank-MD colour (by id) with a
    paper label (the user's choice).
- **Cases:** Rectangles and Text only, so the offscreen software renderer draws them.
  - The colour comes from the cover, under a gradient that makes the spine look rounded, between
    clear-plastic ends.
  - Each case sits a fixed ±2 px off, like a real pile.
  - The cursor's case slides 24 px right, towards the case area, with a shadow under it.
- **`BrowsePane`:** `autoSelect: false` (select only on commit, not after moves or mode changes)
  and `listsShown: false` (dropdown only). Both default to the old behaviour.
- **Colours:** the `SpineColors` singleton.
  - Requests are held by count, newest first, on 2 threads. A request nobody holds is skipped.
  - Colours are kept in memory, keyed by `CoverLoader::key`, so `bumpRevision` gives a new key.
  - No disk cache.
  - The value is clamped to [0.22, 0.86].
  - Ink is dark or light, picked by WCAG contrast.

## Revisit when

- A first visit to a large library feels slow to colour: a small disk cache of colours, keyed like
  the cover cache, would make later visits instant.
- The pile doesn't read as MD cases in the user test: the levers are the plastic ends, the jitter,
  the slide-out and the proportions (`MdPile.caseHeight`).
- Many covers come out muddy or alike: tune `dominantColor` (the saturation threshold, the
  value hump, the minimum chromatic weight).
- Playlists should look like their contents rather than a blank MD: colour a mix disc from its first
  track's cover.
