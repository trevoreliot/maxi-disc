// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built-in sanitizer defaults, linked into every executable when MMM_SANITIZE=ON.
// These apply when the binary is run directly (not just under ctest). Options set
// in ASAN_OPTIONS / LSAN_OPTIONS at runtime still override them.

#define MMM_SANITIZER_EXPORT extern "C" __attribute__((visibility("default"), used))

// Full (slow) unwinding on malloc, so leak stacks reach past driver code that was
// built without frame pointers. Without it LSan suppressions can't match the
// third-party leaks below. Costs ~1 s at startup in debug builds.
MMM_SANITIZER_EXPORT const char *__asan_default_options()
{
    return "fast_unwind_on_malloc=0:detect_leaks=1:print_suppressions=0";
}

// Verified third-party leaks only; each entry needs a comment saying where it was seen.
MMM_SANITIZER_EXPORT const char *__lsan_default_suppressions()
{
    return
        // NVIDIA proprietary driver: allocations in EGL/GBM init during
        // QWaylandEglClientBufferIntegration::initialize and QEGLPlatformContext (M0, driver 620.70).
        "leak:libEGL_nvidia.so\n"
        "leak:libnvidia-\n"
        "leak:libgbm.so\n"
        // libdbus: message loader buffers allocated from the NVIDIA EGL context setup (M0).
        "leak:libdbus-1.so\n";
}
