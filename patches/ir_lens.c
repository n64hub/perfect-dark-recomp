// The IR scanner's lens, each of its rows loaded once.
//
// bview_draw_ir_lens (src/game/bondview.c) draws the view through the IR scanner a row at a time, as the night vision
// goggles' lens does, with bview_copy_pixels: a LoadBlock of the back buffer's row into TMEM and a one-row texture
// rectangle drawn over the same row, tinted by the row's scanline. Where a row crosses the semicircle in the middle,
// it draws it in three pieces (left of the centre, right of the semicircle, the semicircle itself), each a load of its
// own of that piece alone, so the second and third loads start mid-row (at an x that isn't a multiple of 4 pixels, an
// RDRAM address that isn't 8-byte aligned) and come after a draw into that row. RT64 found no tile copy for those
// pieces, so it waited for the framebuffer to come back to RAM for each one: the game drew 1.9 frames a second with
// the IR scanner on (Chrome uncapped at 3840x2000, RTX 5080, Pages' v0.130.0; 59 with it off).
//
// Here such a row is loaded once, whole, as every other row is, and the three pieces are drawn from it, each its own
// texture rectangle starting that many texels into the row: the same pixels, from the row's tile copy. The rest is the
// game's (ntsc-final), including where the pieces end: the left one viewcentrex wide and the right one ending at
// viewwidth, which only differ from the view's own centre and right edge in a split screen's right half.

#include "patches.h"
#include "bss.h"
#include "game/bondview.h"
#include "lib/vi.h"
#include "lib/rng.h"
#include "lib/str.h"

extern char var800a41c0[];
extern u8 g_IrScanlines[2][480];
extern s32 var8007f840;
extern s32 g_IrBinocularRadius;
extern s32 g_IrCentreSize;

Gfx *bview_copy_pixels(Gfx *gdl, u16 *fb, s32 top, u32 tile, s32 arg4, f32 arg5, s32 left, s32 width);
Gfx *bview_prepare_static_rgba16(Gfx *gdl, u32 colour, u32 alpha);

// Loads the back buffer's row y from x = left, width texels, into tile 5's TMEM, as bview_copy_pixels does.
static Gfx *ir_load_row(Gfx *gdl, u16 *fb, s32 y, s32 left, s32 width)
{
	gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_320, (uintptr_t) &fb[vi_get_width() * y + left] & 0x00ffffff);
	gDPLoadBlock(gdl++, 5, 0, 0, width - 1, 0);
	return gdl;
}

// Draws the piece of row y from x to x + width out of the row loaded from x = rowleft.
static Gfx *ir_draw_piece(Gfx *gdl, s32 y, s32 rowleft, s32 x, s32 width)
{
	if (width > 0) {
		gSPTextureRectangle(gdl++, x << 2, y << 2, (x + width) << 2, (y + 1) << 2, G_TX_RENDERTILE,
				(x - rowleft) << 5, 0, 1024, 1024);
	}

	return gdl;
}

