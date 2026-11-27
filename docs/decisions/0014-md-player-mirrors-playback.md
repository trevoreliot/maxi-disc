# 0014: The MD player mirrors Playback; the deck owns insert / eject

(Number note: the `cd-rip` branch also starts its records at 0014. Whichever branch merges second
renumbers its records.)

## Context

V3 adds the portable MiniDisc player under the case area. A disc goes in from the case that's out
(Play) and comes back with Eject. Music also starts from everywhere else: the Library, MPRIS, the
queue and the transport bar. The user's rule (2026-11-26): the MD Visualizer mimics the state of
what the music library is doing. The plan is `docs/plans/v3-visualizer-player.md`.

## Evidence

- `Playback.currentAlbumId` can't identify a mix disc: its tracks come from many albums.
- `Playback.stop()` keeps the queue and its current track (`hasTrack` stays true), so play / pause
  afterwards resumes the same disc. Only a cleared queue needs the disc replayed.
- The case area sits near the top of the screen. A cartridge pulled "fully clear" of the case left
  the window under the header (seen in the screenshot of the flight).

## Decision

- **`MdDeck`** holds the case area, the player and the cartridge that travels between them. One
  `NumberAnimation` steps the phases, as in `MdCaseStage`: Empty → PullingOut → FlyingIn (the lid
  opens along) → DroppingIn → ClosingLid (playback starts on landing) → Loaded; and OpeningLid →
  LiftingOut → FlyingBack (the lid shuts along) → ReturningCase → Empty.
- **Leaving the case:** the cartridge comes out only as far as there's room (`pullOut`). The
  travelling one is drawn behind the case and in front of the player, and in the stacked layout it
  swings out to the right, so the hand-over can't be seen and it never leaves the window.
- **Following Playback:** a disc stays in while the playing track is one of its own. For a
  visualizer disc, that means one of its case's tracks (which works for mix discs); for an external
  disc, the same album. Anything else playing shows that album's disc at once, with no animation,
  and a case that's out goes back to the pile, so only one disc is ever "out". Stopping or clearing
  the queue leaves the disc in; only Eject takes it out.
- **Locked pile:** while a disc is in, the pile can be browsed but no case comes out ("Eject the
  disc first (O)").
- **Keys:** `O` ejects (the OPEN slide), Shift+Enter plays the case that's out, and `-` `=` `+`
  change the volume anywhere.

## Revisit when

- The user wants to pick another album without ejecting: let Play on a new case eject the old disc
  first (animated) instead of the hint.
- Mix discs should keep their identity after a playlist edit: snapshot the track ids at insert
  instead of reading the case's live track list.
- Space should replay the disc after the queue is cleared, as the jog lever does: Main would ask
  the visualizer first.
