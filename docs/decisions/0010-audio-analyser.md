# 0010: The audio analyser: `spectrum` in playbin3's audio-filter, held until heard

## Context

Q3 replaces the transport bar's placeholder with a real spectrum analyser: segmented LED columns
(the user's choice), in time with the music, cheap enough to leave on, and off when nobody can
see it. The player runs playbin3 on its own engine thread (0001); its audio-filter already holds
`rgvolume ! rglimiter` while ReplayGain is on, and GStreamer only lets the filter change at READY.
The plan is docs/plans/q3-q4-analyser-and-disc.md (Gemini 4 Neon's draft, corrected).

## Evidence

- `spectrum` (gst-plugins-good) posts one element message per interval with `running-time`,
  `duration` and per-bin `magnitude` (dB). It keeps a ring buffer of the last FFT's samples, so it
  posts every 33 ms at any bin count: measured 300 messages per 10 s for 512, 1024 and 2048 bins.
  1024 bins = 21.5 Hz apart at 44.1 kHz.
- Its packed 24-bit path (S24LE) reads ~44 dB too loud: a 24-bit FLAC measured +23 dB in the bass
  as S24LE, −20 dB as S16 / S32 / F32. 24-bit FLAC decodes to S24_32, which `spectrum` doesn't
  take, so audioconvert picks S24LE unless told otherwise.
- The element runs well ahead of the sink: playbin3's playsink queues ~1 s after the audio-filter.
  With fakesink, results arrived ~1.25 s before their audio played (and a 1 s "too far ahead" cut
  dropped every one of them).
- For a non-live pipeline the sink plays the sample with running-time T when the pipeline clock
  reads base_time + T (the audio sink's clock follows the device's play-out). So clock − base_time
  is the running-time being heard. It keeps counting across playbin3 gapless advances. A flushing
  seek restarts it; `Player.position` (100 ms steps, reset at each gapless track) is too coarse.
- Calibration on real tracks (24 log bands, power summed per band): a quiet 1970s master's bands
  sit around −45 dB (loud moments −30), a loud modern one's around −34 (bass −20). A 0.3-amplitude
  sine's band is about −21 dB.
- ctest's offscreen platform uses Qt Quick's software renderer, which doesn't draw custom geometry,
  materials or shader effects. Rectangle nodes, images and Shapes do render.

## Decision

- The audio-filter always holds `[rgvolume ! rglimiter !] audioconvert !
  audio/x-raw,format={S16,S32,F32,F64} ! spectrum bands=1024 interval=33333333 threshold=-80`,
  when `spectrum` is available. It sits before playbin's volume, so the volume doesn't move the
  bars, and the formats are limited so the S24LE path is never used. The ReplayGain part still
  comes and goes only at READY. The analyser switches with `post-messages` (an engine command,
  acked like the rest; a new engine gets it like the volume), and busSync drops spectrum messages
  while it's off.
- Results are mapped to 24 bands (40 Hz–16 kHz, log-spaced, power summed, floor −66 dB, top
  −15 dB) on the engine thread and held until their audio is heard: the interval's end
  (running-time + duration) against clock − base_time, with a single-shot timer for the next one.
  Only the newest due result is emitted. The queue is kept across pause and gapless advances, and
  cleared with a generation bump at a flushing seek, stop, setSource, EOS, error and when the
  analyser turns off. At most 192 are held (~6 s); one due more than 5 s ahead is dropped.
- UI: `Playback.levels` (24 values, zeroed when not playing); `analyzerEnabled` (QSettings
  `analyzer/enabled`) and `windowVisible` (Main: minimised / hidden) together decide whether the
  Player analyses. `SpectrumMeter` (C++) draws QSGRectangleNodes; its ballistics run in `step()`,
  called by a FrameAnimation only while it's animating and the window shows.

## Revisit when

- A live source or a sink with configured latency plays: the hold would then need the pipeline's
  latency added.
- A future GStreamer fixes `spectrum`'s S24LE scaling (the format list can stay either way).
- The meter needs more than ~300 rectangles, or the tests move off the software renderer: one
  geometry node, or a shader, would then be fine.
- The floor / top values feel wrong across the user's library: they're `SpectrumMapping` defaults.
