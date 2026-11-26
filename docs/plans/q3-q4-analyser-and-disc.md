# Q3 audio analyser + Q4 spinning MiniDisc: design

Plan for PLAN.md's **Q3 Real audio analyser** and **Q4 Spinning MiniDisc in the transport bar**.
Gemini 4 Neon drafted it (2026-11-25). Claude then checked it against the code and this machine and
corrected it; changes from Neon's draft are marked **(changed)**. The PLAN.md "Done when" text is the
contract. This file is the how. If the two disagree, PLAN.md wins: fix this file.

User's choices: segmented LED bars (not the filled curve in the old pencil sketch); the disc right after
the analyser; a bare disc (no cartridge).

**Q3 as built (2026-11-25)**: the plan held, with these changes; decision record 0010 has the evidence:
- **Hold limits:** Neon's 5 s was right and my 1 s correction was wrong. playsink queues ~1 s after the
  audio-filter, so results arrive ~1.25 s before they're heard. Held: up to 192 (~6 s); dropped when
  due more than 5 s ahead.
- **Formats:** `spectrum`'s packed 24-bit (S24LE) path reads ~44 dB too loud, and 24-bit FLAC would
  get it. The chain is `audioconvert ! audio/x-raw,format={S16,S32,F32,F64} ! spectrum …`.
- **When a result counts as heard:** at running-time + duration, the interval's end. Bars may trail
  by one interval, never lead.
- **Scale:** floor −66 dB and top −15 dB (measured on real tracks), not −60 / −3. The names are
  `floorDb` / `topDb`.