RECOMP_PATCH Gfx *bview_draw_ir_lens(Gfx *gdl)
{
	s32 i;
	s32 fadeincrement;
	u16 *fb = vi_get_back_buffer();
	s32 viewheight = vi_get_view_height();
	s32 viewwidth = vi_get_view_width();
	s32 viewtop = vi_get_view_top();
	s32 viewleft = vi_get_view_left();
	s32 viewright;
	s32 viewbottom;
	s32 viewcentrex;
	s32 sqinnerradius;
	s32 scanincrement;
	s32 scantop;
	s32 scanbottom;
	s32 scanrate = 4;
	s32 faderate = 2;
	u32 red;
	s32 outerradius;
	s32 innerradius;
	s32 viewcentrey;
	s32 a0;

	viewright = viewleft + viewwidth;
	viewcentrex = (viewleft + viewright) / 2;

	outerradius = g_IrBinocularRadius;
	innerradius = g_IrBinocularRadius / g_IrCentreSize;

	var8007f840++;

	if (var8007f840 >= 2) {
		return gdl;
	}

	strcpy(var800a41c0, "Fullscreen_DrawFaultScope");

	viewbottom = viewtop + viewheight;
	viewcentrey = (viewtop + viewbottom) / 2;
	scantop = viewcentrey - outerradius;
	scanbottom = viewcentrey + outerradius;
	i = viewheight;

	if (scantop > viewbottom) {
		scantop = viewbottom;
	}

	if (scanbottom > viewbottom) {
		scanbottom = viewbottom;
	}

	if (scantop < viewtop) {
		scantop = viewtop;
	}

	if (scanbottom < viewtop) {
		scanbottom = viewtop;
	}

	scanincrement = (f32) scanrate * i / 240.0f;
	fadeincrement = (f32) faderate * i / 240.0f;

	// This code runs on the first frame of IR use (90 != 0),
	// and in debug versions developers could change the radius at runtime.
	if (g_Vars.currentplayer->fslastradius != outerradius) {
		for (i = 0; i < 480; i++) {
			g_IrScanlines[g_Vars.currentplayernum][i] = 0xff;
		}

		g_Vars.currentplayer->fsscanline = 0;
		g_Vars.currentplayer->fslastradius = outerradius;
	}

	// Increment the scanline
	for (i = 0; i < scanincrement; i++) {
		if (g_Vars.currentplayer->fsscanline >= scanbottom) {
			g_Vars.currentplayer->fsscanline = scantop;
		}

		g_IrScanlines[g_Vars.currentplayernum][g_Vars.currentplayer->fsscanline] = 0xff - i;

		g_Vars.currentplayer->fsscanline++;
	}

	g_GooglesRoomBrightness = 0xff;
	g_GogglesChrColourIntensity = 0xde;
	g_GogglesChrColourAlpha = 0xde;

	gDPPipeSync(gdl++);

	gdl = bview_prepare_static_rgba16(gdl, 0xffffffff, 255);

	sqinnerradius = innerradius * innerradius;

	for (i = scantop; i < scanbottom; i++) {
		if (i & 1) {
			red = g_IrScanlines[g_Vars.currentplayernum][i];
		} else {
			red = g_IrScanlines[g_Vars.currentplayernum][i] * 2 / 3;
		}

		red += random() % 8;

		if (red > 255) {
			red = 255;
		}

		gDPSetEnvColorViaWord(gdl++, (red << 24) + 0xff);

		a0 = viewcentrey - i;

		if (a0 * a0 < sqinnerradius) {
			// Rendering a line that overlaps the semicircle
			// in the middle of the screen
			f32 f0 = a0;
			s32 semicirclewidth = sqrtf(sqinnerradius - (s32) (f0 * f0)) * (viewwidth / (f32) SCREEN_WIDTH_LO);
			s32 semicircleright = viewcentrex + semicirclewidth;
			s32 rightsidewidth = viewwidth - semicircleright;

			if (viewwidth <= SCREEN_WIDTH_LO) {
				gdl = ir_load_row(gdl, fb, i, viewleft, viewwidth);

				// Left and right of semicircle
				gdl = ir_draw_piece(gdl, i, viewleft, viewleft, viewcentrex);
				gdl = ir_draw_piece(gdl, i, viewleft, semicircleright, rightsidewidth);

				// The semicircle itself has a static colour
				gDPSetEnvColorViaWord(gdl++, 0xee0000ff);
				gdl = ir_draw_piece(gdl, i, viewleft, viewcentrex, semicirclewidth);
			} else {
				// Wider than 320, bview_copy_pixels loads a row in two halves: as the game draws it.
				gdl = bview_copy_pixels(gdl, fb, i, 5, i, 1.0f, viewleft, viewcentrex);
				gdl = bview_copy_pixels(gdl, fb, i, 5, i, 1.0f, semicircleright, rightsidewidth);
				gDPSetEnvColorViaWord(gdl++, 0xee0000ff);
				gdl = bview_copy_pixels(gdl, fb, i, 5, i, 1.0f, viewcentrex, semicirclewidth);
			}
		} else {
			gdl = bview_copy_pixels(gdl, fb, i, 5, i, 1.0f, viewleft, viewwidth);
		}

		if (g_IrScanlines[g_Vars.currentplayernum][i] > fadeincrement) {
			g_IrScanlines[g_Vars.currentplayernum][i] -= fadeincrement;
		}
	}

	if (g_Menus[g_Vars.currentplayerstats->mpindex].curdialog == NULL) {
		gdl = bview_draw_motion_blur(gdl, 0xff000000, 0x40);
	}

	return gdl;
}
