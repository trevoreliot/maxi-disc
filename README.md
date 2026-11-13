# music-md-manager

A Linux desktop music library manager: browsing, playback, tag editing, artwork,
USB-device sync and Sony NetMD MiniDisc upload. C++20 / Qt 6 Qt Quick.

Status: early development. See [docs/PLAN.md](docs/PLAN.md).

## Build

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler, Qt ≥ 6.8 (Base, Declarative).
ccache is used if it's installed.

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
./build/debug/src/app/music-md-manager
```

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).
