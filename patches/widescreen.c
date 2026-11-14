// Aspect Ratio Expand: the game culls and scissors for the window's width, and its HUD sits at the window's edges.
//
// With Expand, RT64 draws the player's view across the whole window: a projection whose scissors span the frame's
// is drawn with its angle widened, and a fill or rectangle that spans them is stretched to the window's edges (the
// fades, the cutscene bars, the z-buffer's clear: rt64 src/render/rt64_framebuffer_renderer.cpp). The game still
// works out what is on the screen for its own 4:3, so rooms, props and characters past the 4:3 edges were culled,
// and its HUD stayed at the 4:3 edges. What the game sees here is ws_ext() more pixels on each side, as RT64 shows:
//  - Culling: the screen box the portals and rooms are tested against (bg_calculate_screen_properties) and the side
//    planes rooms and weather are tested against (cam0f0b5838) reach that far, and a little further, since the
//    window's width in the game's pixels is a few off from RT64's (RT64 rounds the frame's height it reads from the
//    VI to a multiple of 4).
//  - Scissors: a room's, prop's or character's scissor (bg_scissor_within_viewport) spans the frame's whole width,
//    as GoldenEye's recompilation does its own: RT64 widens a projection only where its scissor spans the frame's, so
//    a room seen through a door, scissored to the door, was drawn at 4:3 and cut at the 4:3 edge. The z-buffer still
//    hides what the scissor did. One outside the wider view or with no rows stays as the game gives it (made as wide
//    as the frame, an empty one lost the GPU device under Dozen as Joanna fired). A scissor wider than the frame would
//    make RT64 judge the frame not to be 4:3 and widen nothing.
//  - HUD: the mission timer and the left-aligned HUD messages go to the window's left edge, the ammo, the
//    right-aligned messages and the radar to its right, through RT64's rectangle alignment and, for a message's box
//    (triangles), its viewport alignment, inside a scissor as wide as the window. HUD Placement (RecompFrontend) then
//    decides how far out that is. The crosshair, the gun and the sights stay centred.
//  - The game's own 16:9 (Options, Video) is anamorphic, a squeezed picture for a widescreen TV: it reads as Normal
//    while Expand widens, so the picture isn't widened twice: in the Video menu too, and a game file saved then keeps
//    Normal.
// In split screen a view as wide as the frame (two players, one above the other) is widened the same way, since RT64
// widens it as it does one player's: with 4:3 culling the window's sides drew black. A view narrower than the frame
// (two side by side, three or four players) stays 4:3, ws_ext() 0, and so does Original, where every function here
// does what the decompilation does, command for command.
//
// The functions are src/game/bg.c's, camera.c's, hudmsg.c's, bondgun.c's, radar.c's and options.c's as the
// decompilation builds them for ntsc-final, with the changes marked.

#include "patches.h"
#include "rt64_extended_gbi.h"
#include "gbiex.h"
#include "game/bg.h"
#include "game/bondgun.h"
#include "game/camera.h"
#include "game/game_152fa0.h"
#include "game/gset.h"
#include "game/hudmsg.h"
#include "game/inv.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/scenarios.h"
#include "game/options.h"
#include "game/player.h"
#include "game/radar.h"
#include "game/text.h"
#include "lib/vi.h"
#include "bss.h"
#include "data.h"

// The game's own, declared in no header the patches include.
extern s32 g_ScreenRatio;
extern struct coord var8009dd60;
extern s32 g_HudPaddingY;
extern s32 g_HudPaddingX;
extern s32 g_NumHudMessages;
extern struct hudmessage *g_HudMessages;
extern u32 g_RadarX;
extern u32 g_RadarY;
extern struct ammotype g_AmmoTypes[];

bool countdown_timer_is_visible(void);
void format_time(char *dst, s32 time60, s32 precision);
s32 hudmsg_is_zoom_range_visible(void);
Gfx *hudmsg_render_zoom_range(Gfx *gdl, u32 alpha);
Gfx *hudmsg_render_box(Gfx *gdl, s32 x1, s32 y1, s32 x2, s32 y2, f32 bgopacity, u32 bordercolour, f32 textopacity);
Gfx *radar_render_background(Gfx *gdl, struct textureconfig *tconfig, s32 arg2, s32 arg3, s32 arg4);
Gfx *radar_render_r_tracked_props(Gfx *gdl);
Gfx *countdown_timer_render(Gfx *gdl);
bool chr_is_dead(struct chrdata *chr);
void bgun_get_weapon_info(struct handweaponinfo *info, s32 handnum);
bool bgun_is_using_secondary_function(void);
s32 bgun_get_ammo_state(s32 funcnum, struct handweaponinfo *info, struct hand *hand);
void bgun_reset_abmag(struct abmag *abmag);
Gfx *bgun_draw_hud_gauge(Gfx *gdl, s32 x1, s32 y1, s32 x2, s32 y2, struct abmag *abmag, s32 remaining, s32 capacity, u32 emptycolour, u32 filledcolour, bool flip);

// The pixels past the frame's 4:3 the window shows on each side, in the frame's pixels: 0 with Original, for a view
// narrower than the frame, and where the window is no wider than the frame. RT64 takes the frame's pixels as square.
static f32 ws_ext(void)
{
	f32 width;
	f32 height;
	f32 original;
	f32 aspect;

	width = vi_get_width();
	height = vi_get_height();

	if (width <= 0.0f || height <= 0.0f) {
		return 0.0f;
	}

	// A view narrower than the frame (split screen's halves side by side, its quarters) isn't widened by RT64.
	if (vi_get_view_left() > 0 || vi_get_view_left() + vi_get_view_width() < width) {
		return 0.0f;
	}

	original = width / height;
	aspect = recomp_pd_get_aspect_ratio(original);

	if (aspect <= original) {
		return 0.0f;
	}

	return (height * aspect - width) * 0.5f;
}

