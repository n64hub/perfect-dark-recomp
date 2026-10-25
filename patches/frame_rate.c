// The game's own frame rate, told to RT64.
//
// The main loop ticks at most once a retrace (g_Vars.mininc60, 1 in every build) and each tick advances the game by
// the time it took, counted in 1/60 s and 1/240 s (frametime_calculate, src/game/timing.c): on a console Perfect Dark
// draws at 15-30 a second, here at 60 where the host keeps up. RT64 interpolates to the display's rate from the rate a
// frame says it was made at (gEXSetRefreshRate), as GoldenEye's recompilation does, so each frame's display list
// starts with it.
//
// That rate is the fastest of the last three frames' (each one's retraces, diffframe60), as RT64 guesses a rate from
// the VIs itself (VIHistory::logicalRateFromFactors), not the frame's own. A frame's interpolated frames are drawn
// before the next frame comes, so its rate is a guess at how long it will be on screen, and the frame after a slow one
// comes a VI later as the others do. Told the frame's own, a frame of two VIs among frames of one (a few a second in
// a level) had RT64 plan two VIs of presents and drop half of them when the next frame came, and RT64's pacing, which
// counts those drops as the GPU falling behind and paces to a multiple of the latest rate, held the page at 60
// presents a second or below on a 240 Hz display in both browsers, its tries back up failing within a second
// (N64Recomp-browser's plays.jsonl, set pd-fps). A run of slow frames still reads as their rate.
//
// A tick that would find no controller sample newer than the last tick's while a poll is out is put off, retrace by
// retrace, until the sample comes, for two VIs at most. As the game is, joy_debug_joy (src/lib/joy.c) stops and
// restarts the polling then, which takes two VIs, and the retraces queued meanwhile start the next tick before the
// restarted poll's sample, so it misses too and every frame is two VIs (docs/web.md, Frame rate). The poll that is out
// has its sample at the next VI's joys_handle_retrace, which the scheduler runs before it sends that VI's retrace.
// Retraces already queued come first, two or more where a slow frame started that chain, so the tick is put off past
// each of them, not once (once, the page fell back to 30 for 10-20 s at a time). Polling stopped (a pak's menu) has no
// poll out, and a sample that hasn't come in two VIs is left to the game, which restarts the polling as it would.
// Draining the retraces queued in main_loop instead wouldn't do: the next tick would start at the RDP done of the task
// just submitted, before the next VI, and miss its sample as well.
//
// On the page, while the game can't keep 60 up (a phone's CPU), its ticks are held to every other VI and each frame
// says 30 (recomp_pd_hold_at_30, src/pd_natives.cpp, which says when): mixing frames of one and two VIs, the fastest of
// three reads 60 and RT64 draws about one frame for each, where at 30 each gets two.
//
// main_tick is src/lib/main.c's, as the decompilation builds it for ntsc-final (no DEBUG), with those two commands put
// at the start of the frame's display list with the matrix groups after them (patches/origin_interpolation.c), the
// survey's tick before it (patches/shader_survey.c) and the put-off tick.

#include "patches.h"
#include "rt64_extended_gbi.h"
#include "lib/main.h"
#include "lib/profile.h"
#include "lib/sched.h"
#include "lib/vi.h"
#include "lib/joy.h"
#include "lib/mema.h"
#include "lib/lib_34d0.h"
#include "lib/rdp.h"
#include "game/gfxmemory.h"
#include "game/lv.h"
#include "game/playermgr.h"
#include "game/title.h"
#include "game/timing.h"
#include "game/stubs/game_175f50.h"
#include "game/debug.h"
#include "bss.h"
#include "data.h"

extern bool g_MainGameLogicEnabled;
extern u32 g_MainNumGfxTasks;
extern s32 g_MainChangeToStageNum;
extern s32 g_StageNum;

void shader_survey_tick(void);
Gfx *origin_tag_frame(Gfx *gdl);

// src/lib/joy.c's, which no header has.
struct joydata {
	struct {
		OSContPad pads[4];
	} samples[20];
	s32 curlast;
	s32 curstart;
	s32 nextlast;
};

