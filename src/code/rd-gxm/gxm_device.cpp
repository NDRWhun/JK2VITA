/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// gxm_device.cpp -- native GXM device: context, memory, swap chain, frame.

#include "gxm_device.h"
#include "shaders/gxm_shaders.h"

#include <psp2/common_dialog.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// the device knows nothing about the renderer; the owner installs a printer
static gxmLogFn_t gxm_log;

// the clear re-programs the context, so whoever shadows that state is told
static gxmStateResetFn_t gxm_stateReset;

void GXM_SetStateResetCallback( gxmStateResetFn_t fn )
{
	gxm_stateReset = fn;
}

void GXM_SetLogger( gxmLogFn_t fn )
{
	gxm_log = fn;
}

static void GXM_Log( const char *fmt, ... )
{
	if ( !gxm_log ) {
		return;
	}
	char msg[256];
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( msg, sizeof(msg), fmt, ap );
	va_end( ap );
	gxm_log( msg );
}

// context ring, parameter buffer and shader patcher sizes
#define GXM_VDM_RING_SIZE		( 2 * 1024 * 1024 )
#define GXM_VERTEX_RING_SIZE	( 2 * 1024 * 1024 )
#define GXM_FRAGMENT_RING_SIZE	( 2 * 1024 * 1024 )
#define GXM_FRAGMENT_USSE_SIZE	( 64 * 1024 )
#define GXM_PARAM_BUFFER_SIZE	( 16 * 1024 * 1024 )
#define GXM_PATCHER_BUFFER_SIZE	( 1024 * 1024 )
#define GXM_PATCHER_VERT_USSE	( 64 * 1024 )
#define GXM_PATCHER_FRAG_USSE	( 64 * 1024 )

typedef struct {
	void			*data;
	SceGxmSyncObject *sync;
	SceGxmColorSurface surface;
	SceUID			 uid;
} gxmDisplayBuffer_t;

typedef struct {
	void *addr;
} gxmDisplayData_t;

static SceGxmContext		*gxm_context;
static SceUID				 gxm_vdmUid, gxm_vertexUid, gxm_fragmentUid, gxm_fragmentUsseUid;
static void					*gxm_hostMem;

static SceGxmRenderTarget	*gxm_renderTarget;
static gxmDisplayBuffer_t	 gxm_buffers[GXM_DISPLAY_BUFFERS];
static unsigned int			 gxm_backBuffer, gxm_frontBuffer;

static SceUID				 gxm_depthUid;
static void					*gxm_depthData;
static SceGxmDepthStencilSurface gxm_depthSurface;

static SceGxmShaderPatcher	*gxm_patcher;
static SceUID				 gxm_patcherBufUid, gxm_patcherVertUsseUid, gxm_patcherFragUsseUid;

static SceGxmShaderPatcherId gxm_clearVertId, gxm_clearFragId;
static SceGxmVertexProgram	*gxm_clearVertProgram;
static SceGxmFragmentProgram *gxm_clearFragProgram;
static SceGxmFragmentProgram	*gxm_clearDepthProgram;	// colour writes masked off
static const SceGxmProgramParameter *gxm_clearColorParam;
static SceUID				 gxm_clearVertsUid, gxm_clearIndicesUid;
static float				*gxm_clearVerts;
static uint16_t				*gxm_clearIndices;
static float				 gxm_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

static bool				 gxm_deviceOk;
static bool				 gxm_sceneOpen;	// sceGxmEndScene without a matching begin is a data abort

/*
================
GXM_Alloc

CDRAM is the fast pool but only takes 256 KiB granularity.
================
*/
void *GXM_Alloc( SceKernelMemBlockType type, unsigned int size, unsigned int alignment,
				 unsigned int gpuAttrib, SceUID *uid )
{
	if ( type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ) {
		size = ALIGN( size, 256 * 1024 );
	} else {
		size = ALIGN( size, 4 * 1024 );
	}

	const SceUID memUid = sceKernelAllocMemBlock( "gxm", type, size, NULL );
	if ( memUid < 0 ) {
		return NULL;
	}

	void *mem = NULL;
	if ( sceKernelGetMemBlockBase( memUid, &mem ) < 0 ) {
		sceKernelFreeMemBlock( memUid );
		return NULL;
	}
	if ( sceGxmMapMemory( mem, size, (SceGxmMemoryAttribFlags)gpuAttrib ) < 0 ) {
		sceKernelFreeMemBlock( memUid );
		return NULL;
	}

	(void)alignment;	// the block base is already page aligned
	*uid = memUid;
	return mem;
}

