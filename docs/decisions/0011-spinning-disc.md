# 0011: The spinning MiniDisc: a speed-integrating FrameAnimation, covers cut round by the provider

## Context

Q4 puts a bare MiniDisc (the user's choice: no cartridge) next to the analyser in the transport bar.
It shows the playing album's cover and spins while playing. It should ease to a stop on pause, keep
its angle, resume from it, never reload the art between tracks of one album, and draw nothing while
idle. The plan is docs/plans/q3-q4-analyser-and-disc.md (Gemini 4 Neon's draft, corrected).

## Evidence

- ctest's offscreen platform uses Qt Quick's software renderer (`GraphicsInfo.Software`). It doesn't
  draw `ShaderEffect` / `MultiEffect`, so a disc clipped with `MultiEffect` would be blank in test
  screenshots. Images, rectangles and Shapes do render.
- A `RotationAnimator` runs on the render thread. Its value isn't visible on the GUI thread while it
  runs (tests can't read the angle), and stopping it writes the angle back late, so a following
  ease-out would start from a stale angle.
- Neon's replacement, a 380 ms OutCubic coast over 36°, starts at ≈ 284°/s, while the spin is
  ≈ 138°/s. The disc would lurch faster at the moment of pausing.
- `Playback.coverUrl` includes the track's path whenever the track has embedded art, so it changes
  on every track of such an album. Only its first segment, `<albumId>[.<revision>]`, follows the
  picture.

## Decision

- **Covers:** a second registration of the cover provider, `"coverround"`
  (`CoverImageProvider::Shape::Round`). It loads the normal square bucket (same cache), then
  centre-crops it and paints it into a circle with an antialiased `QPainter` ellipse and an image
  brush (`CoverLoader::round`, transparent corners). The disc is then plain `Image`s that rotate.
- **Comparing covers:** `Playback.discCoverUrl` / `Playback.coverKey` (`CoverLoader::roundUrl` /
  `key`). The disc loads only when the key changes.
- **Spin:** `angle += speed × 360/2.6 s × frameTime` in a `FrameAnimation` that runs only while
  `speed > 0` and the window shows. Only `speed` is animated, OutQuad from its current value: 0.4 s
  up on play, 0.8 s down on pause, 0.25 s on stop, and instant when spinning is switched off
  (`Playback.discSpinEnabled`, QSettings `transport/discSpin`) or the window hides. Decisions wait a
  `Qt.callLater` so `playing` / `paused` (one NOTIFY) have both settled.
- **Cross-fade:** two image slots. The new picture loads into the back one and fades in on top once
  it's `Ready` and still the wanted one, and the old one is cleared after. No art, or an error,
  fades both out to the plain disc.
- 56 px, the analyser box's height. `Main.minimumWidth` is 960 so the analyser keeps 160 px.

## Revisit when

- The GUI thread stalls long enough to make the disc stutter: then a render-thread animator (and a
  different way to test and stop it) may be worth its trouble.
- Tests move off the software renderer: a shader mask would then work too (the provider's cut is
  still cheaper).
- Albums with different embedded art per track matter: the key would have to include the art track.
- Cover art changing in place should update the disc mid-track: Playback would need to announce
  `CoverLoader::bumpRevision` (the transport's small cover has the same limit).
