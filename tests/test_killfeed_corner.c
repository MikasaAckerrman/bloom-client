/* Standalone raster harness for KF_Border / KF_FilledPlate corner geometry.
 *
 * Draws the plate and the outline into an in-memory bitmap using the SAME
 * arithmetic as death.cpp, then prints the corner inset profile so it can be
 * compared against the profiles measured off the reference screenshots:
 *
 *   reference 1920x1080 outlined row: bottom-left [3, 2, 1, 1, 1, ...]
 *   ours before the fix:              all four    [0, 0, 0, 0, 0, ...]
 *
 * A monotone staircase means the corner follows an arc. A flat profile means it
 * is square. Build: gcc -o /tmp/kfborder /tmp/kf_border_raster.c -lm
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#define W 200
#define H 80
static unsigned char plate[H][W];
static unsigned char border[H][W];

static void fill_plate( int x, int y, int w, int h )
{
	int i, j;
	for( j = y; j < y + h; j++ )
		for( i = x; i < x + w; i++ )
			if( j >= 0 && j < H && i >= 0 && i < W )
				plate[j][i] = 1;
}

static void fill_border( int x, int y, int w, int h )
{
	int i, j;
	for( j = y; j < y + h; j++ )
		for( i = x; i < x + w; i++ )
			if( j >= 0 && j < H && i >= 0 && i < W )
				border[j][i] = 1;
}

/* mirror of kf_border_overhang() in killfeed_layout.h */
static int overhang( int t ) { return ( t * 2 ) / 3; }

/* mirror of KF_FilledPlate */
static void KF_FilledPlate( int x, int y, int w, int h, int radius )
{
	int i;
	if( radius < 1 || radius * 2 >= h || radius * 2 >= w )
	{
		fill_plate( x, y, w, h );
		return;
	}
	fill_plate( x, y + radius, w, h - radius * 2 );
	for( i = 0; i < radius; i++ )
	{
		int dx = radius - (int)( sqrtf( (float)( radius * radius
					- ( radius - 1 - i ) * ( radius - 1 - i ) ) ) + 0.5f );
		int rowW = w - dx * 2;
		if( rowW <= 0 ) continue;
		fill_plate( x + dx, y + i,         rowW, 1 );
		fill_plate( x + dx, y + h - 1 - i, rowW, 1 );
	}
}

/* mirror of KF_Border AFTER the fix */
static void KF_Border( int x, int y, int w, int h, int t, int radius )
{
	int out = overhang( t );
	int ox = x - out, oy = y - out;
	int ow = w + out * 2, oh = h + out * 2;
	int rr = radius + out;
	int i;

	if( rr < 1 || rr * 2 >= oh || rr * 2 >= ow )
	{
		fill_border( ox, oy, ow, t );
		fill_border( ox, oy + oh - t, ow, t );
		fill_border( ox, oy, t, oh );
		fill_border( ox + ow - t, oy, t, oh );
		return;
	}

	fill_border( ox + rr, oy,          ow - rr * 2, t );
	fill_border( ox + rr, oy + oh - t, ow - rr * 2, t );
	fill_border( ox,          oy + rr, t, oh - rr * 2 );
	fill_border( ox + ow - t, oy + rr, t, oh - rr * 2 );

	for( i = 0; i < rr; i++ )
	{
		int dx = rr - (int)( sqrtf( (float)( rr * rr
					- ( rr - 1 - i ) * ( rr - 1 - i ) ) ) + 0.5f );
		int topY = oy + i;
		int botY = oy + oh - 1 - i;
		fill_border( ox + dx,          topY, t, 1 );
		fill_border( ox + ow - dx - t, topY, t, 1 );
		fill_border( ox + dx,          botY, t, 1 );
		fill_border( ox + ow - dx - t, botY, t, 1 );
	}
}

static void bbox( unsigned char m[H][W], int *x0, int *y0, int *x1, int *y1 )
{
	int i, j;
	*x0 = W; *y0 = H; *x1 = -1; *y1 = -1;
	for( j = 0; j < H; j++ )
		for( i = 0; i < W; i++ )
			if( m[j][i] )
			{
				if( i < *x0 ) *x0 = i;
				if( i > *x1 ) *x1 = i;
				if( j < *y0 ) *y0 = j;
				if( j > *y1 ) *y1 = j;
			}
}