void GXM_Free( SceUID uid )
{
	void *mem = NULL;
	if ( uid < 0 || sceKernelGetMemBlockBase( uid, &mem ) < 0 ) {
		return;
	}
	sceGxmUnmapMemory( mem );
	sceKernelFreeMemBlock( uid );
}

void *GXM_AllocVertexUsse( unsigned int size, SceUID *uid, unsigned int *usseOffset )
{
	size = ALIGN( size, 4 * 1024 );

	const SceUID memUid = sceKernelAllocMemBlock( "gxm_vert_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL );
	if ( memUid < 0 ) {
		return NULL;
	}

	void *mem = NULL;
	if ( sceKernelGetMemBlockBase( memUid, &mem ) < 0
		|| sceGxmMapVertexUsseMemory( mem, size, usseOffset ) < 0 ) {
		sceKernelFreeMemBlock( memUid );
		return NULL;
	}

	*uid = memUid;
	return mem;
}

void GXM_FreeVertexUsse( SceUID uid )
{
	void *mem = NULL;
	if ( uid < 0 || sceKernelGetMemBlockBase( uid, &mem ) < 0 ) {
		return;
	}
	sceGxmUnmapVertexUsseMemory( mem );
	sceKernelFreeMemBlock( uid );
}

void *GXM_AllocFragmentUsse( unsigned int size, SceUID *uid, unsigned int *usseOffset )
{
	size = ALIGN( size, 4 * 1024 );

	const SceUID memUid = sceKernelAllocMemBlock( "gxm_frag_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL );
	if ( memUid < 0 ) {
		return NULL;
	}

	void *mem = NULL;
	if ( sceKernelGetMemBlockBase( memUid, &mem ) < 0
		|| sceGxmMapFragmentUsseMemory( mem, size, usseOffset ) < 0 ) {
		sceKernelFreeMemBlock( memUid );
		return NULL;
	}

	*uid = memUid;
	return mem;
}

void GXM_FreeFragmentUsse( SceUID uid )
{
	void *mem = NULL;
	if ( uid < 0 || sceKernelGetMemBlockBase( uid, &mem ) < 0 ) {
		return;
	}
	sceGxmUnmapFragmentUsseMemory( mem );
	sceKernelFreeMemBlock( uid );
}

// shader patcher heap; it wants plain host memory, not GPU visible
static void *GXM_PatcherHostAlloc( void *userData, unsigned int size )
{
	(void)userData;
	return malloc( size );
}

static void GXM_PatcherHostFree( void *userData, void *mem )
{
	(void)userData;
	free( mem );
}

/*
================
GXM_DisplayCallback

Runs on the display queue thread; no libgxm calls are legal here.
================
*/
static void GXM_DisplayCallback( const void *callbackData )
{
	const gxmDisplayData_t *display = (const gxmDisplayData_t *)callbackData;

	SceDisplayFrameBuf fb;
	memset( &fb, 0, sizeof(fb) );
	fb.size        = sizeof(fb);
	fb.base        = display->addr;
	fb.pitch       = GXM_DISPLAY_STRIDE;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width       = GXM_DISPLAY_WIDTH;
	fb.height      = GXM_DISPLAY_HEIGHT;

	sceDisplaySetFrameBuf( &fb, SCE_DISPLAY_SETBUF_NEXTFRAME );
	sceDisplayWaitSetFrameBuf();
}

static bool GXM_InitContext( void )
{
	SceGxmInitializeParams initParams;
	memset( &initParams, 0, sizeof(initParams) );
	initParams.displayQueueMaxPendingCount  = GXM_DISPLAY_BUFFERS - 1;
	initParams.displayQueueCallback         = GXM_DisplayCallback;
	initParams.displayQueueCallbackDataSize = sizeof(gxmDisplayData_t);
	initParams.parameterBufferSize          = GXM_PARAM_BUFFER_SIZE;

	if ( sceGxmInitialize( &initParams ) < 0 ) {
		return false;
	}

	SceGxmContextParams ctxParams;
	memset( &ctxParams, 0, sizeof(ctxParams) );

	gxm_hostMem = malloc( SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE );
	ctxParams.hostMem     = gxm_hostMem;
	ctxParams.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;

	ctxParams.vdmRingBufferMem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		GXM_VDM_RING_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &gxm_vdmUid );
	ctxParams.vdmRingBufferMemSize = GXM_VDM_RING_SIZE;

	ctxParams.vertexRingBufferMem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		GXM_VERTEX_RING_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &gxm_vertexUid );
	ctxParams.vertexRingBufferMemSize = GXM_VERTEX_RING_SIZE;

	ctxParams.fragmentRingBufferMem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		GXM_FRAGMENT_RING_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ, &gxm_fragmentUid );
	ctxParams.fragmentRingBufferMemSize = GXM_FRAGMENT_RING_SIZE;

	unsigned int fragmentUsseOffset = 0;
	ctxParams.fragmentUsseRingBufferMem = GXM_AllocFragmentUsse( GXM_FRAGMENT_USSE_SIZE,
		&gxm_fragmentUsseUid, &fragmentUsseOffset );
	ctxParams.fragmentUsseRingBufferMemSize = GXM_FRAGMENT_USSE_SIZE;
	ctxParams.fragmentUsseRingBufferOffset  = fragmentUsseOffset;

	if ( !ctxParams.vdmRingBufferMem || !ctxParams.vertexRingBufferMem
		|| !ctxParams.fragmentRingBufferMem || !ctxParams.fragmentUsseRingBufferMem ) {
		return false;
	}

	return (bool)( sceGxmCreateContext( &ctxParams, &gxm_context ) >= 0 );
}

