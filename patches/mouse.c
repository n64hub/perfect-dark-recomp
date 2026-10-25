// Mouse look and aim, as the decompilation's PC port plays them (its port/src/input.c and src/game/bondmove.c) and as
// GoldenEye64Recomp-web's mouse aims: the game's side, src/mouse.cpp the program's. Only while the mouse plays
// (recomp_pd_mouse_playing: Mouse Sensitivity above 0% and the pointer locked to the game, src/mouse.cpp), and only for
// player 1; otherwise each function here is the decompilation's.
//  - Look: the motion turns Joanna and tilts her view, aiming or not, a count by 0.055 degrees at 50% (the PC port's
//    default) times the field of view over 60, so finer when zoomed. Applied after the game's input (bmove_tick) as a
//    turn of its own, so it stays out of the stick's turn speed and the crosshair's and gun's sway that follows it. The
//    crosshair stays where the game puts it, at the centre while she stands still, as the PC port's "Locked" mouse aim.
//  - Aim: right click, and the keys on L and R (Left Shift), aim without the game's aim mode, in control styles 1.1 and
//    1.2, whose L and R aim (recomp_pd_mouse_aim_held). Aim mode would stop her: the stick (W and S, the arrows) moves
//    its crosshair instead of walking and turning her, and C-left and C-right (A and D) lean instead of strafing. So
//    bmove_process_input doesn't see L and R (the player's joybutinhibit, as the game holds back a button still held
//    from a menu), and what aiming wants is put back where the game decides it: the crosshair (after the game hides
//    it), the weapon's zoom (player_tween_fov_y), and aiming to the rest of the game (bmove_is_in_sight_aim_mode: the
//    tracking weapons' targets). A pad's L and R are still the game's aim mode, and so are the keys with the mouse off.
//    The Aim Control option's Toggle holds it.
//  - No auto-aim, aiming or not, as a PC shooter's mouse aims (bmove_is_autoaim_*_enabled_for_current_weapon): the
//    game's Auto-Aim option, on by default, would pull the crosshair onto a target the mouse isn't pointing at as she
//    fires from the hip. With the mouse off, a pad and the keys keep the option.
//  - The wheel changes weapon, where and when the game's own buttons would (bmove_process_input), and while the mouse
//    aims zooms the weapons that zoom (the sniper rifle, the FarSight, the horizon scanner), as C-up and C-down do in
//    aim mode.
//  - Look Ahead, which levels the view while she walks, is off: it would fight the mouse (bmove_set_automovecentre_enabled).
// The buttons are src/mouse.cpp's: left is Z (fire), right is R (aim, and the menus' shoulder button).
// And the C key crouches, as GoldenEye64Recomp-web's Crouch does, the mouse playing or not (crouch_tick).

#include "patches.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/bondbike.h"
#include "game/bondgrab.h"
#include "game/bondcutscene.h"
#include "game/bondgun.h"
#include "game/footstep.h"
#include "game/gset.h"
#include "game/options.h"
#include "game/player.h"
#include "lib/joy.h"
#include "lib/snd.h"
#include "lib/vi.h"
#include "bss.h"
#include "data.h"

s32 bwalk_try_delta(struct coord *delta, f32 rotateamount, bool apply, f32 extrawidth, s32 types);
void bmove_process_input(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2);
bool bmove_is_autoaim_x_enabled(void);
bool bmove_is_autoaim_y_enabled(void);
f32 player_get_zoom_fov_y(void);
void player_set_zoom_fov_y(f32 fovy, f32 timemax);

// The motion player 1's tick took, in the PC port's units (src/mouse.cpp), until the look uses it.
static f32 s_MouseDx;
static f32 s_MouseDy;
// Whether the mouse aims this tick, and whether its aim was held the tick before (for Toggle).
static bool s_MouseAiming;
static bool s_MouseAimWasHeld;

static bool mouse_plays_for_current_player(void)
{
	return g_Vars.currentplayernum == 0 && recomp_pd_mouse_playing();
}

static bool mouse_aiming(void)
{
	return g_Vars.currentplayernum == 0 && s_MouseAiming;
}

