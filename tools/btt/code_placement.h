#pragma once
// Match Teensy 4's FLASHMEM convention: its linker maps .flashmem* to XIP flash.
// Keep selected foreground operations out of ITCM callers even when inlining is
// otherwise profitable. This is placement, not a timing/admission guarantee.
// Host builds retain their ordinary compiler behavior and need no Arduino headers.
// Stable per-operation section names also separate template COMDAT sections from
// concrete functions; GCC cannot mix both in one explicitly named section.
#if defined(__IMXRT1062__) && defined(__GNUC__)
#define BTT_FLASHMEM(name) __attribute__((section(".flashmem.btt." name), noinline))
#else
#define BTT_FLASHMEM(name)
#endif