static void profile( unsigned char m[H][W], const char *label,
					 int side_top, int side_left, int depth )
{
	int x0, y0, x1, y1, d;
	bbox( m, &x0, &y0, &x1, &y1 );
	printf( "  %-14s", label );
	for( d = 0; d < depth; d++ )
	{
		int y = side_top ? y0 + d : y1 - d;
		int i, edge = -1;
		if( y < 0 || y >= H ) { printf( " ." ); continue; }
		if( side_left )
		{
			for( i = x0; i <= x1; i++ ) if( m[y][i] ) { edge = i; break; }
			printf( " %d", edge < 0 ? -1 : edge - x0 );
		}
		else
		{
			for( i = x1; i >= x0; i-- ) if( m[y][i] ) { edge = i; break; }
			printf( " %d", edge < 0 ? -1 : x1 - edge );
		}
	}
	printf( "\n" );
}

static int monotone_nondecreasing_from_corner( unsigned char m[H][W],
											   int side_top, int side_left,
											   int depth, int *first )
{
	/* the inset must START above zero and DECREASE to zero: that is an arc */
	int x0, y0, x1, y1, d, prev = -1, seen = 0, ok = 1;
	bbox( m, &x0, &y0, &x1, &y1 );
	*first = -1;
	for( d = 0; d < depth; d++ )
	{
		int y = side_top ? y0 + d : y1 - d;
		int i, edge = -1, inset;
		if( y < 0 || y >= H ) break;
		if( side_left ) { for( i = x0; i <= x1; i++ ) if( m[y][i] ) { edge = i; break; } inset = edge - x0; }
		else            { for( i = x1; i >= x0; i-- ) if( m[y][i] ) { edge = i; break; } inset = x1 - edge; }
		if( edge < 0 ) continue;
		if( !seen ) { *first = inset; seen = 1; }
		if( prev >= 0 && inset > prev ) ok = 0;   /* must not grow inward */
		prev = inset;
	}
	return ok;
}

int main( void )
{
	struct { int scale_name; int w, h, t, radius; } cases[] = {
		{ 1080, 120, 33, 3, 3 },     /* reference-ish: plate 33px, outline 3, corner 3 */
		{ 1260, 140, 39, 4, 4 },     /* our phone at scale 1.0 */
		{ 1764, 160, 54, 5, 5 },     /* our phone at scale 1.4 */
	};
	int c, fails = 0;

	for( c = 0; c < 3; c++ )
	{
		int w = cases[c].w, h = cases[c].h, t = cases[c].t, rad = cases[c].radius;
		int first, ok, x0, y0, x1, y1;

		memset( plate, 0, sizeof( plate ) );
		memset( border, 0, sizeof( border ) );

		KF_FilledPlate( 20, 10, w, h, rad );
		KF_Border( 20, 10, w, h, t, rad );

		printf( "\n=== case %d: plate %dx%d, outline t=%d, corner r=%d ===\n",
				c + 1, w, h, t, rad );
		bbox( border, &x0, &y0, &x1, &y1 );
		printf( "  outline bbox w=%d h=%d (plate %dx%d + overhang %d each side)\n",
				x1 - x0 + 1, y1 - y0 + 1, w, h, overhang( t ) );

		profile( border, "top    left", 1, 1, 8 );
		profile( border, "top    right", 1, 0, 8 );
		profile( border, "bottom left", 0, 1, 8 );
		profile( border, "bottom right", 0, 0, 8 );

		ok  = monotone_nondecreasing_from_corner( border, 1, 1, 8, &first );
		if( !ok || first <= 0 ) { printf( "  FAIL: top-left is not an arc (first inset %d)\n", first ); fails++; }
		ok  = monotone_nondecreasing_from_corner( border, 0, 0, 8, &first );
		if( !ok || first <= 0 ) { printf( "  FAIL: bottom-right is not an arc (first inset %d)\n", first ); fails++; }

		/* the outline must not leave the plate's rounded cap uncovered:
		 * at every scanline the outline's inset must be <= the plate's inset + overhang */
		{
			int px0, py0, px1, py1, d, bad = 0;
			bbox( plate, &px0, &py0, &px1, &py1 );
			for( d = 0; d < rad; d++ )
			{
				int py = py0 + d, i, pedge = -1, bedge = -1;
				for( i = px0; i <= px1; i++ ) if( plate[py][i] ) { pedge = i; break; }
				for( i = x0;  i <= x1;  i++ ) if( border[py][i] ) { bedge = i; break; }
				if( pedge < 0 || bedge < 0 ) continue;
				if( bedge > pedge ) bad++;   /* outline starts further in than the plate: gap */
			}
			if( bad ) { printf( "  FAIL: outline leaves %d plate scanline(s) unbordered\n", bad ); fails++; }
			else        printf( "  OK: outline never starts inside the plate edge\n" );
		}
	}

	printf( "\n%s\n", fails ? "SOME CORNER CHECKS FAILED" : "ALL CORNER CHECKS PASSED" );
	return fails ? 1 : 0;
}