// Before bmove_process_input: whether the mouse aims this tick, and while it does, the game's aim buttons held back
// from it.
static void mouse_aim_update(bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	s32 mpindex = g_Vars.currentplayerstats->mpindex;
	s32 controlmode = options_get_control_mode(mpindex);
	bool held;

	if ((controlmode != CONTROLMODE_11 && controlmode != CONTROLMODE_12)
			|| player->bondmovemode != MOVEMODE_WALK || player->isdead || player->pausemode != PAUSEMODE_UNPAUSED) {
		s_MouseAiming = false;
		s_MouseAimWasHeld = false;
		return;
	}

	held = allowc1buttons && recomp_pd_mouse_aim_held();

	if (options_get_aim_control(mpindex) == AIMCONTROL_HOLD) {
		s_MouseAiming = held;
	} else if (held && !s_MouseAimWasHeld) {
		s_MouseAiming = !s_MouseAiming;
	}

	s_MouseAimWasHeld = held;

	if (held || s_MouseAiming) {
		// The game lets go of a held-back button once it is released, so this holds L and R back only while held.
		player->joybutinhibit |= L_TRIG | R_TRIG;
		player->insightaimmode = false;
	}
}

RECOMP_PATCH void bmove_set_automovecentre_enabled(bool enabled)
{
	// @recomp: Look Ahead is off while the mouse plays.
	g_Vars.currentplayer->automovecentreenabled = enabled && !recomp_pd_mouse_playing();
}

RECOMP_PATCH bool bmove_is_in_sight_aim_mode(void)
{
	// @recomp: the mouse's aim is aiming too.
	return g_Vars.currentplayer->insightaimmode || mouse_aiming();
}