static bool GXM_InitSwapChain( void )
{
	SceGxmRenderTargetParams rtParams;
	memset( &rtParams, 0, sizeof(rtParams) );
	rtParams.width          = GXM_DISPLAY_WIDTH;
	rtParams.height         = GXM_DISPLAY_HEIGHT;
	rtParams.scenesPerFrame = 1;
	rtParams.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
	rtParams.driverMemBlock = -1;

	if ( sceGxmCreateRenderTarget( &rtParams, &gxm_renderTarget ) < 0 ) {
		return false;
	}

	for ( int i = 0; i < GXM_DISPLAY_BUFFERS; i++ ) {
		gxm_buffers[i].data = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
			4 * GXM_DISPLAY_STRIDE * GXM_DISPLAY_HEIGHT, SCE_GXM_COLOR_SURFACE_ALIGNMENT,
			SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &gxm_buffers[i].uid );
		if ( !gxm_buffers[i].data ) {
			return false;
		}

		if ( sceGxmColorSurfaceInit( &gxm_buffers[i].surface,
				SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
				SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
				GXM_DISPLAY_WIDTH, GXM_DISPLAY_HEIGHT, GXM_DISPLAY_STRIDE,
				gxm_buffers[i].data ) < 0 ) {
			return false;
		}
		if ( sceGxmSyncObjectCreate( &gxm_buffers[i].sync ) < 0 ) {
			return false;
		}
	}

	// depth is tile-local during normal rendering; the allocation exists so a
	// partial render has somewhere to spill
	const unsigned int alignedW = ALIGN( GXM_DISPLAY_WIDTH, SCE_GXM_TILE_SIZEX );
	const unsigned int alignedH = ALIGN( GXM_DISPLAY_HEIGHT, SCE_GXM_TILE_SIZEY );
	void *depthData = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		4 * alignedW * alignedH, SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT,
		SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &gxm_depthUid );
	gxm_depthData = depthData;
	if ( !depthData ) {
		return false;
	}

	return (bool)( sceGxmDepthStencilSurfaceInit( &gxm_depthSurface,
		SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,
		alignedW, depthData, NULL ) >= 0 );
}

static bool GXM_InitPatcher( void )
{
	SceGxmShaderPatcherParams params;
	memset( &params, 0, sizeof(params) );
	params.hostAllocCallback = &GXM_PatcherHostAlloc;
	params.hostFreeCallback  = &GXM_PatcherHostFree;

	params.bufferMem = GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		GXM_PATCHER_BUFFER_SIZE, 4, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE,
		&gxm_patcherBufUid );
	params.bufferMemSize = GXM_PATCHER_BUFFER_SIZE;

	unsigned int vertUsseOffset = 0, fragUsseOffset = 0;
	params.vertexUsseMem = GXM_AllocVertexUsse( GXM_PATCHER_VERT_USSE, &gxm_patcherVertUsseUid, &vertUsseOffset );
	params.vertexUsseMemSize = GXM_PATCHER_VERT_USSE;
	params.vertexUsseOffset  = vertUsseOffset;

	params.fragmentUsseMem = GXM_AllocFragmentUsse( GXM_PATCHER_FRAG_USSE, &gxm_patcherFragUsseUid, &fragUsseOffset );
	params.fragmentUsseMemSize = GXM_PATCHER_FRAG_USSE;
	params.fragmentUsseOffset  = fragUsseOffset;

	if ( !params.bufferMem || !params.vertexUsseMem || !params.fragmentUsseMem ) {
		return false;
	}

	return (bool)( sceGxmShaderPatcherCreate( &params, &gxm_patcher ) >= 0 );
}

