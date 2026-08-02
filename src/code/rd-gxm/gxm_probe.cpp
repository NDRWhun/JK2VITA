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

// gxm_probe.cpp -- standalone hardware check for the native GXM device.
// sceGxmInitialize is process-global and one-shot, so this runs on its own.

#include "gxm_device.h"
#include "gxm_texture.h"

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#define PROBE_DIR	"ux0:data/JK2VITA"
#define PROBE_LOG	PROBE_DIR "/gxm_probe.log"

static SceUID probe_log = -1;

static void Probe_Write( const char *msg )
{
	if ( probe_log < 0 ) {
		return;
	}
	sceIoWrite( probe_log, msg, strlen( msg ) );
	sceIoWrite( probe_log, "\n", 1 );
}

int main( void )
{
	sceIoMkdir( PROBE_DIR, 0777 );
	probe_log = sceIoOpen( PROBE_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777 );

	GXM_SetLogger( Probe_Write );
	Probe_Write( "gxm_probe start" );

	if ( !GXM_DeviceInit() ) {
		Probe_Write( "RESULT: device init FAILED" );
		sceIoClose( probe_log );
		sceKernelExitProcess( 1 );
		return 1;
	}

	if ( !GXM_RingInit( 256 * 1024 ) || !GXM_TestSceneInit() ) {
		Probe_Write( "RESULT: ring or test scene FAILED" );
		sceIoClose( probe_log );
		sceKernelExitProcess( 1 );
		return 1;
	}

	Probe_Write( "RESULT: device + texture + ring OK - press START to exit" );

	SceCtrlData pad;
	unsigned int frame = 0;
	for ( ;; ) {
		memset( &pad, 0, sizeof(pad) );
		sceCtrlPeekBufferPositive( 0, &pad, 1 );
		if ( pad.buttons & SCE_CTRL_START ) {
			break;
		}

		// a visibly animating colour proves frames are actually reaching the display
		const float t = (float)( frame % 180 ) / 180.0f;
		GXM_SetClearColor( t, 1.0f - t, 0.25f, 1.0f );

		GXM_BeginFrame();
		GXM_DrawTestQuad( (float)frame );
		GXM_EndFrame();
		frame++;

		// report periodically; closing from the LiveArea skips the exit path
		if ( ( frame % 300 ) == 0 ) {
			char tick[128];
			snprintf( tick, sizeof(tick), "  %u frames, ring %u bytes/frame",
				frame, GXM_RingUsedLastFrame() );
			Probe_Write( tick );
		}
	}

	char msg[128];
	snprintf( msg, sizeof(msg), "RESULT: %u frames presented, ring peak %u bytes/frame, clean exit",
		frame, GXM_RingUsedLastFrame() );
	Probe_Write( msg );

	GXM_TestSceneShutdown();
	GXM_RingShutdown();
	GXM_DeviceShutdown();
	sceIoClose( probe_log );
	sceKernelExitProcess( 0 );
	return 0;
}