extern struct joydata g_JoyData[];
extern bool g_JoyBusy;
extern u32 var8005eec0;

static bool s_TickPutOff = false;
static u32 s_TickPutOffAt;
static s32 s_RecentRetraces[3];
static s32 s_HeldAt30 = false;

RECOMP_PATCH void main_tick(void)
{
	Gfx *gdl;
	Gfx *gdlstart;
	OSScMsg msg = {OS_SC_DONE_MSG, NULL};
	s32 i;

	// @recomp: a script's request to start a mission, between frames (patches/shader_survey.c).
	shader_survey_tick();

	if (g_MainChangeToStageNum < 0 && g_MainNumGfxTasks < NUM_GFXTASKS) {
		// @recomp: held to 30, a tick only at a retrace a VI and a half or more after the last one started, as
		// main_loop starts one with g_Vars.mininc60 at 2 (which frametime_calculate would wait out in a loop).
		if (s_HeldAt30 && osGetCount() - g_Vars.thisframestartt < CYCLES_PER_FRAME * 3 / 2) {
			return;
		}

		// @recomp: no sample newer than the last tick's, with a poll out that brings one at the next VI: wait for it,
		// two VIs at most, where joy_debug_joy would restart the polling.
		if (joy_is_cyclic_polling_enabled() && var8005eec0 && g_JoyBusy
				&& g_JoyData[0].nextlast == g_JoyData[0].curlast) {
			if (!s_TickPutOff) {
				s_TickPutOff = true;
				s_TickPutOffAt = osGetCount();
			}

			if (osGetCount() - s_TickPutOffAt < 2 * CYCLES_PER_FRAME) {
				return;
			}
		}

		s_TickPutOff = false;
		s_HeldAt30 = recomp_pd_hold_at_30();
		frametime_calculate();
		profile00009a98();
		profile_reset();
		profile_set_marker(PROFILE_MAINTICK_START);
		func000034d8();
		joy_debug_joy();
		sched_set_crash_enable2(false);

		if (g_MainGameLogicEnabled) {
			gdl = gdlstart = gfx_get_master_display_list();

			// @recomp: the game's rate, from the fewest retraces of the last three frames (at least one).
			{
				s32 fewest = g_Vars.diffframe60 > 0 ? g_Vars.diffframe60 : 1;

				s_RecentRetraces[2] = s_RecentRetraces[1];
				s_RecentRetraces[1] = s_RecentRetraces[0];
				s_RecentRetraces[0] = fewest;

				for (i = 1; i < 3; i++) {
					if (s_RecentRetraces[i] > 0 && s_RecentRetraces[i] < fewest) {
						fewest = s_RecentRetraces[i];
					}
				}

				if (s_HeldAt30 && fewest < 2) {
					fewest = 2;
				}

				gEXEnable(gdl++);
				gEXSetRefreshRate(gdl++, 60 / fewest);
			}
			// @recomp: no interpolation across a move of the world's origin.
			gdl = origin_tag_frame(gdl);

			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, 6, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);

			lv_tick();
			playermgr_shuffle();

			if (g_StageNum < STAGE_TITLE) {
				for (i = 0; i < PLAYERCOUNT(); i++) {
					set_current_player_num(playermgr_get_player_at_order(i));

					if (g_StageNum != STAGE_TEST_OLD || !title_is_keeping_mode()) {
						vi_set_view_position(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
						vi_set_fov_aspect_and_size(
								g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
								g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
					}

					lv_tick_player();
				}
			}

			gdl = lv_render(gdl);
			func000034e0(&gdl);

			if (debug_get_profile_mode() >= 2) {
				gdl = profile_render(gdl);
			}

			gDPFullSync(gdl++);
			gSPEndDisplayList(gdl++);
		}

		if (g_MainGameLogicEnabled) {
			gfx_swap_buffers();
			vi_update_mode();
		}

		rdp_create_task(gdlstart, gdl, 0, (s32) &msg);
		g_MainNumGfxTasks++;
		mema_print();
		func0f16cf94();
		profile_set_marker(PROFILE_MAINTICK_END);
	}
}