/*
================
GXM_InitClear

Builds the fullscreen triangle and its two fragment-program instances.
================
*/
static bool GXM_InitClear( void )
{
	const SceGxmProgram *vert = (const SceGxmProgram *)gxs_clear_v;
	const SceGxmProgram *frag = (const SceGxmProgram *)gxs_clear_f;

	if ( sceGxmShaderPatcherRegisterProgram( gxm_patcher, vert, &gxm_clearVertId ) < 0
		|| sceGxmShaderPatcherRegisterProgram( gxm_patcher, frag, &gxm_clearFragId ) < 0 ) {
		return false;
	}

	const SceGxmProgramParameter *posParam = sceGxmProgramFindParameterByName( vert, "aPosition" );
	if ( !posParam ) {
		return false;
	}

	SceGxmVertexAttribute attr;
	memset( &attr, 0, sizeof(attr) );
	attr.streamIndex    = 0;
	attr.offset         = 0;
	attr.format         = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	attr.componentCount = 2;
	attr.regIndex       = sceGxmProgramParameterGetResourceIndex( posParam );

	SceGxmVertexStream stream;
	memset( &stream, 0, sizeof(stream) );
	stream.stride      = sizeof(float) * 2;
	stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

	if ( sceGxmShaderPatcherCreateVertexProgram( gxm_patcher, gxm_clearVertId,
			&attr, 1, &stream, 1, &gxm_clearVertProgram ) < 0 ) {
		return false;
	}

	if ( sceGxmShaderPatcherCreateFragmentProgram( gxm_patcher, gxm_clearFragId,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			NULL, vert, &gxm_clearFragProgram ) < 0 ) {
		return false;
	}

	// the same program with colour writes masked off, for a depth-only clear
	SceGxmBlendInfo noColor;
	memset( &noColor, 0, sizeof(noColor) );
	noColor.colorMask = SCE_GXM_COLOR_MASK_NONE;
	noColor.colorFunc = SCE_GXM_BLEND_FUNC_NONE;
	noColor.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
	if ( sceGxmShaderPatcherCreateFragmentProgram( gxm_patcher, gxm_clearFragId,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
			&noColor, vert, &gxm_clearDepthProgram ) < 0 ) {
		return false;
	}

	gxm_clearColorParam = sceGxmProgramFindParameterByName( frag, "uClearColor" );

	gxm_clearVerts = (float *)GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		3 * 2 * sizeof(float), 4, SCE_GXM_MEMORY_ATTRIB_READ, &gxm_clearVertsUid );
	gxm_clearIndices = (uint16_t *)GXM_Alloc( SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
		3 * sizeof(uint16_t), 4, SCE_GXM_MEMORY_ATTRIB_READ, &gxm_clearIndicesUid );
	if ( !gxm_clearVerts || !gxm_clearIndices ) {
		return false;
	}

	gxm_clearVerts[0] = -1.0f;	gxm_clearVerts[1] = -1.0f;
	gxm_clearVerts[2] =  3.0f;	gxm_clearVerts[3] = -1.0f;
	gxm_clearVerts[4] = -1.0f;	gxm_clearVerts[5] =  3.0f;
	gxm_clearIndices[0] = 0; gxm_clearIndices[1] = 1; gxm_clearIndices[2] = 2;

	return true;
}

