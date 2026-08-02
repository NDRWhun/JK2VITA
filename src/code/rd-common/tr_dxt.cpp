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

// tr_dxt.cpp -- DXT1/DXT5 block compression.
// Endpoints come from the block's principal axis, then a least-squares refit.

#include "tr_dxt.h"

#include <math.h>

typedef unsigned char byte;

static inline int Clamp255( int v )
{
	return ( v < 0 ) ? 0 : ( v > 255 ? 255 : v );
}

static inline unsigned short Pack565( int r, int g, int b )
{
	return (unsigned short)( ( ( r >> 3 ) << 11 ) | ( ( g >> 2 ) << 5 ) | ( b >> 3 ) );
}

// the decoder sees the quantised endpoint, so the encoder must match on it
static inline void Unpack565( unsigned short c, int *r, int *g, int *b )
{
	const int r5 = ( c >> 11 ) & 31, g6 = ( c >> 5 ) & 63, b5 = c & 31;
	*r = ( r5 << 3 ) | ( r5 >> 2 );
	*g = ( g6 << 2 ) | ( g6 >> 4 );
	*b = ( b5 << 3 ) | ( b5 >> 2 );
}

static void BuildPalette( unsigned short c0, unsigned short c1, int pal[4][3] )
{
	Unpack565( c0, &pal[0][0], &pal[0][1], &pal[0][2] );
	Unpack565( c1, &pal[1][0], &pal[1][1], &pal[1][2] );
	for ( int i = 0; i < 3; i++ ) {
		pal[2][i] = ( 2 * pal[0][i] + pal[1][i] ) / 3;
		pal[3][i] = ( pal[0][i] + 2 * pal[1][i] ) / 3;
	}
}

static unsigned int MatchIndices( const byte *src, const int pal[4][3] )
{
	unsigned int bits = 0;
	for ( int i = 0; i < 16; i++ ) {
		const int r = src[i * 4 + 0], g = src[i * 4 + 1], b = src[i * 4 + 2];
		int best = 0, bestErr = 0x7FFFFFFF;
		for ( int k = 0; k < 4; k++ ) {
			const int dr = r - pal[k][0], dg = g - pal[k][1], db = b - pal[k][2];
			const int err = dr * dr + dg * dg + db * db;
			if ( err < bestErr ) {
				bestErr = err;
				best = k;
			}
		}
		bits |= (unsigned int)best << ( i * 2 );
	}
	return bits;
}

/*
================
RefitEndpoints

Least squares over the assigned indices: with the weights fixed, the endpoints
that minimise total error are the solution of a 2x2 system per channel.
================
*/
static void RefitEndpoints( const byte *src, unsigned int bits,
							unsigned short *c0, unsigned short *c1 )
{
	static const float wa[4] = { 1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f };
	static const float wb[4] = { 0.0f, 1.0f, 1.0f / 3.0f, 2.0f / 3.0f };

	float A = 0.0f, B = 0.0f, C = 0.0f;
	float X[3] = { 0, 0, 0 }, Y[3] = { 0, 0, 0 };

	for ( int i = 0; i < 16; i++ ) {
		const int k = ( bits >> ( i * 2 ) ) & 3;
		const float a = wa[k], b = wb[k];
		A += a * a;
		B += a * b;
		C += b * b;
		for ( int ch = 0; ch < 3; ch++ ) {
			const float p = (float)src[i * 4 + ch];
			X[ch] += a * p;
			Y[ch] += b * p;
		}
	}

	const float det = A * C - B * B;
	if ( det > -1e-6f && det < 1e-6f ) {
		return;	// every texel landed on one endpoint; the box fit already stands
	}

	const float inv = 1.0f / det;
	int e0[3], e1[3];
	for ( int ch = 0; ch < 3; ch++ ) {
		e0[ch] = Clamp255( (int)( ( X[ch] * C - Y[ch] * B ) * inv + 0.5f ) );
		e1[ch] = Clamp255( (int)( ( Y[ch] * A - X[ch] * B ) * inv + 0.5f ) );
	}
	*c0 = Pack565( e0[0], e0[1], e0[2] );
	*c1 = Pack565( e1[0], e1[1], e1[2] );
}

