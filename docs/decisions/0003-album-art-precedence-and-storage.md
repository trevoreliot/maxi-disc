# 0003 — Album art: folder file beats embedded; the finder saves both, at ≤ 1200 px

- **Status:** accepted and implemented (2026-11-21, M7): `src/core/artworksaver.cpp`,
  `src/core/artworkfinder.cpp`, `src/ui/coverimageprovider.cpp`; tests `tst_artworksaver`,
  `tst_artworkfinder`, `tst_uimodels` artFinderSavesCover
- **Decided by:** Claude Code during M7 (planned with Gemini 4 Neon); approved by the user as
  part of the M7 plan. The user can revisit
- **Revisit if:** a player/DAP shows the wrong cover, covers look soft on a big screen, or a
  library keeps several albums in one folder

## Context

The PLAN says covers are stored "both: cover.jpg in album folder and embedded in files".
M3 left open which one wins when an album has both and they differ (M3 follow-up), and M7
has to decide what to save, where, and at what size. Constraints: `src/core` has no Qt Gui
(no `QImage`, so no resizing there); folders sometimes hold tracks of several albums
("Singles/", a flat "Downloads/"); multi-disc albums are often `Album/CD1`, `Album/CD2`.

## Decision

1. **Precedence: folder art beats embedded art** everywhere we show art (album grid, album
   page, transport bar, MPRIS). It's what the scanner already recorded in `albums.cover_path`,
   it's one file per album (cheap to load), and the user can replace it without retagging.
   Embedded art is the fallback, from the album's first track that has some.
2. **The finder saves both.** `cover.jpg` (or `cover.png` for PNG data) goes into **every
   folder that holds only this album's tracks** (so each disc folder of a multi-disc album
   gets one: the scanner links folder art only to tracks directly in that folder). A folder
   shared with another album gets **no** file, since it would become that album's art too;
   the user sees a warning. The picture is embedded as the front cover of **every** track
   (other picture types are kept, except in MP4, which can't tell them apart), via the
   normal TagWriter copy-and-swap (docs/decisions/0002). The album's `cover_path` is then set
   explicitly to the first file written, so it wins over older folder art.
3. **Size: the Cover Art Archive's 1200 px thumbnail** (always JPEG), falling back to 500 px,
   then the original. No resizing anywhere, so core stays Gui-free and there's no
   re-encoding loss. 1200 px JPEGs are ~100–500 KB: small enough to embed in every track,
   big enough for any screen we have. (USB sync shrinks to 500 px for DAPs anyway: PLAN.)
4. **Existing files are not deleted.** A `folder.jpg` or `cover.png` from before stays; an
   existing `cover.jpg` is replaced (atomically, `QSaveFile`).

## Costs

- Every track grows by the cover's size (~0.3 MB × tracks) and is rewritten once (a full
  copy per file without reflinks).
- In a shared folder the album only gets embedded art; a later scan may still link the
  folder's own image (if any) to it, since `setFolderCover` fills albums without folder art.
- If other software prefers `folder.jpg` over `cover.jpg`, an old `folder.jpg` left next
  to the new `cover.jpg` can win there.

## How to revisit

- **Embedded beats folder:** swap the order in `CoverLoader::load()` (`coverimageprovider.cpp`),
  `TrackRow::artSourcePath()`, `ArtCache::fileFor()` and the album query's `artTrackPath` use.
- **Folder file only / embedded only:** drop the `editTracks` call or the folder loop in
  `ArtworkSaver::save()`; `tst_artworksaver` documents the current behaviour.
- **Bigger art:** use `CaaImage::image` in `saveUrl()` and add a size cap; resizing would
  need Qt Gui, so do it in `src/ui` (e.g. in `ArtFinder::save`) before calling
  `Library::saveAlbumArt()`.