bool GXM_DeviceInit( void )
{
	if ( gxm_deviceOk ) {
		return true;
	}

	if ( !GXM_InitContext() || !GXM_InitSwapChain() || !GXM_InitPatcher() || !GXM_InitClear() ) {
		GXM_Log( "GXM_DeviceInit failed" );
		return false;
	}

	gxm_backBuffer  = 0;
	gxm_frontBuffer = 0;
	gxm_deviceOk    = true;

	// the renderer expects a scene to already be open; the first Present would
	// otherwise end one that was never begun
	GXM_BeginFrame();

	GXM_Log( "GXM device: %dx%d, %d buffers, %d MiB parameter buffer",
		GXM_DISPLAY_WIDTH, GXM_DISPLAY_HEIGHT, GXM_DISPLAY_BUFFERS,
		GXM_PARAM_BUFFER_SIZE / (1024 * 1024) );
	return true;
}

void GXM_SetClearColor( float r, float g, float b, float a )
{
	gxm_clearColor[0] = r; gxm_clearColor[1] = g;
	gxm_clearColor[2] = b; gxm_clearColor[3] = a;
}

void GXM_BeginFrame( void )
{
	if ( !gxm_deviceOk || gxm_sceneOpen ) {
		return;
	}
	gxm_sceneOpen = true;

	sceGxmBeginScene( gxm_context, 0, gxm_renderTarget,
		NULL, NULL, gxm_buffers[gxm_backBuffer].sync,
		&gxm_buffers[gxm_backBuffer].surface, &gxm_depthSurface );

	GXM_ClearBuffers( 1, 1 );
}

/*
================
GXM_ClearBuffers

GXM has no clear, so a clear is a fullscreen triangle at z=1.
================
*/
void GXM_ClearBuffers( int color, int depth )
{
	if ( !gxm_deviceOk || !gxm_sceneOpen || ( !color && !depth ) ) {
		return;
	}

	// GXM state survives scenes, so the clear sets everything it depends on
	sceGxmSetFrontDepthFunc( gxm_context, SCE_GXM_DEPTH_FUNC_ALWAYS );
	sceGxmSetBackDepthFunc( gxm_context, SCE_GXM_DEPTH_FUNC_ALWAYS );
	sceGxmSetFrontDepthWriteEnable( gxm_context,
		depth ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED );
	sceGxmSetBackDepthWriteEnable( gxm_context,
		depth ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED );
	sceGxmSetCullMode( gxm_context, SCE_GXM_CULL_NONE );
	sceGxmSetFrontPolygonMode( gxm_context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL );
	sceGxmSetBackPolygonMode( gxm_context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL );

	sceGxmSetVertexProgram( gxm_context, gxm_clearVertProgram );
	sceGxmSetFragmentProgram( gxm_context,
		color ? gxm_clearFragProgram : gxm_clearDepthProgram );

	if ( color && gxm_clearColorParam ) {
		void *uniforms = NULL;
		sceGxmReserveFragmentDefaultUniformBuffer( gxm_context, &uniforms );
		if ( uniforms ) {
			sceGxmSetUniformDataF( uniforms, gxm_clearColorParam, 0, 4, gxm_clearColor );
		}
	}

	sceGxmSetVertexStream( gxm_context, 0, gxm_clearVerts );
	sceGxmDraw( gxm_context, SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, gxm_clearIndices, 3 );

	if ( gxm_stateReset ) {
		gxm_stateReset();
	}
}

void GXM_EndFrame( void )
{
	if ( !gxm_deviceOk || !gxm_sceneOpen ) {
		return;
	}
	gxm_sceneOpen = false;

	sceGxmEndScene( gxm_context, NULL, NULL );

	// hand the back buffer over after the scene ends, so the console IME composites into this flip
	{
		SceCommonDialogUpdateParam dlg;
		memset( &dlg, 0, sizeof(dlg) );
		dlg.renderTarget.colorFormat      = SCE_GXM_COLOR_FORMAT_A8B8G8R8;
		dlg.renderTarget.surfaceType      = SCE_GXM_COLOR_SURFACE_LINEAR;
		dlg.renderTarget.width            = GXM_DISPLAY_WIDTH;
		dlg.renderTarget.height           = GXM_DISPLAY_HEIGHT;
		dlg.renderTarget.strideInPixels   = GXM_DISPLAY_STRIDE;
		dlg.renderTarget.colorSurfaceData = gxm_buffers[gxm_backBuffer].data;
		dlg.renderTarget.depthSurfaceData = gxm_depthData;
		dlg.displaySyncObject             = gxm_buffers[gxm_backBuffer].sync;
		sceCommonDialogUpdate( &dlg );
	}
	sceGxmPadHeartbeat( &gxm_buffers[gxm_backBuffer].surface, gxm_buffers[gxm_backBuffer].sync );

	gxmDisplayData_t display;
	display.addr = gxm_buffers[gxm_backBuffer].data;

	sceGxmDisplayQueueAddEntry( gxm_buffers[gxm_frontBuffer].sync,
		gxm_buffers[gxm_backBuffer].sync, &display );

	gxm_frontBuffer = gxm_backBuffer;
	gxm_backBuffer  = ( gxm_backBuffer + 1 ) % GXM_DISPLAY_BUFFERS;
}

