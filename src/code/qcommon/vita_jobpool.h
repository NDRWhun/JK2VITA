// Worker pool for the Vita's two idle cores.
//
// Shared by the renderer (Ghoul2 skinning, barriered each frame) and the loader
// (texture/sound decode + pk3 inflate). Two threads pinned to cores 1 and 2; the main
// thread steals too so a barrier never parks a core. CAS claim + counting semaphore.
//
// A job fn may only do: malloc/free, NEON/scalar math, memcpy, read immutable input.
// No qgl*/sceGxm*/vitaGL, no FS, no va(), no Com_Error, no Z_Malloc/R_Malloc/Hunk.
// Touch the GL context or the zone before enqueue or after the barrier, never inside.
//
// Off Vita everything is a no-op; the caller falls back to inline, gated on
// JobPool_Available().

#ifndef VITA_JOBPOOL_H
#define VITA_JOBPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*jobFn_t)( void *arg );

// SKIN is barriered every frame; DECODE/INFLATE accumulate and get polled.
enum {
	JOBGROUP_SKIN = 0,
	JOBGROUP_DECODE,
	JOBGROUP_INFLATE,
	JOBGROUP_COUNT
};

void JobPool_Init( void );			// spawn the workers once; they idle until used
void JobPool_Shutdown( void );
int  JobPool_Available( void );		// 1 if the pool is up

// Producer, main thread only: reset the group, enqueue jobs, signal once.
void JobPool_ResetGroup( int group );
void JobPool_Enqueue( int group, jobFn_t fn, void *arg );
void JobPool_Signal( int nPushed );

// Barrier (main thread): steal and run jobs inline until the group is done.
void JobPool_WaitGroup( int group );
// Load-screen helpers: count remaining, or block-drain while stealing.
int  JobPool_GroupPending( int group );
void JobPool_DrainGroup( int group );

#ifdef __cplusplus
}
#endif

#endif // VITA_JOBPOOL_H