RECOMP_PATCH bool bmove_is_autoaim_y_enabled_for_current_weapon(void)
{
	struct funcdef *func = gset_get_current_funcdef(0);

	// @recomp: no auto-aim while the mouse plays, aiming or not.
	if (mouse_plays_for_current_player()) {
		return false;
	}

	if (func) {
		if (func->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((func->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	return bmove_is_autoaim_y_enabled();
}

RECOMP_PATCH bool bmove_is_autoaim_x_enabled_for_current_weapon(void)
{
	struct funcdef *func = gset_get_current_funcdef(0);

	// @recomp: no auto-aim while the mouse plays, aiming or not.
	if (mouse_plays_for_current_player()) {
		return false;
	}

	if (func) {
		if (func->flags & FUNCFLAG_NOAUTOAIM) {
			return false;
		}

		if ((func->type & 0xff) == INVENTORYFUNCTYPE_MELEE) {
			return true;
		}
	}

	return bmove_is_autoaim_x_enabled();
}

RECOMP_PATCH void player_tween_fov_y(f32 targetfovy)
{
	// @recomp: outside aim mode bmove_process_input asks for 60; while the mouse aims, for the weapon's zoom, as in aim
	// mode.
	if (targetfovy == 60 && mouse_aiming() && gset_get_gun_zoom_fov() > 0) {
		targetfovy = gset_get_gun_zoom_fov();
	}

	if (player_get_zoom_fov_y() != targetfovy) {
		if (g_Vars.currentplayer->zoominfovy > targetfovy) {
			player_set_zoom_fov_y(targetfovy, (g_Vars.currentplayer->zoominfovy - targetfovy) * 15.0f / 30.0f);
		} else {
			player_set_zoom_fov_y(targetfovy, (targetfovy - g_Vars.currentplayer->zoominfovy) * 15.0f / 30.0f);
		}
	}
}

// Turns the view by degrees, as bwalk_update_theta turns it by the stick's speed, and tilts it.
static void mouse_turn(f32 theta, f32 verta)
{
	struct coord delta = {0, 0, 0};

	if (theta != 0) {
		bwalk_try_delta(&delta, theta * 0.0174505133f, true, 0, CDTYPE_ALL);
	}

	if (verta != 0) {
		g_Vars.currentplayer->vv_verta += verta;
		g_Vars.currentplayer->docentreupdown = false;
		bmove_update_look();
	}
}

// The C key (src/mouse.cpp): a press squats Joanna, from standing or kneeling, and the next stands her back up, where the
// game's own crouch steps a position a press of C-down or C-up in aim mode (bmove_process_input). Only as she walks:
// a press in a cutscene, a pause or the radial menu is dropped.
static void crouch_tick(bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	s32 presses = recomp_pd_crouch_take();

	if (!allowc1buttons || player->bondmovemode != MOVEMODE_WALK || player->isdead
			|| player->pausemode != PAUSEMODE_UNPAUSED) {
		return;
	}

	for (; presses > 0; presses--) {
		bwalk_adjust_crouch_pos(player->crouchpos == CROUCHPOS_SQUAT
				? CROUCHPOS_STAND - CROUCHPOS_SQUAT : CROUCHPOS_SQUAT - CROUCHPOS_STAND);
	}
}

static void mouse_tick(bool allowc1x, bool allowc1y, bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	f32 fovscale = vi_get_fov_y() / 60.0f;
	s32 wheel = 0;

	if (player->bondmovemode != MOVEMODE_WALK || player->isdead || player->pausemode != PAUSEMODE_UNPAUSED
			|| !(allowc1x && allowc1y)) {
		return;
	}

	mouse_turn(s_MouseDx * fovscale * 3.5f, s_MouseDy * fovscale * 3.5f);

	if (s_MouseAiming) {
		// bmove_process_input hid the crosshair outside aim mode.
		bgun_set_sight_visible(GUNSIGHTREASON_NOTAIMING, true);
	}

	if (allowc1buttons && g_Vars.tickmode != TICKMODE_CUTSCENE && g_Vars.lvframenum >= 10) {
		wheel = recomp_pd_mouse_take_wheel();

		if (s_MouseAiming && gset_has_aim_flag(bgun_get_weapon_num(HAND_RIGHT), INVAIMFLAG_MANUALZOOM)) {
			// A notch zooms as C-up or C-down held for 10 60ths of a second in aim mode do, by a fifth, so about fifteen
			// cover the sniper rifle's 60 to 2 degrees.
			f32 notch = g_Vars.lvupdate60freal > 0 ? 10.0f / g_Vars.lvupdate60freal : 0;

			for (; wheel > 0; wheel--) {
				gset_zoom_in(notch);
			}

			for (; wheel < 0; wheel++) {
				gset_zoom_out(notch);
			}
		}

		while (wheel > 0) {
			bgun_cycle_back();
			wheel--;
		}

		while (wheel < 0) {
			bgun_cycle_forward();
			wheel++;
		}
	}
}

RECOMP_PATCH void bmove_tick(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2)
{
	struct chrdata *chr;
	u8 foot;
	s32 sound;
	f32 xdiff;
	f32 ydiff;
	f32 zdiff;
	f32 distance;
	bool mouse = mouse_plays_for_current_player();

	// @recomp: the mouse's motion since player 1's last tick, for the look, and whether it aims.
	s_MouseDx = 0;
	s_MouseDy = 0;

	if (mouse) {
		recomp_pd_mouse_take(&s_MouseDx, &s_MouseDy);
		mouse_aim_update(allowc1buttons);
	} else if (g_Vars.currentplayernum == 0) {
		s_MouseAiming = false;
		s_MouseAimWasHeld = false;
	}

	bmove_process_input(allowc1x, allowc1y, allowc1buttons, ignorec2);

	if (mouse) {
		mouse_tick(allowc1x, allowc1y, allowc1buttons);
	}

	// @recomp: the C key crouches player 1.
	if (g_Vars.currentplayernum == 0) {
		crouch_tick(allowc1buttons);
	}

	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbike_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrab_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_CUTSCENE) {
		bcutscene_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalk_tick();
	}

	// Update footstep sounds
	if ((g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK || g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB)
			&& (g_Vars.currentplayer->speedforwards || g_Vars.currentplayer->speedsideways)
			&& (!g_Vars.normmplayerisrunning || PLAYERCOUNT() == 1)) {
		chr = g_Vars.currentplayer->prop->chr;

		if (g_Vars.currentplayer->cameramode == CAMERAMODE_DEFAULT
				&& g_Vars.currentplayer->bdeltapos.y >= -6.0f) {
			xdiff = g_Vars.currentplayer->bondprevpos.x - g_Vars.currentplayer->prop->pos.x;
			ydiff = g_Vars.currentplayer->bondprevpos.y - g_Vars.currentplayer->prop->pos.y;
			zdiff = g_Vars.currentplayer->bondprevpos.z - g_Vars.currentplayer->prop->pos.z;

			foot = 0;
			distance = sqrtf(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

			g_Vars.currentplayer->footstepdist += distance;

			if (g_Vars.currentplayer->footstepdist >= 150.0f) {
				foot = 1;
				g_Vars.currentplayer->footstepdist = 0;
			}

			if (foot) {
				if (g_Vars.currentplayer->foot) {
					chr->footstep = 1;
				} else {
					chr->footstep = 2;
				}

				g_Vars.currentplayer->foot = 1 - g_Vars.currentplayer->foot;

				chr->floortype = g_Vars.currentplayer->floortype;

				sound = footstep_choose_sound(chr, distance > 10);

				if (sound != -1) {
					snd_start_extra(NULL, false, AL_VOL_FULL, AL_PAN_CENTER, sound, 1, 1, -1, true);
				}
			}
		}
	}
}