/*
================
GXM_Sync

Blocks until the GPU has finished everything submitted so far.
================
*/
void GXM_Sync( void )
{
	if ( !gxm_deviceOk ) {
		return;
	}
	sceGxmFinish( gxm_context );
}

/*
================
GXM_ReadPixels

Copies a rectangle of the last presented frame, bottom row first; comps is 3 or 4.
================
*/
void GXM_ReadPixels( int x, int y, int width, int height, int comps, int dstStride, void *dst )
{
	if ( !gxm_deviceOk || !dst || width <= 0 || height <= 0 || ( comps != 3 && comps != 4 ) ) {
		return;
	}

	// the front buffer holds a finished image only once its flip has retired
	sceGxmDisplayQueueFinish();

	const unsigned char *src = (const unsigned char *)gxm_buffers[gxm_frontBuffer].data;
	unsigned char *out = (unsigned char *)dst;

	for ( int row = 0; row < height; row++ ) {
		unsigned char *d = out + (size_t)row * (size_t)dstStride;
		const int sy = GXM_DISPLAY_HEIGHT - 1 - ( y + row );

		if ( sy < 0 || sy >= GXM_DISPLAY_HEIGHT ) {
			memset( d, 0, (size_t)width * comps );
			continue;
		}

		for ( int col = 0; col < width; col++ ) {
			const int      sx = x + col;
			unsigned char *p  = d + col * comps;

			if ( comps == 4 ) {
				p[3] = 255;
			}
			if ( sx < 0 || sx >= GXM_DISPLAY_WIDTH ) {
				p[0] = p[1] = p[2] = 0;
				continue;
			}
			const unsigned char *s = src + ( (size_t)sy * GXM_DISPLAY_STRIDE + (size_t)sx ) * 4;
			p[0] = s[0];
			p[1] = s[1];
			p[2] = s[2];
		}
	}
}

void GXM_DeviceShutdown( void )
{
	if ( !gxm_deviceOk ) {
		return;
	}

	if ( gxm_sceneOpen ) {
		sceGxmEndScene( gxm_context, NULL, NULL );
		gxm_sceneOpen = false;
	}
	sceGxmFinish( gxm_context );
	sceGxmDisplayQueueFinish();

	sceGxmShaderPatcherReleaseVertexProgram( gxm_patcher, gxm_clearVertProgram );
	sceGxmShaderPatcherReleaseFragmentProgram( gxm_patcher, gxm_clearFragProgram );
	sceGxmShaderPatcherUnregisterProgram( gxm_patcher, gxm_clearVertId );
	sceGxmShaderPatcherUnregisterProgram( gxm_patcher, gxm_clearFragId );
	sceGxmShaderPatcherDestroy( gxm_patcher );

	GXM_Free( gxm_clearVertsUid );
	GXM_Free( gxm_clearIndicesUid );
	GXM_Free( gxm_patcherBufUid );
	GXM_FreeVertexUsse( gxm_patcherVertUsseUid );
	GXM_FreeFragmentUsse( gxm_patcherFragUsseUid );

	for ( int i = 0; i < GXM_DISPLAY_BUFFERS; i++ ) {
		GXM_Free( gxm_buffers[i].uid );
		sceGxmSyncObjectDestroy( gxm_buffers[i].sync );
	}
	GXM_Free( gxm_depthUid );
	sceGxmDestroyRenderTarget( gxm_renderTarget );

	sceGxmDestroyContext( gxm_context );
	GXM_Free( gxm_vdmUid );
	GXM_Free( gxm_vertexUid );
	GXM_Free( gxm_fragmentUid );
	GXM_FreeFragmentUsse( gxm_fragmentUsseUid );
	free( gxm_hostMem );

	sceGxmTerminate();
	gxm_deviceOk = false;
}

SceGxmContext *GXM_Context( void )
{
	return gxm_context;
}

SceGxmShaderPatcher *GXM_ShaderPatcher( void )
{
	return gxm_patcher;
}
