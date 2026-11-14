// The survey: lets a script start any solo mission by itself, keep the player alive in it and end it completed, for
// the page's shader list (src/rt64_shaders.txt, recorded by tools/shader_plan.json) and its play plans
// (tools/play_plan.json, tools/play_plan_full.json), without anyone playing the menus. It does nothing unless a script
// asks.
//
// The script (tools/shader_survey.js) finds this struct in RDRAM by its two magic words. `newfile` set at the file
// menu makes an Agent file and saves it to the Game Pak as New Agent's name and location do (gamefile_load_defaults,
// then filemgr_save_game_to_device's save), taken once the menu has read the Game Pak, has room on it and the pak
// answers to its serial (before that the save fails without trying), so the game has a file to save each mission's
// end to, as a player's has; `newfileresult` is then 1 if it saved, -1 if not, and `pakerror` the save's error
// (g_FilemgrLastPakError). It writes the stage's index in the mission list (g_SoloStages, 0 is dataDyne Central -
// Defection) into `stageindex` and then `request`; `loaded` counts the requests taken, so it knows one was, and
// `target` is the stage the last one started. main_tick
// (patches/frame_rate.c) calls shader_survey_tick once a frame on the game's main thread, so a request is taken between
// frames: the mission is set up on Agent, alone, and started as mission select's Accept does
// (menuhandler_accept_mission), the game's own way into a level. `invincible` set keeps
// every player alive as the Invincible cheat does. `complete` set ends the mission in a level as its own script does
// when the player has done what it asks (func0000e990, ai_end_level's: the objectives checked, then the stage ended),
// with every objective counted complete (debug_force_all_objectives_complete, the debug build's switch, below) until
// the next mission starts: the endscreen says Mission Completed, the file records it and Continue goes on to the next
// mission, as after a player's. `completed` counts the ends taken. Every frame the struct also carries the stage
// playing, how many stages have started (`loads`: the title's intro plays in Defection's stage, so the stage alone
// doesn't say a request started it), whether the player is in control (`incontrol`: in a level, its own view, no
// cutscene) and the level's clock (g_Vars.lvframe240, 240 a second of the game's own time), which the plans hold
// their waits on. A request taken during the title's intro is lost: the title goes on to the file menu (stage
// STAGE_CITRAINING), where a script asks.
//
// For the Combat Simulator (a pads plan's split screen, the radar): `mpmenu` set at the file menu opens the Combat
// Simulator's menu as the main menu's Combat Simulator does (menuhandler_main_menu_combat_simulator), where each
// controller's Start joins its player, the game's own way ("Player 2: Ready!", g_Vars.waitingtojoin, until the menu
// opens a panel of its own for it); `menuroot` carries the menu's root (MENUROOT_MPSETUP, 3, there; -1 with no menu)
// and `chrslots` a bit each player in (0x1 player 1, 0x2 player 2): g_MpSetup.chrslots' players and those waiting to
// join. A stage written to `mpstage` (STAGE_MP_SKEDAR, 0x32) then starts a combat match there for those players, or
// for `mpslots`' players where it isn't 0 (a desktop build with only a keyboard plays split screen that way), as the menu
// does once every player is ready (menutick's MENUROOT_START_MP_MATCH), and `mpstarted` counts the starts taken.
//
// `hudmsg` set while the player is in control shows a HUD message of the default type, at the left edge, as the level's
// "One minute left." does (lv.c), so a plan can look at one where the window is wider than 4:3.

#include "patches.h"
#include "game/debug.h"
#include "game/filemgr.h"
#include "game/gamefile.h"
#include "game/hudmsg.h"
#include "game/lang.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "game/pak.h"
#include "lib/main.h"
#include "bss.h"
#include "data.h"

extern s32 g_StageNum;
extern struct fileguid g_GameFileGuid;

struct ShaderSurvey {
	u32 magic[2];
	volatile u32 loaded;
	volatile s32 request;
	volatile s32 stageindex;
	volatile s32 invincible;
	volatile s32 stagenum;
	volatile s32 lvframe240;
	volatile s32 target;
	volatile u32 loads;
	volatile s32 incontrol;
	volatile s32 complete;
	volatile u32 completed;
	volatile s32 newfile;
	volatile s32 newfileresult;
	volatile s32 pakerror;
	volatile s32 mpmenu;
	volatile s32 mpstage;
	volatile s32 mpslots;
	volatile u32 mpstarted;
	volatile s32 menuroot;
	volatile s32 chrslots;
	volatile s32 hudmsg;
};

struct ShaderSurvey shader_survey = { { 0x53484452, 0x53525659 }, 0, 0, 0, 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0 }; // "SHDR" "SRVY"

MenuItemHandlerResult menuhandler_main_menu_combat_simulator(s32 operation, struct menuitem *item, union handlerdata *data);

