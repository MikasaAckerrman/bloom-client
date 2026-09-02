/* Dump the C geometry results as CSV so the Python mirror in
 * cl_dll/killfeed_ref/kf_ratios.py can be diffed against the REAL C code.
 * Any divergence means the previews no longer predict the game.
 *
 *   cc -I../cl_dll/include -o /tmp/kf_dump kf_dump.c -lm && /tmp/kf_dump
 *
 * Columns: kind,a,b,c,d,e,value -- five generic inputs, one result. Unused
 * inputs are 0. tests/kf_mirror_check.py maps each kind to the mirror call.
 */
#include <stdio.h>
#include "killfeed_layout.h"

/* zero font height is a degenerate case worth pinning: the fallback must clamp
 * to 1 rather than divide by zero or produce a 0-tall row */
static int kf_fallback_textH_for( int fontRasterH )
{
	kf_metrics m;
	kf_compute_metrics_for_font( fontRasterH, 1.0f, &m );
	return m.textH;
}

int main(void)
{
	int screens[] = { 240, 480, 540, 720, 1080, 1440, 2160, 2340, 4320 };
	float uscales[] = { 0.0f, 0.25f, 0.5f, 0.85f, 1.0f, 1.5f, 2.0f, 4.0f, 99.0f };
	int fonts[] = { 0, 1, 8, 13, 20, 27, 31, 43, 48, 96 };
	int i, j, k;

	printf("kind,a,b,c,d,e,value\n");

	/* the ONE scale, incl. clamps and degenerate inputs */
	for( i = 0; i < (int)(sizeof(screens)/sizeof(screens[0])); i++ )
	for( j = 0; j < (int)(sizeof(uscales)/sizeof(uscales[0])); j++ )
		printf("scale,%d,%.2f,0,0,0,%.9f\n", screens[i], uscales[j],
			kf_scale(screens[i], uscales[j]));
	printf("scale,0,1.00,0,0,0,%.9f\n", kf_scale(0, 1.0f));
	printf("scale,-5,1.00,0,0,0,%.9f\n", kf_scale(-5, 1.0f));

	/* reference-space -> screen-space rounding */
	{
		float refs[] = { 0.0f, 0.4f, 0.5f, 1.0f, 3.5f, 8.0f, 13.0f, 27.0f, 100.0f };
		for( i = 0; i < (int)(sizeof(refs)/sizeof(refs[0])); i++ )
		for( j = 0; j < (int)(sizeof(screens)/sizeof(screens[0])); j++ )
			printf("px,%.2f,%d,0,0,0,%d\n", refs[i], screens[j],
				kf_px(refs[i], kf_scale(screens[j], 1.0f)));
	}

	/* the whole metric bundle, resolution-driven */
	for( i = 0; i < (int)(sizeof(screens)/sizeof(screens[0])); i++ )
	for( j = 0; j < (int)(sizeof(uscales)/sizeof(uscales[0])); j++ )
	for( k = 0; k < (int)(sizeof(fonts)/sizeof(fonts[0])); k++ )
	{
		kf_metrics m;
		kf_compute_metrics( screens[i], uscales[j], fonts[k], &m );
		printf("m_textH,%d,%.2f,%d,0,0,%d\n",    screens[i], uscales[j], fonts[k], m.textH);
		printf("m_capH,%d,%.2f,%d,0,0,%d\n",     screens[i], uscales[j], fonts[k], m.capH);
		printf("m_padx,%d,%.2f,%d,0,0,%d\n",     screens[i], uscales[j], fonts[k], m.padx);
		printf("m_pady,%d,%.2f,%d,0,0,%d\n",     screens[i], uscales[j], fonts[k], m.pady);
		printf("m_gap,%d,%.2f,%d,0,0,%d\n",      screens[i], uscales[j], fonts[k], m.gap);
		printf("m_gapTight,%d,%.2f,%d,0,0,%d\n", screens[i], uscales[j], fonts[k], m.gapTight);
		printf("m_vgap,%d,%.2f,%d,0,0,%d\n",     screens[i], uscales[j], fonts[k], m.vgap);
		printf("m_corner,%d,%.2f,%d,0,0,%d\n",   screens[i], uscales[j], fonts[k], m.corner);
		printf("m_outline,%d,%.2f,%d,0,0,%d\n",  screens[i], uscales[j], fonts[k], m.outline);
		printf("m_marginX,%d,%.2f,%d,0,0,%d\n",  screens[i], uscales[j], fonts[k], m.marginX);
		printf("m_marginY,%d,%.2f,%d,0,0,%d\n",  screens[i], uscales[j], fonts[k], m.marginY);
		printf("m_raise,%d,%.2f,%d,0,0,%d\n",    screens[i], uscales[j], fonts[k], m.raise);
		printf("m_textScale,%d,%.2f,%d,0,0,%.9f\n", screens[i], uscales[j], fonts[k], m.textScale);
	}

	/* the font-driven fallback, used when the engine cannot scale text.
	 * userScale is exercised here too: this is the DEFAULT path, so a mirror
	 * that only checked scale 1.0 would not have caught cl_killfeed_scale being
	 * dropped on it. */
	for( k = 0; k < (int)(sizeof(fonts)/sizeof(fonts[0])); k++ )
	{
		for( j = 0; j < (int)(sizeof(uscales)/sizeof(uscales[0])); j++ )
		{
			kf_metrics m;
			kf_compute_metrics_for_font( fonts[k], uscales[j], &m );
			printf("f_textH,%d,%.9f,0,0,0,%d\n",  fonts[k], uscales[j], m.textH);
			printf("f_padx,%d,%.9f,0,0,0,%d\n",   fonts[k], uscales[j], m.padx);
			printf("f_gap,%d,%.9f,0,0,0,%d\n",    fonts[k], uscales[j], m.gap);
			printf("f_vgap,%d,%.9f,0,0,0,%d\n",   fonts[k], uscales[j], m.vgap);
			printf("f_corner,%d,%.9f,0,0,0,%d\n", fonts[k], uscales[j], m.corner);
			printf("f_raise,%d,%.9f,0,0,0,%d\n",  fonts[k], uscales[j], m.raise);
			printf("f_scale,%d,%.9f,0,0,0,%.9f\n", fonts[k], uscales[j], m.scale);
			printf("f_textScale,%d,%.9f,0,0,0,%.9f\n", fonts[k], uscales[j],
				   m.textScale);
		}
	}
	printf("f_textH,0,1.0,0,0,0,%d\n", kf_fallback_textH_for( 0 ));

	/* icon heights: texture sizes from the real GoldClient set, with and
	 * without the text-cell cap */
	{
		int texs[] = { 0, 1, 12, 16, 32, 46, 64 };
		int caps[] = { 0, 19, 27, 54 };   /* 0 = uncapped */
		int c;
		for( i = 0; i < (int)(sizeof(texs)/sizeof(texs[0])); i++ )
		for( j = 0; j < (int)(sizeof(screens)/sizeof(screens[0])); j++ )
		for( k = 0; k < (int)(sizeof(uscales)/sizeof(uscales[0])); k++ )
		for( c = 0; c < (int)(sizeof(caps)/sizeof(caps[0])); c++ )
			printf("iconh,%d,%d,%.2f,%d,0,%d\n", texs[i], screens[j], uscales[k],
				caps[c],
				kf_icon_height( texs[i], kf_scale(screens[j], uscales[k]),
								caps[c] ));
	}

	/* wing height: its own measured ratio, never capped */
	{
		int texs[] = { 0, 12, 32, 64, 128 };
		for( i = 0; i < (int)(sizeof(texs)/sizeof(texs[0])); i++ )
		for( j = 0; j < (int)(sizeof(screens)/sizeof(screens[0])); j++ )
		for( k = 0; k < (int)(sizeof(uscales)/sizeof(uscales[0])); k++ )
			printf("wingh,%d,%d,%.2f,0,0,%d\n", texs[i], screens[j], uscales[k],
				kf_wing_height( texs[i], kf_scale(screens[j], uscales[k]) ));
	}

	/* icon widths at a few heights, aspect preserved */
	{
		int ws[] = { 12, 32, 96, 117 }, hs[] = { 12, 32, 32, 32 };
		int h;
		for( i = 0; i < 4; i++ )
			for( h = 1; h <= 60; h += 7 )
				printf("iconw,%d,%d,%d,0,0,%d\n", ws[i], hs[i], h,
					kf_icon_width(ws[i], hs[i], h));
	}

	/* row heights */
	for( i = 0; i < (int)(sizeof(fonts)/sizeof(fonts[0])); i++ )
		for( j = 0; j <= 60; j += 5 )
			printf("rowh,%d,%d,0,0,0,%d\n", fonts[i], j,
				kf_row_height(fonts[i], j));


	/* alpha curve */
	{
		float d;
		for( d = -10.0f; d <= 260.0f; d += 10.0f )
			printf("alpha,%.1f,220,0,0,0,%.6f\n", d, kf_row_alpha(d, 220.0f));
		printf("alpha,5.0,0,0,0,0,%.6f\n", kf_row_alpha(5.0f, 0.0f));
	}

	/* vertical element placement: raised (wing) vs centred, across lifts */
	{
		kf_elem e;
		int top, cy, hh, lift, rl;
		e.slot = 0; e.w = 20; e.tightGap = 0; e.x = 0;
		for( top = 0; top <= 200; top += 50 )
		for( hh = 8; hh <= 40; hh += 8 )
		for( lift = 0; lift <= 1; lift++ )
		for( rl = 0; rl <= 22; rl += 11 )
		{
			cy = top + 24;
			e.h = hh; e.raised = lift;
			printf("elemy,%d,%d,%d,%d,%d,%d\n", top, cy, lift, hh, rl,
				kf_elem_y(&e, top, cy, rl));
		}
	}

	/* tallest: raised elements must NOT count toward the row height */
	{
		kf_elem el[3];
		int hraise;
		for( hraise = 8; hraise <= 60; hraise += 13 )
		{
			el[0].slot = -1; el[0].w = 40; el[0].h = 27; el[0].tightGap = 0;
			el[0].raised = 0; el[0].x = 0;
			el[1].slot = 0;  el[1].w = 20; el[1].h = 19; el[1].tightGap = 0;
			el[1].raised = 0; el[1].x = 0;
			el[2].slot = 0;  el[2].w = 25; el[2].h = hraise; el[2].tightGap = 0;
			el[2].raised = 1; el[2].x = 0;
			printf("tallest,%d,0,0,0,0,%d\n", hraise, kf_tallest(el, 3));
		}
	}

	/* local-player outline: overhang and the vgap-derived thickness cap */
	{
		int t, vg;
		for( t = 0; t <= 14; t++ )
			printf("bover,%d,0,0,0,0,%d\n", t, kf_border_overhang(t));
		for( t = 0; t <= 14; t++ )
			for( vg = 1; vg <= 8; vg++ )
				printf("bthick,%d,%d,0,0,0,%d\n", t, vg,
					   kf_border_thickness(t, vg));
	}
	return 0;
}
