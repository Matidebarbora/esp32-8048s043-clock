#pragma once
// Force-included (via platformio.ini's `-include`) into every translation
// unit in the build, so lv_mem.c sees LV_MEM_POOL_ALLOC defined without it
// ever needing to appear as a raw -D on the compiler command line — a
// function-like macro's parentheses/commas there get misparsed as shell
// syntax on some hosts once PlatformIO/SCons's command line is long enough
// to route through `sh -c "<whole command as one string>"` (seen on macOS;
// harmless when the same command is exec'd as an argv array instead).
//
// build_flags apply to EVERY compile PlatformIO drives for this project,
// including the second-stage bootloader's own separate build — which has
// no esp_heap_caps.h on its include path (different component set) and
// doesn't need this macro at all. __has_include guards the whole thing so
// it's a no-op there instead of a hard "file not found" build failure.
#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#endif
