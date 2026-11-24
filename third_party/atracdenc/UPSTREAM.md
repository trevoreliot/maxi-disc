# atracdenc (vendored)

- **From:** https://github.com/dcherednik/atracdenc, tag `0.2.3`
  (commit `ec98d6e3fa68565ffd524b869ee7d7918b7d52e7`, 2026-07-08), with its one submodule
  `src/lib/libgha` from https://github.com/dcherednik/libgha at `b4ed00bfd942e476fc9ab95adc0bc77bac407b28`
  (branch `at3pghadev`).
- **Copied:** the whole tree without git metadata (`.git`, `.gitmodules`, `.github`) and without
  `debian/` (packaging).
- **Why vendored:** no package on this distribution, the build must work offline, and the app
  needs a known version: the LP2 / LP4 frame sizes below are what the recorder accepts
  (docs/decisions/0007).

## Licence

atracdenc is LGPL-2.1 (`LICENSE`); libgha is BSD-2-Clause (`src/lib/libgha/LICENSE`).
music-md-manager runs atracdenc as a separate program and doesn't link it.

## How it's built

`third_party/atracdenc.cmake`: an `ExternalProject` from this directory (no download), Release,
none of our warning flags or sanitizers, `CMAKE_DISABLE_FIND_PACKAGE_GTest=ON` (its unit tests
would otherwise join our ctest), and `-include cstdint` (0.2.3 relies on `<cstdint>` coming in
through other headers, which GCC 15+ no longer does). Needs libsndfile. The binary is copied next
to the app (`build/*/src/app/atracdenc`).

## What we rely on (checked against 0.2.3's source and a real encode)

- `-e atrac3` with no `--bitrate` = 132300 bit/s = **384-byte** frames (MiniDisc LP2).
- `-e atrac3_lp4` = 64 × 1024 bit/s, rounded up by `GetContainerParamsForBitrate` to 66150 bit/s =
  **192-byte** joint-stereo frames (LP4).
- `-o x.at3` writes RIFF: format tag 0x270, 2 channels, block align = frame size, then a `data`
  chunk of whole frames (1024 samples each). `--container raw` is newer than 0.2.3.
- The `data` size in that header counts whole *input* frames (written before encoding); one or two
  more frames follow (the encoder's delay, i.e. the end of the music: 86 in the header, 88 written
  for 88200 samples). `AtracEncoder::readAt3` keeps everything to the end of the file.
- Input: 44.1 kHz 16-bit WAV (GStreamer writes it first). `--nostdout` silences progress.

`tests/core/tst_atracencoder.cpp` checks the frame sizes on every build.

## Local patches

None.