- **New engines:** no constructor parameter. `createEngine()` sends `setSpectrumActive` like the volume.
- **Neon's review of the code:** fixed: results wait for Playing after a seek's re-preroll; invalid
  (NONE) times are skipped; levels clear on a seek; the meter's node list lives in its root node;
  the sample rate is read where the message is posted (busSync, from the message's element); null
  checks after `gst_bin_get_by_name`; the meter resets when the window hides; segment gaps are
  symmetric. Not done: engine construction off the UI thread (pre-existing; PLAN.md follow-ups).
- **Menu bug:** `AppContextMenu` made its items parentless (JS-owned), so the garbage collector could
  collect them while the menu was open.

**Q4 as built (2026-11-25)**: the plan held, with these changes; decision record 0011 has the details:
- **Size:** 56 px, the analyser box's height in a 72 px row. There's no separator between them, and a
  `Theme.spaceSM` gap after the disc.
- **Inputs:** plain properties (`coverKey`, `coverUrl`, `playing`, `paused`, `spinEnabled`, `shown`)
  bound to Playback, so tests can drive the disc directly.
- **Settling:** spin and cover decisions wait a `Qt.callLater`, because `playing` / `paused` (and
  `coverKey` / `coverUrl`) share one NOTIFY and can update in either order.
- **The turning mark:** a faint printed wedge (`Theme.discSheen`) under the cover, so a disc without
  a cover visibly turns. The hub is about 32 % of the disc, plus a spindle hole in
  `Theme.surfaceChrome`.
- **Missing picture:** if the image provider fails, the disc stays plain. The analyser's extra JS allocations made that show
  (tst_contextmenu_export failed in release). Items now get the menu as parent.

## Facts checked before planning

- **`spectrum` posts at 30 Hz at any bin count.** It keeps a ring buffer of the last `nfft` samples and
  runs an FFT at each interval if none ran yet, so windows overlap. Measured with `gst-launch-1.0 -m
  audiotestsrc ! spectrum bands=N interval=33333333 ! fakesink`: 300 messages per 10 s for N = 512,
  1024 and 2048. Neon's draft said only ≤ 512 bins could post at 30 Hz: wrong.
- **ctest's offscreen platform uses Qt Quick's software renderer**: `qt.scenegraph.general: Loading
  backend software`, `GraphicsInfo.api === GraphicsInfo.Software`. That renderer draws no custom
  geometry or materials, `ShaderEffect` or `MultiEffect`. Anything drawn that way is missing from
  `MMM_SCREENSHOT_DIR` screenshots and grab-based tests. Rectangles, images, text and Shapes do render.
- **`Playback.coverUrl` changes on every track** of an album whose tracks have embedded art.
  `CoverLoader::url(albumId, coverPath, hasEmbeddedArt ? path : "")` puts the track's path into the
  base64 payload. Only the first path segment (`<albumId>[.<revision>]`) is stable per album.
- **Clock vs heard audio**: file playback is a non-live pipeline, so no latency is configured. The audio
  sink writes the sample with running-time T to the ring buffer slot that plays when the pipeline clock
  reads base_time + T. The sink's clock follows the device's play-out, so `clock − base_time` is the
  running-time being heard now. It keeps rising across playbin3 gapless advances. A flushing seek
  resets it.
- **Transport bar width today**: 232 (media) + 176 (analyser min 160 + 2 × 8 margins) + 168 (volume 136
  + 2 × 16) + 252 (toggles 220 + 2 × 16) + 3 separators ≈ 831 px. `Main.minimumWidth` is 900. A 60 px
  disc plus a gap doesn't fit.
- Playback's own volume / RG setters call `m_controller.player()` directly (`Playback::setVolume`), so
  the analyser setting can do the same. It isn't a playback command, so PlaybackController needs no
  change.

---

# Q3: Real audio analyser

## Data flow and threads

```
[GStreamer streaming thread]
  … ! rgvolume ! rglimiter ! spectrum name=spec (bands=1024, interval=33 333 333 ns, threshold=-80)
     └─ posts "spectrum" element messages → busSync():
          if it's a spectrum message and !m_spectrumActive (atomic): drop it (unref, GST_BUS_DROP)
          capture m_busGeneration and m_spectrumGeneration → queued to the engine thread

[PlayerEngine thread]
  handleSpectrum(msg, specGen):
     if specGen != m_spectrumGeneration: return            (pre-seek / pre-stop leftovers)
     rate = current caps of spec's sink pad ("rate")        (changed: no notify::caps handler)
     levels = mmm::mapSpectrumToBands(magnitudes, rate)     (std::array<float, 24>)
     m_pendingSpectrum.push_back({runningTimeNs, levels})   (guards below)
     if Playing: flushDueSpectrum()
  flushDueSpectrum()  (also the single-shot Qt::PreciseTimer's slot)
     nowRt = gst_clock_get_time(clock) − gst_element_get_base_time(pipeline)
     pop every entry with runningTime ≤ nowRt + 5 ms; emit only the newest:
        Q_EMIT spectrumLevels(epoch, newest)
     if entries remain and Playing: m_spectrumTimer.start(clamp(dueMs, 4, 200))

[UI thread]
  Player (isCurrent(engine, epoch)) → Player::spectrumLevels → Playback.levels (QList<qreal>, 24)
  AudioAnalyzer.qml: SpectrumMeter.levels ← Playback.levels
     FrameAnimation (running while meter.animating && Playback.windowVisible) → meter.step(frameTime)
  SpectrumMeter::updatePaintNode → QSGRectangleNodes (changed: not a custom geometry node)
```

## 1. Band mapping: `src/core/spectrum.{h,cpp}` (new, pure, headless)

```cpp
namespace mmm {
inline constexpr int kSpectrumBandCount = 24;
using SpectrumLevels = std::array<float, kSpectrumBandCount>;

struct SpectrumMappingConfig {
    double minFreqHz = 40.0;
    double maxFreqHz = 16000.0;
    float minDb = -60.0f;     // display floor  (as built: floorDb = -66, measured)
    float maxDb = -3.0f;      // display top    (as built: topDb = -15, measured)
    float thresholdDb = -80.0f; // = the element's threshold; bins at/below it count as silence
};

// Band edges, log-spaced: kSpectrumBandCount + 1 values from minFreqHz to maxFreqHz.
std::array<double, kSpectrumBandCount + 1> spectrumBandEdges(double minFreqHz, double maxFreqHz);

// `binMagnitudesDb`: spectrum's "magnitude" list (N bins, DC … Nyquist; N = the element's bands).
SpectrumLevels mapSpectrumToBands(std::span<const float> binMagnitudesDb, int sampleRateHz,
                                  const SpectrumMappingConfig &cfg = {});
}
Q_DECLARE_METATYPE(mmm::SpectrumLevels)
```

- **Bins (changed):** `bands = 1024`, so `nfft = 2·1024 − 2 = 2046`. Bin k is centred on
  `k · rate / nfft` (≈ 21.5 Hz apart at 44.1 kHz). Skip bin 0 (DC).
- **Bands:** 24 log-spaced edges, 40 Hz–16 kHz (each band ≈ 1.284× the last). Clamp the top edge to
  Nyquist for low sample rates.
- **Power sum:** for each band, add up `10^(dB/10)` of the bins it covers, weighted by the bin's
  fractional overlap with the band (bin k spans `[(k−½)·binHz, (k+½)·binHz]`). Music is roughly pink
  (equal energy per octave), so summing power per log band looks balanced from bass to treble, and a
  pure tone still lands in one band.
- **Narrow bass bands:** the lowest bands (40–51 Hz …) are narrower than one bin even at 1024 bins. When
  the overlap weight is < 1, use `power / weight`, so a 0 dBFS 45 Hz tone still reaches full scale.
- **Silence:** a bin with `dB ≤ thresholdDb + 0.5`, or a non-finite value, adds 0. Silence then gives
  exactly 0 in every band, however many bins a band covers.
- **To 0..1:** `clamp((10·log10(power) − minDb) / (maxDb − minDb), 0, 1)`; power 0 → 0.
- Keep minDb / maxDb as the only tuning knobs, and tune them with real music during the user test.

## 2. Engine (`src/core/playerengine.{h,cpp}`)

- **The filter bin is always built.** `applyAudioFilter()` now builds `"rgvolume name=rg ! rglimiter !
  spectrum name=spec"` when RG is on, or `"spectrum name=spec"` when it's off. The spectrum is set to
  `bands=1024 interval=33333333 threshold=-80 message-phase=false post-messages=<active>`. Track what's
  installed (RG on/off; spectrum present) instead of today's `want == m_rgFilterActive` early return.
  The bin is still only swapped at READY / NULL (ctor, setSource, stop, setReplayGainMode while
  Stopped). Keep a ref to `spec` like `m_rgVolume`, released in `shutdown()` / the dtor and when the
  bin is replaced. If `spectrum` can't be made (gst-plugins-good missing), warn once and run without
  an analyser, as RG does.
- **New command** (acked like the rest, watched by the watchdog):
  `void setSpectrumActive(quint64 seq, bool active);` sets `post-messages` on `spec`, stores the atomic
  `m_spectrumActive` (read by busSync), and when turning off clears the queue and stops the timer.
  (As built: no constructor parameter; `createEngine()` sends this command to every new engine, like
  the volume, so a replacement engine starts with the same setting.)
- **New signal:** `void spectrumLevels(quint64 epoch, const mmm::SpectrumLevels &levels);`
- **busSync:** for an `GST_MESSAGE_ELEMENT` whose structure is named `"spectrum"`, drop it right away
  when `!m_spectrumActive`. Otherwise capture `m_spectrumGeneration` alongside `m_busGeneration` and
  handle it on the engine thread as today.
- **Parsing (engine thread):** check `GST_MESSAGE_SRC(msg) == spec`. Read `"running-time"` (guint64)
  and `"magnitude"` (a GstValueList of floats: `gst_value_list_get_size` /
  `gst_value_list_get_value` → `g_value_get_float`). **(changed)** Get the sample rate from
  `gst_pad_get_current_caps(spec's sink pad)` → `"rate"` on each message (30/s, cheap). Neon's
  `notify::caps` handler would have to be disconnected before teardown, which Neon listed as an ASan
  risk itself. A message posted just before a gapless rate change may be mapped with the new rate:
  one frame, invisible.
- **The queue:** `std::deque<PendingSpectrum> m_pendingSpectrum` with `PendingSpectrum { quint64
  runningTimeNs; SpectrumLevels levels; }` and a single-shot `Qt::PreciseTimer` `m_spectrumTimer`
  (a child of the engine, like the position timer). Guards:
  - an entry whose running-time is > 50 ms *older* than the last one queued (a discontinuity) clears
    the queue first;
  - drop an entry due more than 5 s ahead of now (Neon's figure; a 1 s cut dropped everything,
    because playsink queues ~1 s after the filter);
  - cap at 192 entries (≈ 6 s).
- **When to clear / keep the queue:**

  | Event | Queue | Timer | `m_spectrumGeneration` |
  | --- | --- | --- | --- |
  | Pause | keep: that audio is still in the sink and plays on resume (base_time moves) | stop | — |
  | Resume (STATE_CHANGED → Playing) | `flushDueSpectrum()` | as needed | — |
  | Preroll (PAUSED before the first PLAYING) | collect | stopped | — |
  | Flushing seek (`doSeek`, **after** `gst_element_seek_simple` returns) | clear | stop | bump |
  | Gapless advance (STREAM_START) | keep: running-time continues | — | — |
  | setSource / stop / EOS / error (`resetStream` and the EOS/error paths) | clear | stop | bump |
  | `setSpectrumActive(false)` | clear | stop | — |

  Bumping only `m_spectrumGeneration` on a seek (not `m_busGeneration`) keeps the seek's ASYNC_DONE
  alive. Messages posted between the flush and the bump are dropped too: a few ms of bars, harmless.

## 3. Player and Playback

- `Player`: `Q_PROPERTY(bool analyzerActive READ analyzerActive WRITE setAnalyzerActive NOTIFY
  analyzerActiveChanged FINAL)` and a signal `spectrumLevels(const mmm::SpectrumLevels &levels)`. The
  property defaults to false, so existing tests and tools don't change. `Player::Private` keeps the
  value and passes it to every new engine, including after a watchdog recovery. The event goes through
  `isCurrent(e, ev)` like the rest.
- `Playback` (QML singleton):
  - `Q_PROPERTY(QList<qreal> levels READ levels NOTIFY levelsChanged FINAL)`: 24 values, or all zero.
    Zeroed on pause, stop, or when the analyser goes inactive.
  - `Q_PROPERTY(bool analyzerEnabled READ … WRITE … NOTIFY … FINAL)`: default true, persisted
    (`analyzer/enabled`) only when `persistSettings`.
  - `Q_PROPERTY(bool windowVisible READ … WRITE … NOTIFY … FINAL)`: default true, not persisted.
    Q4 uses it too.
  - The Player gets `analyzerEnabled && windowVisible` (through `m_controller.player()`, like volume).
- `Main.qml` (the ApplicationWindow):
  ```qml
  readonly property bool windowShown: visible && visibility !== Window.Minimized && visibility !== Window.Hidden
  onWindowShownChanged: Playback.windowVisible = windowShown
  ```
  plus the same assignment in `Component.onCompleted`. Known limit: tiling Wayland compositors have no
  "minimised" state, so a window on another workspace still counts as shown.

## 4. The meter: `src/ui/spectrummeter.{h,cpp}` (new C++ QQuickItem, `QML_ELEMENT`)

**(changed)** Neon's draft drew everything with one `QSGGeometryNode` + `QSGVertexColorMaterial`. That
is fast, but the software renderer used by ctest's offscreen screenshots doesn't draw it. Build the node
tree from **`QSGRectangleNode`s** (`window()->createRectangleNode()`) instead: one per segment plus one
peak cap per band (24 × segments + 24 nodes, created when the size or segment count changes). Each frame
only updates colours and the peak caps' rects. Every renderer draws these, and with RHI they batch into a
few draw calls.

- Properties: `levels` (`QList<qreal>`), `playing` (bool), `animating` (bool, read-only),
  `segmentCount` (int, by default from the height: ≈ 3 px segments with 1 px gaps), and the colours
  `unlitColor`, `lowColor`, `midColor`, `highColor`, `peakColor`, set from Theme in QML.
- Invokables (for the QML tests): `void step(qreal dtSeconds)`, `void reset()`, `int litSegments(int
  band) const`, `int peakSegment(int band) const` (−1 = none), `qreal displayLevel(int band) const`,
  `qreal peakLevel(int band) const`.
- Bars: 24 columns across the width with 2–3 px gaps. A segment is lit when it's below
  `round(display · segmentCount)`. Lit colours step low → mid → high with height. Unlit segments use
  `unlitColor`, so the whole grid is always faintly visible, like a real LED meter.
- **Physics** in `step(dt)`, with `dt` clamped to [0, 0.05]:
  - attack: exponential towards the target, τ = 14 ms (≈ 90 % in 32 ms);
  - fall: constant 1.6 units/s (full scale in ≈ 0.6 s). The scale is already dB, so a constant fall
    looks like a real meter and reaches exactly 0;
  - peak: `max(peak, display)`. A new peak holds 0.5 s, then falls at 0.85 units/s down to the bar.
  - `animating` = playing, or any display / peak still > 0. It goes false once everything is at rest
    and not playing.
- Theme tokens (new, CONSTANT, in `theme.h`): `analyzerUnlit`, `analyzerLow` (the accent amber),
  `analyzerMid`, `analyzerHigh`, `analyzerPeak`. Neon's starting values were `#232730`, accent,
  `#f2bc57`, `#ffe08a`, `#fff4cc`; judge them in the screenshot.

## 5. `src/ui/qml/AudioAnalyzer.qml` (replaces `AudioAnalyzerStub.qml`)

The same recessed inset as the stub (`Theme.surfaceInset`, `Theme.borderSubtle`, `Theme.radiusSmall`)
around a `SpectrumMeter`, with:
```qml
FrameAnimation {
    running: meter.animating && Playback.windowVisible
    onTriggered: meter.step(frameTime)
}
```
When `!Playback.analyzerEnabled`, a muted "Analyser off" label (`Theme.fontMicro`, `Theme.textMuted`)
shows. A right-click `TapHandler` calls `ContextMenus.open(...)` with one entry, "Turn analyser off" /
"Turn analyser on". `TransportBar.qml` swaps the stub for it. Remove the stub from `src/ui/CMakeLists.txt`.

## Q3 files

| File | Change |
| --- | --- |
| `src/core/spectrum.{h,cpp}` | new: band mapping |
| `src/core/playerengine.{h,cpp}` | filter bin, `setSpectrumActive`, queue + timer, `spectrumLevels` |
| `src/core/player.{h,cpp}` | `analyzerActive`, `spectrumLevels`, carried across recovery |
| `src/core/CMakeLists.txt` | add spectrum.* |
| `src/ui/playback.{h,cpp}` | `levels`, `analyzerEnabled` (QSettings), `windowVisible` |
| `src/ui/spectrummeter.{h,cpp}` | new: the meter item |
| `src/ui/theme.h` | `analyzer*` tokens |
| `src/ui/qml/AudioAnalyzer.qml` | new; `AudioAnalyzerStub.qml` removed |
| `src/ui/qml/TransportBar.qml`, `Main.qml` | use it; window visibility → Playback |
| `src/ui/CMakeLists.txt` | register the new C++ and QML files |
| `tests/core/testmedia.h/.cpp` | `generateTone(..., double freqHz = 440.0)` |
| `tests/core/tst_spectrum.cpp`, `tests/qml/tst_analyzer.qml` | new tests |
| `docs/decisions/0010-audio-analyser.md` | new decision record |

## Q3 tests

1. `tst_spectrum` (headless, `QTEST_GUILESS_MAIN`), with synthetic 1024-bin inputs:
   - all bins at the threshold, and all at −60 dB → every band exactly 0;
   - a 0 dB bin at 1 kHz → its band > 0.9, bands two or more away = 0;
   - 60 Hz lights band 0–2, 8 kHz lights band 19–21;
   - a pink (1/f) spectrum → bands 3–22 within ±0.08 of each other;
   - an empty span, rate 0, a 22.05 kHz rate (top band clamped), NaN / −inf → no crash, sane output.
2. `tst_player`, run in **both** the debug and release presets:
   - analyser on, a 1 kHz tone (`generateTone(..., 1000.0)`): levels arrive with the peak in the 1 kHz
     band. Pause, then after `Paused` is observed clear the spy: nothing arrives for 250 ms. Resume:
     levels return;
   - two tones chained gaplessly (200 Hz FLAC → 4 kHz Ogg): levels keep coming after `advancedToNext`
     and the peak moves from the bass bands to the treble bands;
   - seek mid-track → levels resume; Track ↔ Album while playing → they keep coming; stop, RG Off →
     Track, play → they come (the rebuilt bin has the spectrum);
   - **(added)** volume 1.0 vs 0.1 on the same tone: the peak band's level is the same (± 0.05). This
     proves the spectrum sits before the volume;
   - `analyzerActive = false` while playing: nothing arrives for 250 ms;
   - a replaced engine (the `stalledEngineIsReplaced` set-up) still posts levels.
3. `tst_analyzer.qml`: feed levels and call `meter.step(0.05)`; check `litSegments` / `peakSegment`. Drop
   to 0 and step 0.40 s: the peak is still held and the bars have fallen. Another 0.8 s: the peak is −1
   and `animating` is false. The right-click entry toggles `Playback.analyzerEnabled` and its text flips.
   At rest, the FrameAnimation isn't running.
4. Screenshot (`tst_views` `MMM_SCREENSHOT_DIR`): at rest and with levels. Look at it.

## Q3 risks

| Risk | Avoid it by |
| --- | --- |
| Leaked refs to `spec`, its pad, the caps or the clock | RAII holders (`unique_ptr` + `gst_object_unref` / `gst_caps_unref`); ASan in debug |
| Pre-seek messages shown after a seek | bump `m_spectrumGeneration` after the seek returns; the discontinuity guard |
| Flaky timing between debug and release | clear spies only after the state change is observed; run both presets |
| `qmlcachegen` `-Wconversion` | keep the maths in C++; `levels` is `QList<qreal>` |
| A state change during STREAM_START (gapless pitfall 5) | none needed: the analyser never changes state; the bin is only swapped where it is today |

---

# Q4: Spinning MiniDisc

## Layout

`[ media 232 ] | [ AudioAnalyzer (fill, min 160) ] [ SpinningDisc 60×60 ] | [ volume ] | [ toggles ]`

There's no separator between the analyser and the disc. A `Theme.spaceSM` gap makes them read as one
deck window. **(changed)** Raise `Main.qml`'s `minimumWidth` from 900 to about 960 (the bar needs
≈ 831 + 68 px). A test checks the analyser keeps 160 px at the minimum width.