// The frame's pixels to spare, a few at 320 wide: what the window's width in them can be off from RT64's.
static f32 ws_slack(void)
{
	return vi_get_width() * (6.0f / 320.0f);
}

// How far culling looks past the 4:3 edges: ws_ext() and the slack, so nothing at the window's edge is culled.
static f32 ws_cull_ext(void)
{
	f32 ext = ws_ext();

	return ext > 0.0f ? ext + ws_slack() : 0.0f;
}

// Starts HUD drawn at a side of the window (G_EX_ORIGIN_LEFT or G_EX_ORIGIN_RIGHT): a scissor as wide as the window
// over the view's rows, and rectangles moved to that side by RT64, as far as they were from the frame's same side.
// With box, the triangles of a message's box move too, through the viewport. Only while ws_ext() is more than 0.
static Gfx *ws_hud_begin(Gfx *gdl, u32 origin, bool box)
{
	s32 width = vi_get_width();

	gEXPushScissor(gdl++);
	gEXSetScissor(gdl++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT,
			0, vi_get_view_top(), 0, vi_get_view_top() + vi_get_view_height());

	if (origin == G_EX_ORIGIN_RIGHT) {
		gEXSetRectAlign(gdl++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -width * 4, 0, -width * 4, 0);
	} else {
		gEXSetRectAlign(gdl++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
	}

	if (box) {
		gEXPushViewport(gdl++);
		gEXSetViewportAlign(gdl++, origin, origin == G_EX_ORIGIN_RIGHT ? -width * 4 : 0, 0);
		gSPViewport(gdl++, OS_K0_TO_PHYSICAL(vi_get_current_player_viewport()));
	}

	return gdl;
}

// Ends what ws_hud_begin started.
static Gfx *ws_hud_end(Gfx *gdl, bool box)
{
	if (box) {
		gEXSetViewportAlign(gdl++, G_EX_ORIGIN_NONE, 0, 0);
		gEXPopViewport(gdl++);
	}

	gEXSetRectAlign(gdl++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
	gEXPopScissor(gdl++);

	return gdl;
}

RECOMP_PATCH void bg_calculate_screen_properties(void)
{
	struct player *player = g_Vars.currentplayer;
	f32 width = vi_get_width();
	u32 stack;
	f32 height = vi_get_height();
	u32 stack2;
	// @recomp: the view and the screen as wide as the window.
	f32 ext = ws_cull_ext();

	player->screenxminf = vi_get_view_left() - ext;

	if (player->screenxminf < -ext) {
		player->screenxminf = -ext;
	}

	if (player->screenxminf > width + ext) {
		player->screenxminf = width + ext;
	}

	player->screenyminf = vi_get_view_top();

	if (player->screenyminf < 0) {
		player->screenyminf = 0;
	}

	if (player->screenyminf > height) {
		player->screenyminf = height;
	}

	player->screenxmaxf = vi_get_view_left() + vi_get_view_width() + ext;

	if (player->screenxmaxf < -ext) {
		player->screenxmaxf = -ext;
	}

	if (player->screenxmaxf > width + ext) {
		player->screenxmaxf = width + ext;
	}

	player->screenymaxf = vi_get_view_top() + vi_get_view_height();

	if (player->screenymaxf < 0) {
		player->screenymaxf = 0;
	}

	if (player->screenymaxf > height) {
		player->screenymaxf = height;
	}
}

RECOMP_PATCH void cam0f0b5838(void)
{
	f32 sp2c;
	f32 sp28;
	f32 sp24;
	f32 sp20;
	struct player *player;
	Mtxf *mtx;
	f32 sp14;
	f32 sp10;

	player = g_Vars.currentplayer;
	sp24 = player->c_halfheight * player->c_scaley;
	mtx = player->projectionmtx;

	sp2c = 1.0f / sqrtf(sp24 * sp24 + 1.0f);
	sp24 *= sp2c;
	sp20 = -sp2c;

	var8009dd20.f[0] = -sp20 * mtx->m[1][0] + (sp24) * mtx->m[2][0];
	var8009dd20.f[1] = -sp20 * mtx->m[1][1] + (sp24) * mtx->m[2][1];
	var8009dd20.f[2] = -sp20 * mtx->m[1][2] + (sp24) * mtx->m[2][2];

	var8009dd2c = var8009dd20.f[0] * mtx->m[3][0] + var8009dd20.f[1] * mtx->m[3][1] + var8009dd20.f[2] * mtx->m[3][2];

	var8009dd30.f[0] = sp20 * mtx->m[1][0] + (sp24) * mtx->m[2][0];
	var8009dd30.f[1] = sp20 * mtx->m[1][1] + (sp24) * mtx->m[2][1];
	var8009dd30.f[2] = sp20 * mtx->m[1][2] + (sp24) * mtx->m[2][2];

	var8009dd3c = var8009dd30.f[0] * mtx->m[3][0] + var8009dd30.f[1] * mtx->m[3][1] + var8009dd30.f[2] * mtx->m[3][2];

	// @recomp: the left and right planes at the window's edges.
	sp28 = -(player->c_halfwidth + ws_cull_ext()) * player->c_scalex;

	sp10 = 1.0f / sqrtf(sp28 * sp28 + 1.0f);
	sp28 *= sp10;
	sp14 = -sp10;

	var8009dd40.f[0] = sp14 * mtx->m[0][0] - sp28 * mtx->m[2][0];
	var8009dd40.f[1] = sp14 * mtx->m[0][1] - sp28 * mtx->m[2][1];
	var8009dd40.f[2] = sp14 * mtx->m[0][2] - sp28 * mtx->m[2][2];

	var8009dd4c = var8009dd40.f[0] * mtx->m[3][0] + var8009dd40.f[1] * mtx->m[3][1] + var8009dd40.f[2] * mtx->m[3][2];

	var8009dd50.f[0] = -sp14 * mtx->m[0][0] - sp28 * mtx->m[2][0];
	var8009dd50.f[1] = -sp14 * mtx->m[0][1] - sp28 * mtx->m[2][1];
	var8009dd50.f[2] = -sp14 * mtx->m[0][2] - sp28 * mtx->m[2][2];

	var8009dd5c = var8009dd50.f[0] * mtx->m[3][0] + var8009dd50.f[1] * mtx->m[3][1] + var8009dd50.f[2] * mtx->m[3][2];

	var8009dd60.f[0] = -mtx->m[3][0];
	var8009dd60.f[1] = -mtx->m[3][1];
	var8009dd60.f[2] = -mtx->m[3][2];

	var8009dd6c = mtx->m[2][0] * mtx->m[3][0] + mtx->m[2][1] * mtx->m[3][1] + mtx->m[2][2] * mtx->m[3][2];
}

RECOMP_PATCH Gfx *bg_scissor_within_viewport(Gfx *gdl, s32 viewleft, s32 viewtop, s32 viewright, s32 viewbottom)
{
	// @recomp: whether it is in the view as wide as the window, which it is drawn across (below).
	f32 ext = ws_cull_ext();
	bool wide = ext > 0.0f && viewleft < viewright
		&& viewright > g_Vars.currentplayer->viewleft - ext
		&& viewleft < g_Vars.currentplayer->viewleft + g_Vars.currentplayer->viewwidth + ext;

	if (viewleft < g_Vars.currentplayer->viewleft) {
		viewleft = g_Vars.currentplayer->viewleft;
	}

	if (viewtop < g_Vars.currentplayer->viewtop) {
		viewtop = g_Vars.currentplayer->viewtop;
	}

	if (viewright > g_Vars.currentplayer->viewleft + g_Vars.currentplayer->viewwidth) {
		viewright = g_Vars.currentplayer->viewleft + g_Vars.currentplayer->viewwidth;
	}

	if (viewbottom > g_Vars.currentplayer->viewtop + g_Vars.currentplayer->viewheight) {
		viewbottom = g_Vars.currentplayer->viewtop + g_Vars.currentplayer->viewheight;
	}

	// @recomp: as wide as the frame while Expand widens, so RT64 widens what it scissors; one with no rows stays empty.
	// Not cut to the portal as GoldenEye64Recomp-web's 62dfec0 and e517c93 cut: the z-buffer hides what is behind a
	// wall (props and characters draw with it), and in Investigation, Chicago and Defection no room, prop or guard drew
	// past its door at the whole width. Either cut left strips of clear colour where rooms reach the window's edges.
	if (wide && viewtop < viewbottom) {
		viewleft = g_Vars.currentplayer->viewleft;
		viewright = g_Vars.currentplayer->viewleft + g_Vars.currentplayer->viewwidth;
	}

	gDPSetScissor(gdl++, G_SC_NON_INTERLACE, viewleft, viewtop, viewright, viewbottom);

	return gdl;
}

RECOMP_PATCH s32 options_get_screen_ratio(void)
{
	// @recomp: Normal while Expand widens the picture.
	if (ws_ext() > 0.0f) {
		return SCREENRATIO_NORMAL;
	}

	return g_ScreenRatio;
}

RECOMP_PATCH Gfx *hudmsg_render_mission_timer(Gfx *gdl, u32 alpha)
{
	s32 x;
	s32 y;
	s32 viewleft;
	s32 timery;
	char buffer[24];
	u32 textcolour;
	s32 is4mb;
	s32 playercount;
	s32 playernum;
	s16 viewtop;
	s16 viewheight;
	bool ws;

	textcolour = alpha;

	viewleft = vi_get_view_left() / g_UiScaleX;
	viewtop = vi_get_view_top();
	viewheight = vi_get_view_height();
	playercount = PLAYERCOUNT();
	playernum = g_Vars.currentplayernum;

	timery = viewheight;
	timery += viewtop;
	timery -= g_HudPaddingY;
	timery -= 8;

	is4mb = IS4MB();

	// @bug: There is no check for playercount >= 2 in the next two statements.
	// Because of this, in 1 player the timer is drawn out of place when the
	// screen split option is vertical and either the countdown timer is visible
	// or a zoomable weapon is in use.
	if ((is4mb || options_get_screen_split() == SCREENSPLIT_VERTICAL) && countdown_timer_is_visible()) {
		timery -= 8;
	}

	if ((IS4MB() || options_get_screen_split() == SCREENSPLIT_VERTICAL || playercount >= 3) && hudmsg_is_zoom_range_visible()) {
		timery -= 8;
	}

	if (playercount == 2) {
		if (IS4MB() || (options_get_screen_split() != SCREENSPLIT_VERTICAL && playernum == 0)) {
			timery += 10;
		} else {
			timery += 2;
		}
	} else if (playercount >= 3) {
		if (playernum < 2) {
			timery += 10;
		} else {
			timery += 2;
		}
	} else {
		if (options_get_effective_screen_size() != SCREENSIZE_FULL) {
			timery += 8;
		}
	}

	// If this is a second player with their viewport on the right side of the
	// screen, move the timer left a bit as the safe zone doesn't need to be
	// considered.
	if (playercount == 2 && (options_get_screen_split() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 1) {
		viewleft -= 14;
	} else if (playercount >= 3 && (playernum & 1) == 1) {
		viewleft -= 14;
	}

	textcolour = textcolour * 160 / 255;
	// @recomp: the decompilation's empty `if (g_Is4Mb);` (a matching quirk) is left out.
	textcolour |= 0x00ff0000;

	format_time(buffer, player_get_mission_time(), TIMEPRECISION_HUNDREDTHS);

	x = viewleft + g_HudPaddingX + 3;
	y = timery;

	// @recomp: at the window's left edge.
	ws = ws_ext() > 0.0f;

	if (ws) {
		gdl = ws_hud_begin(gdl, G_EX_ORIGIN_LEFT, false);
	}

	// @recomp: vi_get_height, which the decompilation's vi_get_height_hack is.
	gdl = text_render_v1(gdl, &x, &y, buffer, g_CharsNumeric, g_FontNumeric, textcolour, 0x000000a0, vi_get_width(), vi_get_height(), 0, 0);

	if (ws) {
		gdl = ws_hud_end(gdl, false);
	}

	return gdl;
}

RECOMP_PATCH Gfx *hudmsgs_render(Gfx *gdl)
{
	struct hudmessage *msg;
	s32 i;
	u32 textcolour;
	u32 glowcolour;
	f32 sin;
	s32 x;
	s32 y;
	s32 timerthing = 255;
	s32 spdc = true;
	bool ws = ws_ext() > 0.0f;
	u32 wsorigin;

	g_UiScaleX = g_ViRes == VIRES_HI ? 2 : 1;

	gdl = text_begin(gdl);

	if ((g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)
			&& g_InCutscene
			&& g_MainIsEndscreen == 0
			&& g_Vars.currentplayernum == 0) {
		spdc = false;
	}

	for (i = 0; i < g_NumHudMessages; i++) {
		msg = &g_HudMessages[i];

		if (!msg->opacity) {
			continue;
		}

		if (msg->state == HUDMSGSTATE_FREE
				|| msg->state == HUDMSGSTATE_QUEUED
				|| (spdc && g_Vars.currentplayernum != msg->playernum)) {
			continue;
		}

		if (msg->flash) {
			s32 alpha;
			sin = sinf((msg->timer * DTOR(180)) / 60.0f);

			if (sin < 0.0f) {
				sin = -sin;
			}

			alpha = 192.0f * sin;

			textcolour = (msg->textcolour & 0xffffff00) + alpha;
			glowcolour = msg->glowcolour;
		} else {
			textcolour = msg->textcolour | 0xa0;
			glowcolour = msg->glowcolour;
		}

		if (msg->opacity != 255) {
			u32 textalpha = textcolour & 0xff;
			u32 glowalpha = glowcolour & 0xff;

			textalpha = (msg->opacity * textalpha) / 255;
			glowalpha = (msg->opacity * glowalpha) / 255;

			textcolour = (textcolour & 0xffffff00) + (textalpha & 0xff);
			glowcolour = (glowcolour & 0xffffff00) + (glowalpha & 0xff);
		}

		x = msg->x;
		y = msg->y;

		if (msg->type == HUDMSGTYPE_INGAMESUBTITLE && player_is_health_visible()) {
			y += (s32)(16.0f * player_get_health_bar_height_frac());
		}

		if (msg->type == HUDMSGTYPE_CUTSCENESUBTITLE) {
			gDPSetScissor(gdl++, 0,
					(x - 4) * g_UiScaleX, 0,
					(x + msg->width + 3) * g_UiScaleX, vi_get_buf_height());
		}

		// @recomp: a message aligned to a side of the view at that side of the window, as the PC port puts it.
		wsorigin = G_EX_ORIGIN_NONE;

		if (ws && msg->type != HUDMSGTYPE_CUTSCENESUBTITLE) {
			if (msg->alignh == HUDMSGALIGN_SCREENLEFT || msg->alignh == HUDMSGALIGN_LEFT) {
				wsorigin = G_EX_ORIGIN_LEFT;
			} else if (msg->alignh == HUDMSGALIGN_RIGHT) {
				wsorigin = G_EX_ORIGIN_RIGHT;
			}
		}

		if (wsorigin != G_EX_ORIGIN_NONE) {
			gdl = ws_hud_begin(gdl, wsorigin, msg->boxed);
		}

		switch (msg->state) {
		case HUDMSGSTATE_FREE:
		case HUDMSGSTATE_QUEUED:
			break;
		case HUDMSGSTATE_FADINGIN:
			{
				u32 bordercolour = msg->textcolour | 0x40;
				f32 tmp;
				f32 spc0;

				if (msg->opacity != 255) {
					u32 alpha = (msg->opacity * (bordercolour & 0xff)) / 255;
					bordercolour = (bordercolour & 0xffffff00) + (alpha & 0xff);
				}

				spc0 = (sqrtf(msg->width * msg->width + msg->height * msg->height) + 132.0f) / PALUPF(7.0f);

				if (spc0 > 30.0f) {
					spc0 = 30.0f;
				}

				spc0 = msg->timer / spc0;

				if (spc0 > 1.0f) {
					spc0 = 1.0f;
				}

				if (spc0 < 0.0f) {
					spc0 = 0.0f;
				}

				tmp = msg->timer * PALUPF(7.0f);

				text_set_diagonal_blend(x, y, tmp, DIAGMODE_FADEIN);

				if (msg->boxed) {
					gdl = hudmsg_render_box(gdl, x - 3, y - 3, x + msg->width + 2, y + msg->height + 2, 1.0f, bordercolour, spc0);

					gdl = text_render_v2(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, vi_get_width(), vi_get_height(), 0, 0);
				} else {
					gdl = text_draw_box(gdl, x, y, x + msg->width, y + msg->height, 0);
					gdl = text_render_vx(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, glowcolour, vi_get_width(), vi_get_height(), 0, 0);
				}

				if (msg->alignv == 6) {
					timerthing = 0;
				}

				text_reset_blends();
			}
			break;
		case HUDMSGSTATE_ONSCREEN:
			if (msg->boxed) {
				u32 bordercolour = msg->textcolour | 0x40;

				if (msg->opacity != 255) {
					u32 alpha = (msg->opacity * (bordercolour & 0xff)) / 255;
					bordercolour = (bordercolour & 0xffffff00) + (alpha & 0xff);
				}

				gdl = hudmsg_render_box(gdl, x - 3, y - 3, x + msg->width + 2, y + msg->height + 2, 1.0f, bordercolour, 1.0f);

				gdl = text_render_v2(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, vi_get_width(), vi_get_height(), 0, 0);
			} else {
				gdl = text_draw_box(gdl, x, y, x + msg->width, y + msg->height, 0);
				gdl = text_render_vx(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, glowcolour, vi_get_width(), vi_get_height(), 0, 0);
			}
			if (msg->alignv == 6) {
				timerthing = 0;
			}
			break;
		case HUDMSGSTATE_FADINGOUT:
			{
				u32 bordercolour;
				u32 stack;
				f32 spa8 = (sqrtf(msg->width * msg->width + msg->height * msg->height) + 92.0f) / PALUPF(7.0f);
				f32 tmp;

				bordercolour = msg->textcolour | 0x40;

				if (msg->opacity != 255) {
					u32 alpha = (msg->opacity * (bordercolour & 0xff)) / 255;
					bordercolour = (bordercolour & 0xffffff00) + (alpha & 0xff);
				}

				tmp = (spa8 - msg->timer) * PALUPF(7.0f);

				text_set_diagonal_blend(x + msg->width, y + msg->height, tmp, DIAGMODE_FADEOUT);

				if (spa8 > 30.0f) {
					spa8 = 30.0f;
				}

				spa8 = msg->timer / spa8;

				if (spa8 > 1.0f) {
					spa8 = 1.0f;
				}

				if (msg->boxed) {
					gdl = hudmsg_render_box(gdl, x - 3, y - 3, x + msg->width + 2, y + msg->height + 2, 1.0f, bordercolour, 1.0f - spa8);

					gdl = text_render_v2(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, vi_get_width(), vi_get_height(), 0, 0);
				} else {
					gdl = text_draw_box(gdl, x, y, x + msg->width, y + msg->height, 0);
					gdl = text_render_vx(gdl, &x, &y, msg->text, msg->font1, msg->font2, textcolour, glowcolour, vi_get_width(), vi_get_height(), 0, 0);
				}

				if (msg->alignv == 6) {
					timerthing = 0;
				}

				text_reset_blends();
			}
			break;
		}

		if (wsorigin != G_EX_ORIGIN_NONE) {
			gdl = ws_hud_end(gdl, msg->boxed);
		}

		if (msg->type == HUDMSGTYPE_CUTSCENESUBTITLE) {
			gDPSetScissor(gdl++, 0,
					vi_get_view_left(), vi_get_view_top(),
					vi_get_view_left() + vi_get_view_width(), vi_get_view_top() + vi_get_view_height());
		}
	}

	if (timerthing) {
		if (options_get_show_mission_time(g_Vars.currentplayerstats->mpindex)
				&& var80075d60 == 2
				&& g_Vars.normmplayerisrunning == false
				&& g_Vars.stagenum != STAGE_CITRAINING
				&& g_Vars.currentplayer->cameramode != CAMERAMODE_EYESPY
				&& g_Vars.currentplayer->cameramode != CAMERAMODE_THIRDPERSON) {
			gdl = hudmsg_render_mission_timer(gdl, timerthing);
		}

		if (hudmsg_is_zoom_range_visible()) {
			gdl = hudmsg_render_zoom_range(gdl, timerthing);
		}

		gdl = countdown_timer_render(gdl);
	}

	gdl = text_end(gdl);

	g_UiScaleX = 1;

	return gdl;
}

RECOMP_PATCH Gfx *bgun_draw_hud(Gfx *gdl)
{
	struct player *player = g_Vars.currentplayer;
	s32 bottom = vi_get_view_top() + vi_get_view_height() - 13;
	s32 playercount = PLAYERCOUNT();
	s32 playernum = g_Vars.currentplayernum;
	struct gunctrl *ctrl;
	s32 secs60;
	s32 speedpilltime;
	s32 ammoindex = 0;
	s32 barwidth = 9;
	s32 reserveheight = 36;
	s32 clipheight = 57;
	s32 xpos;
	struct weapondef *weapon = gset_get_weapondef(player->gunctrl.weaponnum);
	u32 alpha;
	u32 fncolour;
	s32 funcnum;
	s32 fnfaderinc;
	s32 tmpfuncnum;
	struct handweaponinfo info;
	struct hand *hand = &player->hands[HAND_RIGHT];
	char *str;
	u32 colour;
	s32 x;
	s32 y;
	s32 textheight;
	s32 textwidth;
	struct funcdef *func;
	u16 nameid;
	struct hand *lefthand = &player->hands[HAND_LEFT];
	bool ws;

	ctrl = &player->gunctrl;

	if (player->isdead) {
		return gdl;
	}

	if (g_Vars.currentplayer->gunctrl.passivemode) {
		return gdl;
	}

	if (g_Vars.lvframenum < 5) {
		return gdl;
	}

	g_UiScaleX = g_ViRes == VIRES_HI ? 2 : 1;

	gdl = text_begin(gdl);

	// @recomp: at the window's right edge.
	ws = ws_ext() > 0.0f;

	if (ws) {
		gdl = ws_hud_begin(gdl, G_EX_ORIGIN_RIGHT, false);
	}

	if (playercount >= 2) {
		barwidth = 5;
		reserveheight = 26;
		clipheight = 47;

		if (playercount == 2) {
			if (IS4MB() || (options_get_screen_split() != SCREENSPLIT_VERTICAL && playernum == 0)) {
				bottom += 10;
			} else {
				bottom += 2;
			}
		} else if (playercount >= 3) {
			if (playernum < 2) {
				bottom += 10;
			} else {
				bottom += 2;
			}
		}
	} else if (options_get_effective_screen_size() != SCREENSIZE_FULL) {
		bottom += 8;
	}

	fncolour = 0xff000040;
	funcnum = hand->gset.weaponfunc;
	fnfaderinc = PALUP(g_Vars.lvupdate240 * 2);

	bgun_get_weapon_info(&info, HAND_RIGHT);
	tmpfuncnum = bgun_is_using_secondary_function();

	if (bgun_get_ammo_state(tmpfuncnum, &info, hand) > GUNAMMOSTATE_DEPLETED) {
		funcnum = tmpfuncnum;
	}

	xpos = (vi_get_view_left() + vi_get_view_width()) / g_UiScaleX - barwidth - 24;

	if (playercount == 2 && (options_get_screen_split() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 0) {
		xpos += 15;
	} else if (playercount >= 3 && (playernum % 2) == 0) {
		xpos += 15;
	}

	// Draw function square
	if (funcnum == FUNC_SECONDARY && ctrl->fnfader < 255) {
		if (ctrl->fnfader < 128) {
			ctrl->fnfader = 128;
		}

		if (ctrl->fnfader + fnfaderinc > 255) {
			ctrl->fnfader = 255;
		} else {
			ctrl->fnfader += fnfaderinc;
		}
	}

	if (funcnum == FUNC_PRIMARY && ctrl->fnfader > 0) {
		if (ctrl->fnfader - fnfaderinc < 0) {
			ctrl->fnfader = 0;
		} else {
			ctrl->fnfader -= fnfaderinc;
		}
	}

	if (ctrl->fnfader > 128) {
		fncolour = ((ctrl->fnfader * 2) - 256) << 16 | 0xff000040;
	}

	gdl = text_begin_boxmode(gdl, fncolour);

	gDPFillRectangleScaled(gdl++, xpos - 13, bottom - 11, xpos - 2, bottom);

	gdl = text_end_boxmode(gdl);

	// Draw weapon name and function name
	if (options_get_show_gun_function(g_Vars.currentplayerstats->mpindex)) {
		func = gset_get_funcdef_by_weaponnum_funcnum(hand->gset.weaponnum, funcnum);
		nameid = inv_get_name_id_by_index(inv_get_current_index());
		str = lang_get(nameid);

		if (ctrl->curgunstr != nameid) {
			ctrl->guntypetimer = 0;
			ctrl->curgunstr = nameid;
		}

		if (ctrl->guntypetimer < 255) {
			colour = 0x55ffffff;

			// @recomp: the decompilation's empty `if (ctrl->guntypetimer);` (a matching quirk) is left out.

			if (ctrl->guntypetimer + g_Vars.lvupdate60 > 255) {
				ctrl->guntypetimer = 255;
			} else {
				ctrl->guntypetimer += (u16) g_Vars.lvupdate60;
			}

			text_measure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
			textwidth += 2;

			if (textwidth > ctrl->guntypetimer * 3) {
				textwidth = ctrl->guntypetimer * 3;
			}

			if (playercount >= 2) {
				x = xpos - textwidth - 13;
			} else {
				x = xpos - textwidth - 2;
			}

			y = bottom - textheight - 15;

			if (ctrl->guntypetimer > 192) {
				alpha = 255 - (ctrl->guntypetimer - 192) * 255 / 63U;
				colour = (colour & 0xffffff00) | alpha;
				// @recomp: the decompilation's empty `if (0xffffff00);` (a matching quirk) is left out.
			}

			gdl = text_begin_boxmode(gdl, 0);

			gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom);

			gdl = text_end_boxmode(gdl);
			text_set_wave_blend(g_20SecIntervalFrac * 50.0f, 0, 50);
			text_set_wave_colours(0xffffffff, 0xffffffff);
			gdl = text_render_v2(gdl, &x, &y, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth, 1000, 0, 0);
			text_reset_blends();
		}

		if (func) {
			lang_get(func->name);

			colour = 0xff5555ff;

			if ((ctrl->curfnstr != func->name && ctrl->fnfader > 128) || ctrl->curfnstr == 0) {
				ctrl->fnstrtimer = 0;
				ctrl->curfnstr = func->name;
			}

			str = lang_get(ctrl->curfnstr);

			if (ctrl->fnstrtimer < 255) {
				if (ctrl->fnstrtimer + g_Vars.lvupdate60 > 255) {
					ctrl->fnstrtimer = 255;
				} else {
					ctrl->fnstrtimer += (u16) g_Vars.lvupdate60;
				}

				if (funcnum == FUNC_SECONDARY && func->name == ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}

				if (funcnum == FUNC_PRIMARY && func->name != ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}

				text_measure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
				textwidth += 2;

				if (textwidth > ctrl->fnstrtimer * 3) {
					textwidth = ctrl->fnstrtimer * 3;
				}

				x = xpos - textwidth - 13;
				y = bottom - textheight - 1;

				if (ctrl->fnstrtimer > 192) {
					alpha = 255 - (ctrl->fnstrtimer - 192) * 255 / 63U;
					colour = (colour & 0xffffff00) | alpha;
				}

				gdl = text_begin_boxmode(gdl, 0);

				gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom + 3);

				gdl = text_end_boxmode(gdl);

				text_set_wave_blend(g_20SecIntervalFrac * 50.0f, 0, 50);
				text_set_wave_colours(0xffffffff, 0xffffffff);

				gdl = text_render_v2(gdl, &x, &y, str,
						g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth,
						1000, 0, 0);

				text_reset_blends();
			}
		}
	}

	if (weapon && weapon->functions[hand->gset.weaponfunc] != NULL) {
		ammoindex = ((struct funcdef *)(weapon->functions[hand->gset.weaponfunc]))->ammoindex;
	}

	if (ammoindex == -1) {
		if (weapon->functions[1 - hand->gset.weaponfunc] != NULL) {
			ammoindex = ((struct funcdef *)(weapon->functions[1 - hand->gset.weaponfunc]))->ammoindex;
		}

		if (ammoindex == -1) {
			// @recomp: what ws_hud_begin started ends.
			if (ws) {
				gdl = ws_hud_end(gdl, false);
			}

			gdl = text_end(gdl);
			g_UiScaleX = 1;
			return gdl;
		}
	}

	if (ammoindex != ctrl->lastmag) {
		bgun_reset_abmag(&player->hands[HAND_LEFT].abmag);
		bgun_reset_abmag(&hand->abmag);
		bgun_reset_abmag(&ctrl->abmag);
		ctrl->lastmag = ammoindex;
	}

	// Left hand - mag
	if (lefthand->inuse
			&& weapon->ammos[ammoindex] != NULL
			&& lefthand->gset.weaponnum != WEAPON_REMOTEMINE) {
		xpos = vi_get_view_left() / g_UiScaleX + 24;

		if (playercount == 2 && (options_get_screen_split() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 1) {
			xpos -= 14;
		} else if (playercount >= 3 && (playernum & 1) == 1) {
			xpos -= 14;
		}

		if (lefthand->clipsizes[ammoindex] > 0 && (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
			// @recomp: the left gun's at the window's left edge.
			if (ws) {
				gEXSetRectAlign(gdl++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
			}

			gdl = bgun_draw_hud_gauge(gdl,
					xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth, bottom - reserveheight - 3,
					&lefthand->abmag, lefthand->loadedammo[ammoindex], lefthand->clipsizes[ammoindex],
					0x00300080, 0x00ff0040, false);
			gdl = bgun_draw_hud_integer(gdl, lefthand->loadedammo[ammoindex], xpos + barwidth + 2, true,
					bottom - reserveheight - 8, 0, 0x00ff00a0);

			if (ws) {
				gEXSetRectAlign(gdl++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT,
						-vi_get_width() * 4, 0, -vi_get_width() * 4, 0);
			}
		}
	}

	// Right hand - mag, reserve and combat boost timer
	if (hand->inuse && ctrl->ammotypes[ammoindex] >= 0) {
		s32 ammotype;
		s32 ammoheld;
		s32 ammototal;

		ammotype = player->gunctrl.ammotypes[ammoindex];

		xpos = (vi_get_view_left() + vi_get_view_width()) / g_UiScaleX - barwidth - 24;

		if (playercount == 2 && (options_get_screen_split() == SCREENSPLIT_VERTICAL || IS4MB()) && playernum == 0) {
			xpos += 15;
		} else if (playercount >= 3 && (playernum % 2) == 0) {
			xpos += 15;
		}

		// Mag
		ammoheld = player->ammoheldarr[ammotype];

		if (hand->clipsizes[ammoindex] > 0
				&& weapon->ammos[ammoindex] != NULL
				&& (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
			gdl = bgun_draw_hud_gauge(gdl, xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth,
					bottom - reserveheight - 3, &hand->abmag, hand->loadedammo[ammoindex], hand->clipsizes[ammoindex],
					0x00300080, 0x00ff0040, false);
			gdl = bgun_draw_hud_integer(gdl, hand->loadedammo[ammoindex], xpos - 2, false,
					bottom - reserveheight - 8, 0, 0x00ff00a0);
		}

		// Reserve
		if (g_AmmoTypes[ammotype].capacity > 0
				&& (weapon->ammos[ammoindex]->flags & AMMOFLAG_NORESERVE) == 0) {
			ammototal = ammoheld;

			if (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) {
				if (hand->clipsizes[ammoindex] > 0) {
					ammototal += hand->loadedammo[ammoindex];
				}

				if (lefthand->clipsizes[ammoindex] > 0) {
					ammototal += lefthand->loadedammo[ammoindex];
				}
			}

			gdl = bgun_draw_hud_gauge(gdl, xpos, bottom - reserveheight, xpos + barwidth,
					bottom, &ctrl->abmag, ammototal, g_AmmoTypes[ammotype].capacity,
					0x00403080, 0x00ffc040, true);
			gdl = bgun_draw_hud_integer(gdl, ammototal, xpos - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
		}

		// Combat boost timer
		if (hand->gset.weaponnum == WEAPON_COMBATBOOST) {
			s32 mins;
			char text[32];

			speedpilltime = g_Vars.speedpilltime;
			mins = speedpilltime / TICKS(3600);
			secs60 = speedpilltime - mins * TICKS(3600);

			if (mins >= 1) {
				sprintf(text, "%02d:%02d:%02d\n", mins, secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
			} else {
				sprintf(text, "%02d:%02d\n", secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
			}

			gdl = bgun_draw_hud_string(gdl, text, xpos + barwidth - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
		}
	}

	if (ws) {
		gdl = ws_hud_end(gdl, false);
	}

	gdl = text_end(gdl);

	g_UiScaleX = 1;

	return gdl;
}

RECOMP_PATCH Gfx *radar_render(Gfx *gdl)
{
	s32 stack;
	s32 stack2;
	s32 playercount;
	s32 playernum;
	struct textureconfig *tconfig;
	struct coord pos;
	u32 colour;
	s32 i;
	bool ws;

	tconfig = &g_TexRadarConfigs[TEX_RADAR_BG];
	playernum = g_Vars.currentplayernum;
	playercount = PLAYERCOUNT();

	if (g_Vars.mplayerisrunning) {
		if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_NORADAR)) {
			return gdl;
		}

		if ((g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.displayoptions & 0x00000004) == 0) {
			return gdl;
		}
	} else if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_RTRACKER) == 0) {
		if (!g_MissionConfig.iscoop || !g_Vars.coopradaron) {
			return gdl;
		}
	}

	if (g_Vars.currentplayer->mpmenuon || g_Vars.currentplayer->isdead) {
		return gdl;
	}

	if (g_ViRes == VIRES_HI) {
		g_UiScaleX = 2;
	} else {
		g_UiScaleX = 1;
	}

	g_RadarX = (vi_get_view_left() + vi_get_view_width()) / g_UiScaleX - 41;

	if (playercount == 2) {
		if (IS4MB() || options_get_screen_split() == SCREENSPLIT_VERTICAL) {
			if (playernum == 0) {
				g_RadarX += 16;
			}

			if (IS4MB()) {
				g_RadarX -= 4;
			}
		} else {
			g_RadarX -= 7;
		}
	} else if (playercount >= 3) {
		if ((playernum & 1) == 0) {
			g_RadarX += 7;
		} else {
			g_RadarX -= 7;
		}
	}

	g_RadarY = vi_get_view_top() + (PAL ? 29 : 26);

	if (playercount == 2) {
		if (IS4MB()) {
			g_RadarY -= 6;
		} else if (options_get_screen_split() != SCREENSPLIT_VERTICAL && playernum == 1) {
			g_RadarY -= 8;
		}
	} else if (playercount >= 3) {
		if (playernum >= 2) {
			g_RadarY -= 8;
		} else {
			g_RadarY -= 2;
		}
	} else {
		if (options_get_effective_screen_size() != SCREENSIZE_FULL) {
			g_RadarY -= 6;
		}
	}

	// @recomp: at the window's right edge.
	ws = ws_ext() > 0.0f;

	if (ws) {
		gdl = ws_hud_begin(gdl, G_EX_ORIGIN_RIGHT, false);
	}

	gdl = radar_render_background(gdl, tconfig, g_RadarX, g_RadarY, 0x10);
	gdl = func0f153134(gdl);

	// Draw dots for human players
	for (i = 0; i < playercount; i++) {
		if (i != playernum) {
			if (g_Vars.players[i]->isdead == false
					&& (g_Vars.players[i]->prop->chr->hidden & CHRHFLAG_CLOAKED) == 0
					&& scenario_radar_chr(&gdl, g_Vars.players[i]->prop) == false) {
				pos.x = g_Vars.players[i]->prop->pos.x - g_Vars.currentplayer->prop->pos.x;
				pos.y = g_Vars.players[i]->prop->pos.y - g_Vars.currentplayer->prop->pos.y;
				pos.z = g_Vars.players[i]->prop->pos.z - g_Vars.currentplayer->prop->pos.z;

				if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_TEAMSENABLED)) {
					s32 index = g_PlayerConfigsArray[g_Vars.playerstats[i].mpindex].base.team;
					colour = g_TeamColours[index];
				} else {
					colour = 0x00ff0000;
				}

				gdl = radar_draw_dot(gdl, g_Vars.players[i]->prop, &pos, colour, 0, 0);
			}
		}
	}

	// Draw dots for coop AI buddies
	if (!g_Vars.normmplayerisrunning && g_MissionConfig.iscoop) {
		for (i = 0; i < g_Vars.numaibuddies && i < ARRAYCOUNT(g_Vars.aibuddies); i++) {
			struct prop *prop = g_Vars.aibuddies[i];

			if (prop
					&& prop->type == PROPTYPE_CHR
					&& prop->chr
					&& prop->chr->actiontype != ACT_DIE
					&& prop->chr->actiontype != ACT_DEAD) {
				pos.x = prop->pos.x - g_Vars.currentplayer->prop->pos.x;
				pos.y = prop->pos.y - g_Vars.currentplayer->prop->pos.y;
				pos.z = prop->pos.z - g_Vars.currentplayer->prop->pos.z;

				gdl = radar_draw_dot(gdl, prop, &pos, 0x00ff0000, 0, 0);
			}
		}
	}

	// Draw dots for MP simulants
	if (g_Vars.normmplayerisrunning) {
		for (i = 0; i < g_BotCount; i++) {
			if (!chr_is_dead(g_MpBotChrPtrs[i])
					&& (g_MpBotChrPtrs[i]->hidden & CHRHFLAG_CLOAKED) == 0
					&& scenario_radar_chr(&gdl, g_MpBotChrPtrs[i]->prop) == false) {
				pos.x = g_MpBotChrPtrs[i]->prop->pos.x - g_Vars.currentplayer->prop->pos.x;
				pos.y = g_MpBotChrPtrs[i]->prop->pos.y - g_Vars.currentplayer->prop->pos.y;
				pos.z = g_MpBotChrPtrs[i]->prop->pos.z - g_Vars.currentplayer->prop->pos.z;

				if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_TEAMSENABLED)) {
					colour = g_TeamColours[radar_get_team_index(g_MpBotChrPtrs[i]->team)];
				} else {
					colour = 0x00ff0000;
				}

				gdl = radar_draw_dot(gdl, g_MpBotChrPtrs[i]->prop, &pos, colour, 0, 0);
			}
		}
	}

	gdl = scenario_radar_extra(gdl);

	// Draw dots for r-tracked props
	if (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_RTRACKER) {
		gdl = radar_render_r_tracked_props(gdl);
	}

	// Draw dot for the current player
	if (scenario_radar_chr(&gdl, g_Vars.currentplayer->prop) == false) {
		pos.x = 0;
		pos.y = 0;
		pos.z = 0;

		if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_TEAMSENABLED)) {
			s32 index = g_PlayerConfigsArray[g_Vars.playerstats[playernum].mpindex].base.team;
			colour = g_TeamColours[index];
		} else {
			colour = 0x00ff0000;
		}

		gdl = radar_draw_dot(gdl, g_Vars.currentplayer->prop, &pos, colour, 0, 0);
	}

	if (ws) {
		gdl = ws_hud_end(gdl, false);
	}

	g_UiScaleX = 1;

	return gdl;
}
