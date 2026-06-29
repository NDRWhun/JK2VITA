// CAS-claimed ring + counting sema. See vita_jobpool.h.
// Two workers pinned to cores 1 and 2. Main thread also steals in WaitGroup
// so the barrier doesn't leave its core idle.

#include "vita_jobpool.h"

#include <stdint.h>
#include <string.h>

#ifdef VITA

#include <psp2/kernel/threadmgr.h>

#define RING_SIZE	8192
#define RING_MASK	(RING_SIZE - 1)
#define NUM_WORKERS	2

typedef struct {
	jobFn_t	fn;
	void	*arg;
	int		group;
} poolJob_t;

static poolJob_t	s_ring[RING_SIZE];
static uint32_t		s_head  = 0;	// next free slot; producer store-releases to publish
static uint32_t		s_claim = 0;	// next index to run; advanced by CAS (workers + main)
static uint32_t		s_groupEnq[JOBGROUP_COUNT];	// main-only
static uint32_t		s_groupDone[JOBGROUP_COUNT];	// atomic (workers + stealing main)
static SceUID		s_sema = -1;
static SceUID		s_threads[NUM_WORKERS];
static int			s_shutdown = 0;
static int			s_up = 0;

static inline void cpu_relax( void ) { __asm__ __volatile__( "yield" ::: "memory" ); }

// Claim and run one job. Returns 0 only when the queue is empty (caller may sleep);
// 1 if it ran one or lost the CAS — caller retries either way.
static int pool_try_run( void )
{
	uint32_t c = __atomic_load_n( &s_claim, __ATOMIC_RELAXED );
	uint32_t h = __atomic_load_n( &s_head,  __ATOMIC_ACQUIRE );
	if ( (int32_t)( h - c ) <= 0 ) {
		return 0;	// empty — signed delta is wrap-safe
	}
	if ( __atomic_compare_exchange_n( &s_claim, &c, c + 1, 0 /*strong*/,
									  __ATOMIC_ACQ_REL, __ATOMIC_RELAXED ) ) {
		poolJob_t *j = &s_ring[c & RING_MASK];
		j->fn( j->arg );
		__atomic_add_fetch( &s_groupDone[j->group], 1, __ATOMIC_ACQ_REL );
	}
	return 1;
}

static int worker_entry( SceSize argSize, void *argp )
{
	(void)argSize; (void)argp;
	while ( !__atomic_load_n( &s_shutdown, __ATOMIC_ACQUIRE ) ) {
		if ( !pool_try_run() ) {
			sceKernelWaitSema( s_sema, 1, NULL );	// sleep until the next batch is signalled
		}
	}
	return 0;
}

extern "C" void JobPool_Init( void )
{
	if ( s_up ) return;
	s_head = s_claim = 0;
	memset( s_groupEnq, 0, sizeof( s_groupEnq ) );
	memset( s_groupDone, 0, sizeof( s_groupDone ) );
	s_shutdown = 0;
	s_sema = sceKernelCreateSema( "jobpool_sema", 0, 0, RING_SIZE, NULL );
	if ( s_sema < 0 ) return;
	static const int affin[NUM_WORKERS] = { SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2 };
	int created = 0;
	for ( int i = 0; i < NUM_WORKERS; i++ ) {
		s_threads[i] = sceKernelCreateThread( "jobpool_worker", worker_entry,
											  0x10000100, 256 * 1024, 0, affin[i], NULL );
		if ( s_threads[i] >= 0 ) {
			sceKernelStartThread( s_threads[i], 0, NULL );
			created++;
		}
	}
	s_up = ( created > 0 );
}

extern "C" void JobPool_Shutdown( void )
{
	if ( !s_up ) return;
	__atomic_store_n( &s_shutdown, 1, __ATOMIC_RELEASE );
	sceKernelSignalSema( s_sema, NUM_WORKERS );	// wake all so they observe shutdown
	for ( int i = 0; i < NUM_WORKERS; i++ ) {
		if ( s_threads[i] >= 0 ) {
			sceKernelWaitThreadEnd( s_threads[i], NULL, NULL );
			sceKernelDeleteThread( s_threads[i] );
		}
	}
	sceKernelDeleteSema( s_sema );
	s_up = 0;
}

extern "C" int JobPool_Available( void ) { return s_up; }

extern "C" void JobPool_ResetGroup( int group )
{
	s_groupEnq[group] = 0;
	__atomic_store_n( &s_groupDone[group], 0, __ATOMIC_RELEASE );
}

extern "C" void JobPool_Enqueue( int group, jobFn_t fn, void *arg )
{
	uint32_t h = s_head;	// single producer (main thread)
	s_ring[h & RING_MASK].fn    = fn;
	s_ring[h & RING_MASK].arg   = arg;
	s_ring[h & RING_MASK].group = group;
	s_groupEnq[group]++;
	__atomic_store_n( &s_head, h + 1, __ATOMIC_RELEASE );	// publish (workers acquire-load head)
}

extern "C" void JobPool_Signal( int nPushed )
{
	if ( nPushed > 0 ) sceKernelSignalSema( s_sema, nPushed );
}

extern "C" void JobPool_WaitGroup( int group )
{
	while ( __atomic_load_n( &s_groupDone[group], __ATOMIC_ACQUIRE ) < s_groupEnq[group] ) {
		if ( !pool_try_run() ) {
			cpu_relax();	// queue drained; workers are finishing the last in-flight jobs
		}
	}
}

extern "C" int JobPool_GroupPending( int group )
{
	int32_t d = (int32_t)( s_groupEnq[group] - __atomic_load_n( &s_groupDone[group], __ATOMIC_ACQUIRE ) );
	return d > 0 ? d : 0;
}

extern "C" void JobPool_DrainGroup( int group ) { JobPool_WaitGroup( group ); }

#else // !VITA — serial fallback so it still builds off-device

extern "C" void JobPool_Init( void ) {}
extern "C" void JobPool_Shutdown( void ) {}
extern "C" int  JobPool_Available( void ) { return 0; }
extern "C" void JobPool_ResetGroup( int group ) { (void)group; }
extern "C" void JobPool_Enqueue( int group, jobFn_t fn, void *arg ) { (void)group; fn( arg ); }
extern "C" void JobPool_Signal( int n ) { (void)n; }
extern "C" void JobPool_WaitGroup( int group ) { (void)group; }
extern "C" int  JobPool_GroupPending( int group ) { (void)group; return 0; }
extern "C" void JobPool_DrainGroup( int group ) { (void)group; }

#endif