// The Combat Simulator's players: those in g_MpSetup.chrslots and those waiting to join, a bit each.
static s32 shader_survey_players(void)
{
	s32 players = g_MpSetup.chrslots & 0xf;
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.waitingtojoin[i]) {
			players |= 1 << i;
		}
	}

	return players;
}

static s32 shader_survey_forcecomplete = false;

// @recomp: false in the retail game; true from a script's end of a mission to the next mission's start.
RECOMP_PATCH bool debug_force_all_objectives_complete(void)
{
	return shader_survey_forcecomplete;
}

void shader_survey_tick(void)
{
	static const char name[] = "Survey";
	s32 i;

	if (shader_survey.newfile && g_FileLists[0] != NULL && g_FileLists[0]->spacesfree[SAVEDEVICE_GAMEPAK] > 0
			&& pak_find_by_serial(g_FileLists[0]->deviceguids[SAVEDEVICE_GAMEPAK].deviceserial) == SAVEDEVICE_GAMEPAK) {
		shader_survey.newfile = 0;
		gamefile_load_defaults(&g_GameFile);

		for (i = 0; i < sizeof(name); i++) {
			g_GameFile.name[i] = name[i];
		}

		g_GameFileGuid.fileid = g_FileLists[0]->deviceguids[SAVEDEVICE_GAMEPAK].fileid;
		g_GameFileGuid.deviceserial = g_FileLists[0]->deviceguids[SAVEDEVICE_GAMEPAK].deviceserial;
		shader_survey.newfileresult = filemgr_save_or_load(&g_GameFileGuid, FILEOP_SAVE_GAME_000, 0) ? 1 : -1;
		shader_survey.pakerror = g_FilemgrLastPakError;
	}

	if (shader_survey.request && shader_survey.stageindex >= 0 && shader_survey.stageindex < NUM_SOLOSTAGES) {
		shader_survey.request = 0;
		shader_survey_forcecomplete = false;
		g_MissionConfig.difficulty = DIFF_A;
		g_MissionConfig.pdmode = false;
		g_MissionConfig.iscoop = false;
		g_MissionConfig.isanti = false;
		g_MissionConfig.stageindex = shader_survey.stageindex;
		g_MissionConfig.stagenum = g_SoloStages[shader_survey.stageindex].stagenum;
		menuhandler_accept_mission(MENUOP_CONFIRM, NULL, NULL);
		shader_survey.target = g_MissionConfig.stagenum;
		shader_survey.loaded++;
	}

	if (shader_survey.mpmenu && g_StageNum == STAGE_CITRAINING && g_MenuData.count > 0) {
		shader_survey.mpmenu = 0;
		menuhandler_main_menu_combat_simulator(MENUOP_CONFIRM, NULL, NULL);
	}

	if (shader_survey.mpstage > 0 && g_MenuData.count > 0 && g_MenuData.root == MENUROOT_MPSETUP && g_MenuData.nextroot == -1) {
		g_MpSetup.stagenum = shader_survey.mpstage;
		g_MpSetup.scenario = MPSCENARIO_COMBAT;

		g_MpSetup.chrslots = (g_MpSetup.chrslots & ~0xf)
			| ((shader_survey.mpslots ? shader_survey.mpslots : shader_survey_players()) & 0xf);

		shader_survey.target = shader_survey.mpstage;
		shader_survey.mpstage = 0;
		menu_save_and_push_root_dialog(NULL, MENUROOT_START_MP_MATCH);
		shader_survey.mpstarted++;
	}

	if (shader_survey.complete && g_StageNum < STAGE_TITLE && g_Vars.currentplayer != NULL) {
		shader_survey.complete = 0;
		shader_survey_forcecomplete = true;
		func0000e990();
		shader_survey.completed++;
	}

	if (shader_survey.invincible && g_StageNum < STAGE_TITLE) {
		for (i = 0; i < MAX_PLAYERS; i++) {
			if (g_Vars.players[i]) {
				g_Vars.players[i]->invincible = 1;
			}
		}
	}

	// A stage started: the next mission's, by Continue on the endscreen as by a request, counts its own objectives.
	if (shader_survey.stagenum != g_StageNum) {
		shader_survey.loads++;
		shader_survey_forcecomplete = false;
	}

	shader_survey.stagenum = g_StageNum;
	shader_survey.incontrol = g_StageNum < STAGE_TITLE && !g_Vars.in_cutscene && g_Vars.currentplayer != NULL
		&& g_Vars.currentplayer->cameramode == CAMERAMODE_DEFAULT;
	shader_survey.lvframe240 = g_Vars.lvframe240;
	shader_survey.menuroot = g_MenuData.count > 0 ? g_MenuData.root : -1;
	shader_survey.chrslots = shader_survey_players();

	if (shader_survey.hudmsg && shader_survey.incontrol) {
		shader_survey.hudmsg = 0;
		hudmsg_create(lang_get(L_MISC_068), HUDMSGTYPE_DEFAULT); // "One minute left."
	}
}
