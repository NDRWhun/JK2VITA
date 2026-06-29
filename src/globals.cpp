//============================================================================
// JK2VITA — static-build glue translation unit.
//
// JK2 SP is built here as a SINGLE statically-linked executable (the Vita has
// no dlopen, and SP has no QVM). On desktop, OpenJK builds three separate
// binaries — engine (openjo_sp), renderer (rdjosp-vanilla) and game
// (jospgame) — and each may carry its own definition of certain globals.
// Linked together into one ELF, those become "multiple definition" errors.
//
// This file is the single, intentional home for such globals: as Phase 2
// link errors surface a redundant definition, the duplicates are removed from
// their original TUs (left as `extern`) and the one true definition lives here.
// We never `#if 0` real engine/game code to dodge a collision.
//
// Kept deliberately minimal; grown only in response to real link diagnostics.
//============================================================================

#ifdef VITA
// VitaSDK newlib reads this symbol to size the program heap. JK2 SP has no fixed
// hunk/zone block — Hunk_Alloc/Z_Malloc are thin malloc wrappers, so this heap IS
// the engine's entire malloc budget. With ATTRIBUTE2=12 the USER/MAIN partition is
// ~365 MiB; the heap is reserved BEFORE vglInit, so every MiB here is one MiB LESS
// for vitaGL's texture RAM pool. The engine's real malloc peak (sound pool 25 MiB +
// BSP + models + transient image buffers) sits ~100-120 MiB, so 144 MiB keeps a
// safe margin while handing ~48 MiB more to vitaGL (RAM pool ~100 -> ~148 MiB),
// giving texture-heavy scenes more spill room before the GPU pools thrash.
unsigned int _newlib_heap_size_user = 144 * 1024 * 1024;

// VitaSDK reads this to size the main thread's stack. The default (~256 KB) is
// too small for rd-vanilla: R_SubdividePatchToGrid alone uses a ~332 KB stack
// frame (large local drawVert_t grids) during BSP load and overflows it, faulting
// with a data abort. 4 MB is plenty of headroom for the engine's deep call paths.
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
#endif