## Look (bare disc, real proportions: 64 mm disc, ≈ 20 mm hub → hub ≈ 31 %)

- **Turning layer:** the plain dark disc (`Theme.discBase`), plus a faint asymmetric mark or sheen
  (`Theme.discSheen`) so a coverless disc visibly turns. On top of that, the round cover, two image
  slots for the cross-fade.
- **Fixed overlay:** a 1 px rim (`Theme.discRim`), a fixed top-left highlight, the ≈ 19 px metal hub
  (`discHub` → `discHubHighlight` gradient, `discHubBorder` ring), and a ≈ 6 px spindle hole in
  `Theme.surfaceChrome`, so it reads as a hole in the bar.
- New Theme tokens: `discBase`, `discSheen`, `discRim`, `discHub`, `discHubHighlight`,
  `discHubBorder`. Neon's starting values were `#1c2028`, `#20ffffff`, `#3e4656`, `#7d8594`,
  `#b2bac8`, `#4a5160`; judge them in the screenshot.

## Round covers from the provider (changed)

Neon's draft clipped the cover with `MultiEffect`. The software renderer doesn't draw that, so the disc
would be blank in test screenshots, and it would need offscreen layers. Instead:
- `CoverImageProvider` gets a shape option. Register it a second time as `"coverround"` (main.cpp and
  tst_qml's Setup). Its `requestImageResponse` loads the normal bucket through `CoverLoader::load`
  (same disk cache: the JPEG stays square, with no alpha). Then, on the provider's thread, it
  centre-crops to a square and masks it to a circle: an `ARGB32_Premultiplied` image, `QPainter` with
  antialiasing, an ellipse clip or a `DestinationIn` circle. At 128 px that takes microseconds, so the
  round result isn't cached.
- `CoverLoader::roundUrl(...)` (or a variant of `url`) gives `image://coverround/<same id>`.
  `Playback` exposes `discCoverUrl` and `coverKey` (`<albumId>[.<revision>]`, or "" with no art), both
  NOTIFY `currentChanged`.
- The disc is then plain `Image`s that rotate. This renders in every backend.

## Spinning (changed)

No `RotationAnimator`. Neon's draft explains why: its angle isn't visible on the GUI thread while it
runs, and stopping writes it back late. Neon's replacement had a problem too: a 380 ms OutCubic coast
over 36° starts at ≈ 284°/s, but the spin is ≈ 138°/s, so the disc would lurch faster on pause.
Integrate a speed instead:

```qml
property real angle: 0                       // degrees, the turning layer's rotation
property real speed: 0                       // 0..1 of full speed
readonly property real degPerSec: 360 / 2.6
readonly property bool wantSpin: Playback.playing && Playback.discSpinEnabled && Playback.windowVisible
readonly property bool animating: speed > 0

NumberAnimation { id: speedAnim; target: root; property: "speed"; easing.type: Easing.OutQuad }

onWantSpinChanged: {
    speedAnim.stop();
    if (wantSpin) {                                   // spin up from the current speed
        speedAnim.to = 1; speedAnim.duration = 400; speedAnim.start();
    } else if (!Playback.discSpinEnabled || !Playback.windowVisible) {
        speed = 0;                                    // off / minimised: halt, keep the angle
    } else {                                          // pause: coast; stop: quick stop
        speedAnim.to = 0; speedAnim.duration = Playback.paused ? 800 : 250; speedAnim.start();
    }
}

FrameAnimation {
    running: root.speed > 0 && Playback.windowVisible
    onTriggered: root.angle = (root.angle + root.speed * root.degPerSec * Math.min(frameTime, 0.05)) % 360
}
```

The speed never jumps (OutQuad starts from the current value), so resuming mid-coast just spins back
up. The angle is a plain property, which tests can read. Nothing runs once the speed is 0. The cost is
that it ticks on the GUI thread, so a long GUI-thread stall makes it stutter (fine at 60 px).

## Cross-fade without flashing

- Two `Image` slots (`asynchronous: true`, `sourceSize: 128×128`, `PreserveAspectCrop`) over the plain
  disc.
- **(changed)** The guard compares `Playback.coverKey`, not the URL. Only a different key (or art
  appearing / disappearing) loads anything.
- New key: load the round URL into the hidden slot at opacity 0, above the visible one. Once it's
  `Image.Ready`, and its source is still the wanted one (fast skipping), fade it 0 → 1 (240 ms) **over**
  the old slot, which stays fully opaque underneath so the dark disc never shows through. Then clear
  the old slot and swap their roles.
- Key → "" (no art, or stopped with nothing loaded) or `Image.Error`: fade both slots out (220 ms) to
  the plain disc.

## Interaction

- Hover: a `ToolTip` with "album — artist" (hidden with no track).
- Left click: `ContextMenus.goToAlbumRequested(Playback.currentAlbumId)` when there's a track with an
  album.
- Right click: `ContextMenus.nowPlayingEntries()` (with a track), then a separator, then "Stop spinning" /
  "Start spinning". The spin entry shows with no track too.
- `Playback.discSpinEnabled`: default true, persisted (`transport/discSpin`) only when `persistSettings`.
- Read-only properties for tests: `angle`, `animating`, `showingCover`.

## Q4 files

| File | Change |
| --- | --- |
| `src/ui/coverimageprovider.{h,cpp}` | shape option, `roundUrl` |
| `src/app/main.cpp`, `tests/qml/tst_qml.cpp` | register `"coverround"` |
| `src/ui/playback.{h,cpp}` | `discCoverUrl`, `coverKey`, `discSpinEnabled` |
| `src/ui/theme.h` | `disc*` tokens |
| `src/ui/qml/SpinningDisc.qml` | new |
| `src/ui/qml/TransportBar.qml` | the disc after the analyser |
| `src/ui/qml/Main.qml` | `minimumWidth` ≈ 960 |
| `src/ui/CMakeLists.txt` | add the QML file |
| `tests/ui/tst_uimodels.cpp` | the round variant (next to the cover tests) |
| `tests/qml/tst_disc.qml` | new |
| `docs/decisions/0011-spinning-disc.md` | new decision record |

## Q4 tests

1. Cover provider (`tests/ui/tst_uimodels.cpp`): the round variant of the fixture's art is square, its corners are
   transparent (alpha 0) and its centre opaque. A missing cover gives a null / error image.
2. `tst_disc.qml`:
   - playing (the fixture's `Kid A/01.flac`): `animating` is true and `angle` advances. Pause:
     `tryCompare(disc, "animating", false)`, then the angle holds for 150 ms. Resume: it continues
     from the held angle (no reset to 0);
   - stop → `animating` false. `discSpinEnabled = false` while playing → halts at once, angle kept;
   - no track → `showingCover` false. A track with art → true. Another track of the **same album**
     (different `coverUrl`, same `coverKey`) → `showingCover` stays true with no reload (the slot's
     source is unchanged). No art → back to the plain disc;
   - right click: the now-playing entries plus "Stop spinning". Toggling it flips the text and
     `Playback.discSpinEnabled`. With no track, only the spin entry;
   - the main window at `minimumWidth`: the analyser is ≥ 160 px wide and the disc 60 px, with nothing
     clipped on the right.
3. Screenshot: the disc with a cover and plain (rim, hub, spindle hole). Look at it at 1× and 2×.

## Q4 risks

| Risk | Avoid it by |
| --- | --- |
| The cover reloads on every track (the URL includes the track path) | compare `coverKey` |
| A stale cover after fast skipping | fade only when the ready slot's source is still the wanted one |
| The bar overflowing at the minimum width | raise `minimumWidth`; test it |
| The disc animating while hidden | `wantSpin` and the FrameAnimation both need `Playback.windowVisible` |
| `-Wconversion` from qmlcachegen | typed properties (`real`, `int`, `bool`, `string`); no untyped `var` maths |

---

## Decision records to write while implementing

- **0010 audio analyser**: `spectrum` in playbin3's audio-filter (before the volume); results held on
  the engine thread against clock − base_time (not `Player.position`: 100 ms steps, and stream-time
  resets at each gapless track); 1024 bins at 30 Hz; drawn with rectangle nodes so the software
  renderer shows it. Revisit if a live source (latency) or another sink makes the bars lead or lag.
- **0011 spinning disc**: a GUI-thread FrameAnimation integrating speed (not RotationAnimator); round
  covers made by the provider (not MultiEffect / shaders). Revisit if the GUI thread stalls make it
  stutter, or if shader effects become fine because tests move off the software renderer.
