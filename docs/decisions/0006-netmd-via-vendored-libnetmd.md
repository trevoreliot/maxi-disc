# 0006 — NetMD through a vendored, patched libnetmd behind a small backend interface

- **Status:** accepted and implemented (2026-11-23, M10): `third_party/libnetmd/` (see its
  `UPSTREAM.md`), `src/core/libnetmdbackend.cpp`, `src/core/netmdmanager.cpp`,
  `src/core/netmdmonitor.cpp`, `src/core/mdtitle.cpp`; tests `tst_mdtitle`, `tst_netmdmanager`
- **Decided by:** Claude Code during M10 (planned with Gemini 4 Neon); approved by the user. The
  user can revisit
- **Revisit if:** libnetmd blocks uploads (M11/M12) or misbehaves on the MZ-NE410, or a
  distribution starts packaging it

## Context

The plan names libnetmd (linux-minidisc) for NetMD. It isn't packaged on Arch/CachyOS, builds
with qmake only, was last tagged in 2019, and has rough edges: leaks, unchecked replies, no
thread safety, blocking calls of up to ~30 s, and a disc-title parser that writes a literal
"<Untitled>" back. Licence (per file): LGPL-2.1-or-later and GPL-2.0-or-later, compatible with
the app's GPL-3.0-or-later.

## Decision

1. **Vendor** libnetmd's library sources at a pinned commit into `third_party/libnetmd/`, built by
   our own CMake as a C static library without our warning flags. Small fixes are patched in place
   and listed in `UPSTREAM.md` (leaks, error reporting, exported disc-header read, track hours).
2. **One file talks to libnetmd** (`libnetmdbackend.cpp`) through the `NetMdBackend` interface;
   tests use an in-memory fake. A native libusb implementation could replace it later.
3. **The disc header is ours:** read raw, parsed and formatted by `MdTitle` (groups kept,
   `0;Title//` form when needed), written raw; titles are cleaned to ASCII first.
4. **One worker thread** owns the backend; every command runs there in order; stale results are
   dropped (generation). Exit never waits more than 5 s on a stuck USB command.
5. **Detection by polling** USB every 2 s with a separate libusb context (no hotplug thread).
6. Track count = asking for titles until the recorder says there's no such track (netmdcli's way).

## Costs

- We carry patches to an old C library; upstream fixes must be merged by hand.
- Polling means a recorder shows up within ~2 s, not instantly.
- No write-protect / title-space check before writing: the recorder's refusal is reported.

## How to revisit

- **Update libnetmd:** copy the new sources, re-apply the patches in `UPSTREAM.md`.
- **Replace libnetmd:** implement `NetMdBackend` on libusb directly (netmd-js is the reference).
- **Hotplug:** swap `NetMdMonitor`'s timer for libusb hotplug callbacks.
