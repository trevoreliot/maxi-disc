# 0013: The MD case turns in flat 2D (an orthographic box), not perspective 3D

## Context

V2 takes a case out of the pile: it turns from its spine to its front, its cartridge slides part
way out, and it flips to its track list and back. Phase 6b said 2.5D (Flipable, perspective
`Rotation`) unless shown otherwise. The plan is `docs/plans/v2-visualizer-case.md` (Neon's draft,
corrected).

## Evidence

- ctest's offscreen platform uses Qt Quick's software renderer. It draws perspective (3D) transforms
  flat, so a perspective `Rotation` would look right only on the GPU, and test screenshots of a
  turn wouldn't show what the user sees. Affine transforms (`Scale`), Rectangles, Images, Text and
  Shapes render the same on both.
- A box seen straight on as it turns about one axis is affine. The face turning away is the face
  scaled by cos θ along that axis, and the face turning in is scaled by sin θ. A flip about the
  vertical axis is `xScale = |cos φ|`, swapping faces half-way.
- Animating `width` / `height` relays out text and re-requests images every frame. Scaling a
  fixed-size face doesn't.

## Decision

- **Taking out:** the spine face (the pile's own `MdCaseSpine`) folds under the front face. It goes
  from the pile's case height to 13 % of the case height, times cos θ, while the front is case
  height · sin θ, stacked edge to edge. The whole case moves from the pile's case rectangle to the
  case area, with a small lift.
- **Flip:** the case, its cartridge included, scales to its edge about its vertical centre and opens
  out on the other face. A dark strip at the turning edge gives it thickness.
- **Shading:** a shade on each face deepens as it turns away from the viewer.
- **Animation:** only `take`, `cartridge` and `flipProgress` animate, one step at a time, through
  one `NumberAnimation`. Nothing runs while the case is settled. With animations off (screen or
  window hidden) every step completes at once.
- **Pressing again:** the same key during an animation finishes it; another key finishes it and
  then goes ahead.

## Revisit when

- The turn looks too flat on the user's display: a perspective `Rotation` can be added on top for the
  GPU only, with the tests still checking the orthographic geometry.
- Tests move off the software renderer.
