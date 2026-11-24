# 0007: LP2 / LP4 through a vendored atracdenc, run as a separate program

## Context

M12 records LP2 / LP4 to the MZ-NE410. NetMD takes LP as ATRAC3 made on the computer (the deck
only encodes SP itself). The only free ATRAC3 encoder is atracdenc (LGPL-2.1). It isn't packaged
here, ships only a command-line program (no library API), and ffmpeg can decode ATRAC3 but not
encode it.

## Evidence

- atracdenc 0.2.3's source: LP2 is its ATRAC3 default (132300 bit/s, 384-byte frames); its
  `atrac3_lp4` alias gives 66150 bit/s, 192-byte joint-stereo frames; `.at3` output is RIFF with
  the frames in the `data` chunk. A real encode of a test tone matched (44 frames for 44 × 1024
  samples).
- libnetmd and netmd-js agree on the wire side: LP data goes unmodified, counted in 192 / 96-byte
  units (an ATRAC3 frame is two), wire formats 0x94 / 0xa8, disc formats 2 / 0.

## Decision

Vendor atracdenc 0.2.3 (+ libgha) under `third_party/atracdenc`, build it as its own CMake project
with ours (offline, its own flags), put the binary next to the app, and run it with QProcess from
`AtracEncoder` (decode to WAV with GStreamer → atracdenc → keep the `data` chunk). A separate
process keeps the LGPL boundary simple, can't crash the app, and cancels by being killed.
`MMM_ATRACDENC` points to another binary; if none is found, LP is disabled with the reason.

## Revisit when

- atracdenc gains a library API or a distribution package (link it, or use the system one).
- A newer atracdenc changes the defaults (the frame-size test fails): pin the bitrate explicitly.
- Streaming (GStreamer → atracdenc's stdin) would save the temporary WAV (`-i -` is documented but
  unverified in 0.2.3).
