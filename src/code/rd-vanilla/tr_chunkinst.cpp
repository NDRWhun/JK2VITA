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

// Instanced chunk renderer (r_chunkInstancing). A scripted blow-up (e.g.
// artus_mine generator_explode) flings 100+ debris md3 fragments at once, each
// its own draw call. This collapses every opaque, unfogged chunk sharing a
// model+shader into ONE glDrawElementsInstanced, with per-instance transform
// and per-instance diffuse lighting computed in the shader - reproducing
// RB_CalcDiffuseColor exactly. Faded (translucent) and fogged chunks fall back
// to the normal per-entity path. Gated; off unless r_chunkInstancing is set.

#ifdef VITA

#include "../server/exe_headers.h"
#include "tr_local.h"

// glVertexAttribDivisor / glDrawElementsInstanced come from <vitaGL.h> via qgl.h.
extern void myGlMultMatrix( const float *a, const float *b, float *out );

#define CI_MAX_BUCKETS	8			// distinct (model,shader) groups per view
#define CI_BUCKET_CAP	256			// instances per bucket; overflow -> normal path
#define CI_GEOM_SLOTS	64			// persistent decoded-geometry cache

// per-instance stream: mat4 model->world (16) + ambient(4) + directed(4) + lightDir(4)
#define CI_INST_FLOATS	28
#define CI_INST_STRIDE	(CI_INST_FLOATS * (int)sizeof(float))

typedef struct {
	md3Surface_t	*surf;			// key; NULL == empty
	int				numVerts;
	int				numIndexes;
	float			*geo;			// interleaved pos3 nrm3 st2 (8 floats/vert)
	unsigned short	*idx;
} ciGeom_t;

typedef struct {
	ciGeom_t		*geom;
	shader_t		*shader;
	int				count;
	float			inst[CI_BUCKET_CAP * CI_INST_FLOATS];
} ciBucket_t;

static ciGeom_t		ci_geom[CI_GEOM_SLOTS];
static ciBucket_t	ci_buckets[CI_MAX_BUCKETS];
static int			ci_numBuckets = 0;

static GLuint		ci_prog = 0;
static qboolean		ci_failed = qfalse;
static GLint		ci_uMVP = -1;
static GLint		ci_uTex = -1;

static const char *CI_VS =
	"uniform mat4 uMVP;\n"
	"attribute vec3 aPos;\n"
	"attribute vec3 aNormal;\n"
	"attribute vec2 aUv;\n"
	"attribute vec4 aI0;\n"
	"attribute vec4 aI1;\n"
	"attribute vec4 aI2;\n"
	"attribute vec4 aI3;\n"
	"attribute vec4 aAmb;\n"
	"attribute vec4 aDir;\n"
	"attribute vec4 aLdir;\n"
	"varying vec2 vUv;\n"
	"varying vec4 vCol;\n"
	"void main() {\n"
	"  mat4 m = mat4(aI0, aI1, aI2, aI3);\n"
	"  float d = max(dot(aNormal, aLdir.xyz), 0.0);\n"
	"  vCol = vec4((aAmb.xyz + d * aDir.xyz) * 0.0039215686, 1.0);\n"
	"  vUv = aUv;\n"
	"  gl_Position = uMVP * (m * vec4(aPos, 1.0));\n"
	"}\n";

static const char *CI_FS =
	"uniform sampler2D uTex;\n"
	"varying vec2 vUv;\n"
	"varying vec4 vCol;\n"
	"void main() { gl_FragColor = vCol * texture2D(uTex, vUv); }\n";

static GLuint CI_Compile( GLenum type, const char *src )
{
	GLuint	sh = glCreateShader( type );
	GLint	ok = 0;
	glShaderSource( sh, 1, &src, NULL );
	glCompileShader( sh );
	glGetShaderiv( sh, GL_COMPILE_STATUS, &ok );
	if ( !ok ) {
		glDeleteShader( sh );
		return 0;
	}
	return sh;
}

static qboolean CI_BuildProgram( void )
{
	GLuint	vsh, fsh, prog;
	GLint	ok = 0;

	vsh = CI_Compile( GL_VERTEX_SHADER, CI_VS );
	fsh = vsh ? CI_Compile( GL_FRAGMENT_SHADER, CI_FS ) : 0;
	if ( !vsh || !fsh ) {
		if ( vsh ) {
			glDeleteShader( vsh );
		}
		ci_failed = qtrue;
		ri.Printf( PRINT_WARNING, "r_chunkInstancing: shader compile failed, disabling\n" );
		return qfalse;
	}
	prog = glCreateProgram();
	glAttachShader( prog, vsh );
	glAttachShader( prog, fsh );
	glBindAttribLocation( prog, 0, "aPos" );
	glBindAttribLocation( prog, 1, "aNormal" );
	glBindAttribLocation( prog, 2, "aUv" );
	glBindAttribLocation( prog, 3, "aI0" );
	glBindAttribLocation( prog, 4, "aI1" );
	glBindAttribLocation( prog, 5, "aI2" );
	glBindAttribLocation( prog, 6, "aI3" );
	glBindAttribLocation( prog, 7, "aAmb" );
	glBindAttribLocation( prog, 8, "aDir" );
	glBindAttribLocation( prog, 9, "aLdir" );
	glLinkProgram( prog );
	glGetProgramiv( prog, GL_LINK_STATUS, &ok );
	glDeleteShader( vsh );
	glDeleteShader( fsh );
	if ( !ok ) {
		glDeleteProgram( prog );
		ci_failed = qtrue;
		ri.Printf( PRINT_WARNING, "r_chunkInstancing: program link failed, disabling\n" );
		return qfalse;
	}
	ci_prog = prog;
	ci_uMVP = glGetUniformLocation( prog, "uMVP" );
	ci_uTex = glGetUniformLocation( prog, "uTex" );
	return qtrue;
}