/*
================
AxisEndpoints

The four palette entries lie on a line, so the endpoints belong on the block's
principal axis. A bounding box only agrees with that when the colours happen to
run along its diagonal; on a two-tone block it can miss both colours entirely.
Power iteration on the covariance is enough to find the axis.
================
*/
static bool AxisEndpoints( const byte *src, unsigned short *c0, unsigned short *c1 )
{
	float mean[3] = { 0, 0, 0 };
	for ( int i = 0; i < 16; i++ ) {
		for ( int ch = 0; ch < 3; ch++ ) {
			mean[ch] += (float)src[i * 4 + ch];
		}
	}
	for ( int ch = 0; ch < 3; ch++ ) {
		mean[ch] *= 1.0f / 16.0f;
	}

	float xx = 0, yy = 0, zz = 0, xy = 0, xz = 0, yz = 0;
	for ( int i = 0; i < 16; i++ ) {
		const float dx = (float)src[i * 4 + 0] - mean[0];
		const float dy = (float)src[i * 4 + 1] - mean[1];
		const float dz = (float)src[i * 4 + 2] - mean[2];
		xx += dx * dx; yy += dy * dy; zz += dz * dz;
		xy += dx * dy; xz += dx * dz; yz += dy * dz;
	}

	float v[3] = { 1.0f, 1.0f, 1.0f };
	for ( int it = 0; it < 8; it++ ) {
		const float nx = xx * v[0] + xy * v[1] + xz * v[2];
		const float ny = xy * v[0] + yy * v[1] + yz * v[2];
		const float nz = xz * v[0] + yz * v[1] + zz * v[2];
		float m = ( nx < 0 ? -nx : nx );
		if ( ( ny < 0 ? -ny : ny ) > m ) m = ( ny < 0 ? -ny : ny );
		if ( ( nz < 0 ? -nz : nz ) > m ) m = ( nz < 0 ? -nz : nz );
		if ( m < 1e-9f ) {
			return false;	// no variance; the caller falls back
		}
		v[0] = nx / m; v[1] = ny / m; v[2] = nz / m;
	}

	const float len = sqrtf( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
	if ( len < 1e-9f ) {
		return false;
	}
	v[0] /= len; v[1] /= len; v[2] /= len;

	float tmin = 1e30f, tmax = -1e30f;
	for ( int i = 0; i < 16; i++ ) {
		const float t = ( (float)src[i * 4 + 0] - mean[0] ) * v[0]
					  + ( (float)src[i * 4 + 1] - mean[1] ) * v[1]
					  + ( (float)src[i * 4 + 2] - mean[2] ) * v[2];
		if ( t < tmin ) tmin = t;
		if ( t > tmax ) tmax = t;
	}

	int e0[3], e1[3];
	for ( int ch = 0; ch < 3; ch++ ) {
		e0[ch] = Clamp255( (int)( mean[ch] + v[ch] * tmax + 0.5f ) );
		e1[ch] = Clamp255( (int)( mean[ch] + v[ch] * tmin + 0.5f ) );
	}
	*c0 = Pack565( e0[0], e0[1], e0[2] );
	*c1 = Pack565( e1[0], e1[1], e1[2] );
	return true;
}

static void EncodeColorBlock( byte *dst, const byte *src, int refinePasses )
{
	unsigned short c0, c1;
	if ( !AxisEndpoints( src, &c0, &c1 ) ) {
		int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
		for ( int i = 0; i < 16; i++ ) {
			for ( int ch = 0; ch < 3; ch++ ) {
				const int v = src[i * 4 + ch];
				if ( v < lo[ch] ) lo[ch] = v;
				if ( v > hi[ch] ) hi[ch] = v;
			}
		}
		c0 = Pack565( hi[0], hi[1], hi[2] );
		c1 = Pack565( lo[0], lo[1], lo[2] );
	}

	int pal[4][3];
	BuildPalette( c0, c1, pal );
	unsigned int bits = MatchIndices( src, pal );

	for ( int pass = 0; pass < refinePasses; pass++ ) {
		unsigned short r0 = c0, r1 = c1;
		RefitEndpoints( src, bits, &r0, &r1 );
		if ( r0 == c0 && r1 == c1 ) {
			break;
		}
		c0 = r0; c1 = r1;
		BuildPalette( c0, c1, pal );
		bits = MatchIndices( src, pal );
	}

	// c0 > c1 selects the four-colour mode; equal endpoints are a flat block
	if ( c0 < c1 ) {
		const unsigned short t = c0; c0 = c1; c1 = t;
		bits ^= 0x55555555u;			// swaps 0<->1 and 2<->3
		BuildPalette( c0, c1, pal );
		bits = MatchIndices( src, pal );
	} else if ( c0 == c1 ) {
		bits = 0;
		if ( c1 ) {
			c1--;						// keep four-colour mode; index 0 is exact
		} else {
			c0 = 1;
		}
	}

	dst[0] = (byte)( c0 & 0xFF );
	dst[1] = (byte)( c0 >> 8 );
	dst[2] = (byte)( c1 & 0xFF );
	dst[3] = (byte)( c1 >> 8 );
	dst[4] = (byte)( bits & 0xFF );
	dst[5] = (byte)( ( bits >> 8 ) & 0xFF );
	dst[6] = (byte)( ( bits >> 16 ) & 0xFF );
	dst[7] = (byte)( ( bits >> 24 ) & 0xFF );
}

// eight-value mode: a0 > a1 gives six interpolants, which beats the 0/255 variant
static void EncodeAlphaBlock( byte *dst, const byte *src )
{
	int lo = 255, hi = 0;
	for ( int i = 0; i < 16; i++ ) {
		const int a = src[i * 4 + 3];
		if ( a < lo ) lo = a;
		if ( a > hi ) hi = a;
	}
	if ( hi == lo ) {
		if ( hi < 255 ) hi++; else lo--;
	}

	int pal[8];
	pal[0] = hi;
	pal[1] = lo;
	for ( int k = 1; k < 7; k++ ) {
		pal[k + 1] = ( ( 7 - k ) * hi + k * lo ) / 7;
	}

	dst[0] = (byte)hi;
	dst[1] = (byte)lo;

	unsigned long long bits = 0;
	for ( int i = 0; i < 16; i++ ) {
		const int a = src[i * 4 + 3];
		int best = 0, bestErr = 0x7FFFFFFF;
		for ( int k = 0; k < 8; k++ ) {
			const int d = a - pal[k];
			const int err = d * d;
			if ( err < bestErr ) {
				bestErr = err;
				best = k;
			}
		}
		bits |= (unsigned long long)best << ( i * 3 );
	}
	for ( int i = 0; i < 6; i++ ) {
		dst[2 + i] = (byte)( ( bits >> ( i * 8 ) ) & 0xFF );
	}
}

void R_CompressDxtBlock( byte *dst, const byte *src, int isDxt5, int highQuality )
{
	const int refinePasses = highQuality ? 3 : 1;

	if ( isDxt5 ) {
		EncodeAlphaBlock( dst, src );
		EncodeColorBlock( dst + 8, src, refinePasses );
	} else {
		EncodeColorBlock( dst, src, refinePasses );
	}
}