// Decode a static single-frame md3 surface into an interleaved client array once.
static ciGeom_t *CI_GetGeom( md3Surface_t *surf )
{
	int			i, slot = -1;
	short		*xyz;
	float		*st;
	int			*tris;
	ciGeom_t	*g;

	for ( i = 0; i < CI_GEOM_SLOTS; i++ ) {
		if ( ci_geom[i].surf == surf ) {
			return &ci_geom[i];
		}
		if ( ci_geom[i].surf == NULL && slot < 0 ) {
			slot = i;
		}
	}
	if ( slot < 0 || surf->numVerts <= 0 || surf->numVerts > 65535 ) {
		return NULL;
	}

	g = &ci_geom[slot];
	g->geo = (float *)malloc( surf->numVerts * 8 * sizeof( float ) );
	g->idx = (unsigned short *)malloc( surf->numTriangles * 3 * sizeof( unsigned short ) );
	if ( !g->geo || !g->idx ) {
		if ( g->geo ) { free( g->geo ); g->geo = NULL; }
		if ( g->idx ) { free( g->idx ); g->idx = NULL; }
		return NULL;
	}

	xyz  = (short *)( (byte *)surf + surf->ofsXyzNormals );	// frame 0
	st   = (float *)( (byte *)surf + surf->ofsSt );
	tris = (int *)( (byte *)surf + surf->ofsTriangles );

	for ( i = 0; i < surf->numVerts; i++ ) {
		unsigned lat, lng;
		float	*o = &g->geo[i * 8];
		short	*v = &xyz[i * 4];

		o[0] = v[0] * MD3_XYZ_SCALE;
		o[1] = v[1] * MD3_XYZ_SCALE;
		o[2] = v[2] * MD3_XYZ_SCALE;

		lat = ( v[3] >> 8 ) & 0xff;
		lng = ( v[3] & 0xff );
		lat *= ( FUNCTABLE_SIZE / 256 );
		lng *= ( FUNCTABLE_SIZE / 256 );
		o[3] = tr.sinTable[( lat + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK] * tr.sinTable[lng];
		o[4] = tr.sinTable[lat] * tr.sinTable[lng];
		o[5] = tr.sinTable[( lng + ( FUNCTABLE_SIZE / 4 ) ) & FUNCTABLE_MASK];

		o[6] = st[i * 2 + 0];
		o[7] = st[i * 2 + 1];
	}
	for ( i = 0; i < surf->numTriangles * 3; i++ ) {
		g->idx[i] = (unsigned short)tris[i];
	}
	g->surf       = surf;
	g->numVerts   = surf->numVerts;
	g->numIndexes = surf->numTriangles * 3;
	return g;
}

void R_ChunkInst_Clear( void )
{
	ci_numBuckets = 0;
}

qboolean R_ChunkInst_Enabled( void )
{
	return (qboolean)( r_chunkInstancing && r_chunkInstancing->integer && !ci_failed );
}

// Collect one opaque, unfogged chunk fragment; returns qtrue if it was taken
// (caller must then skip the normal per-entity draw). Filtering happens here so
// the backend hook stays a one-liner.
qboolean R_ChunkInst_Collect( md3Surface_t *surf, shader_t *shader, trRefEntity_t *ent )
{
	int			i, b = -1;
	ciBucket_t	*bucket;
	float		*d;

	if ( !shader->stages || !shader->stages[0].active || !shader->stages[0].bundle[0].image ) {
		return qfalse;
	}
	// opaque texture stage only; alpha-tested / multi-blend cutouts stay on the FFP path
	if ( shader->stages[0].stateBits & ( GLS_ATEST_BITS | GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
		return qfalse;
	}

	for ( i = 0; i < ci_numBuckets; i++ ) {
		if ( ci_buckets[i].geom && ci_buckets[i].geom->surf == surf && ci_buckets[i].shader == shader ) {
			b = i;
			break;
		}
	}
	if ( b < 0 ) {
		ciGeom_t *g;
		if ( ci_numBuckets >= CI_MAX_BUCKETS ) {
			return qfalse;
		}
		g = CI_GetGeom( surf );
		if ( !g ) {
			return qfalse;
		}
		b = ci_numBuckets++;
		ci_buckets[b].geom   = g;
		ci_buckets[b].shader = shader;
		ci_buckets[b].count  = 0;
	}

	bucket = &ci_buckets[b];
	if ( bucket->count >= CI_BUCKET_CAP ) {
		return qfalse;	// overflow: let this one draw the old way
	}

	// per-instance stream: mat4 (model->world, column-major) then ambient/directed/lightDir
	d = &bucket->inst[bucket->count * CI_INST_FLOATS];
	d[0]  = ent->e.axis[0][0]; d[1]  = ent->e.axis[0][1]; d[2]  = ent->e.axis[0][2]; d[3]  = 0.0f;
	d[4]  = ent->e.axis[1][0]; d[5]  = ent->e.axis[1][1]; d[6]  = ent->e.axis[1][2]; d[7]  = 0.0f;
	d[8]  = ent->e.axis[2][0]; d[9]  = ent->e.axis[2][1]; d[10] = ent->e.axis[2][2]; d[11] = 0.0f;
	d[12] = ent->e.origin[0];  d[13] = ent->e.origin[1];  d[14] = ent->e.origin[2];  d[15] = 1.0f;
	d[16] = ent->ambientLight[0];  d[17] = ent->ambientLight[1];  d[18] = ent->ambientLight[2];  d[19] = 0.0f;
	d[20] = ent->directedLight[0]; d[21] = ent->directedLight[1]; d[22] = ent->directedLight[2]; d[23] = 0.0f;
	d[24] = ent->lightDir[0];      d[25] = ent->lightDir[1];      d[26] = ent->lightDir[2];      d[27] = 0.0f;
	bucket->count++;
	return qtrue;
}

// Draw every collected bucket as one instanced call each, then reset. Runs on
// the backend at the opaque->translucent boundary so chunks land in the depth
// buffer before translucent effects test against them.
void R_ChunkInst_Flush( void )
{
	float	mvp[16];
	int		b, k;

	if ( ci_numBuckets == 0 ) {
		return;
	}
	if ( !ci_prog && ( ci_failed || !CI_BuildProgram() ) ) {
		ci_numBuckets = 0;
		return;
	}

	// chunk verts reach world space via the per-instance matrix, so uMVP is world->clip
	myGlMultMatrix( backEnd.viewParms.world.modelMatrix, backEnd.viewParms.projectionMatrix, mvp );

	GL_State( GLS_DEPTHMASK_TRUE );		// opaque: depth test + write, no blend
	glUseProgram( ci_prog );
	glUniformMatrix4fv( ci_uMVP, 1, GL_FALSE, mvp );
	glUniform1i( ci_uTex, 0 );

	for ( b = 0; b < ci_numBuckets; b++ ) {
		ciBucket_t	*bk = &ci_buckets[b];
		float		*geo = bk->geom->geo;

		GL_Cull( bk->shader->cullType );
		GL_SelectTexture( 0 );
		GL_Bind( bk->shader->stages[0].bundle[0].image );

		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof( float ), geo + 0 );
		glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof( float ), geo + 3 );
		glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof( float ), geo + 6 );
		glEnableVertexAttribArray( 0 );
		glEnableVertexAttribArray( 1 );
		glEnableVertexAttribArray( 2 );

		for ( k = 0; k < 4; k++ ) {
			glVertexAttribPointer( 3 + k, 4, GL_FLOAT, GL_FALSE, CI_INST_STRIDE, bk->inst + 4 * k );
			glEnableVertexAttribArray( 3 + k );
			glVertexAttribDivisor( 3 + k, 1 );
		}
		glVertexAttribPointer( 7, 4, GL_FLOAT, GL_FALSE, CI_INST_STRIDE, bk->inst + 16 );
		glVertexAttribPointer( 8, 4, GL_FLOAT, GL_FALSE, CI_INST_STRIDE, bk->inst + 20 );
		glVertexAttribPointer( 9, 4, GL_FLOAT, GL_FALSE, CI_INST_STRIDE, bk->inst + 24 );
		glEnableVertexAttribArray( 7 );
		glEnableVertexAttribArray( 8 );
		glEnableVertexAttribArray( 9 );
		glVertexAttribDivisor( 7, 1 );
		glVertexAttribDivisor( 8, 1 );
		glVertexAttribDivisor( 9, 1 );

		glDrawElementsInstanced( GL_TRIANGLES, bk->geom->numIndexes, GL_UNSIGNED_SHORT, bk->geom->idx, bk->count );
	}

	glUseProgram( 0 );
	for ( k = 0; k <= 9; k++ ) {
		glDisableVertexAttribArray( k );
		glVertexAttribDivisor( k, 0 );	// clear per-instance streams for the FFP path
	}
	GL_SelectTexture( 0 );
	ci_numBuckets = 0;
}

#endif // VITA
