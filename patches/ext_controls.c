// Ext, a ninth control style, as the decompilation's PC port (fgsfdsfgs/perfect_dark) has it: a modern pad's two
// sticks and its buttons as a PC shooter plays them. The game's side; src/ext_controls.cpp is the program's.
//  - Where it is kept: in the player's control style (g_PlayerConfigsArray's controlmode) as CONTROLMODE_EXT, a value
//    the console's game never sees: options_get_control_mode gives every reader 1.2 for it (its stick looks and turns,
//    its C buttons walk and strafe), so the CamSpy, the hoverbike, the radial menu and the menus' picture of the pad
//    play it as 1.2, and an agent file saves 1.2 in its 3 bits, whose 8 values the other styles fill. Ext itself is a
//    bit the game file has spare (GAMEFILEFLAG_P1_EXT, _P2_EXT: 0x25-0x28 are never set or read) and, for a Combat
//    Simulator player's file, OPTION_0100 cleared, a bit the game sets on every player it makes and never reads. So a
//    file stays one a console loads, as 1.2 or the player file's 1.1.
//  - New agent files and new Combat Simulator players start on Ext (pak_clear_all_bitflags, mp_player_set_defaults);
//    a file saved before Ext existed keeps its style.
//  - The Control Style menu lists it after 2.4, in a group of its own, and the Combat Simulator's after 1.4.
//  - While an Ext player walks in a level the program plays their pad as the PC port does (src/ext_controls.cpp): the
//    right stick is the game's stick, so it looks and turns as 1.2's does, the left stick walks and strafes, read here
//    each tick (recomp_pd_ext_take) and added where the game hands her movement over (bmove_apply_move_data) as the
//    game's 2.x styles add their second controller's stick: strafing always, walking unless she aims. RT is Z (fire),
//    LT is R (aim), A and RB are B (use, reload, and held or with RT the gun's other function), LB holds A, so it opens
//    the radial menu at once (ext_tick_begin) and keeps it open while held, and the d-pad is the C buttons. X reloads,
//    Y and B change to the next and previous weapon, and a click of the left stick crouches (ext_tick_end), each
//    pressed since the last tick. The keyboard plays its keys as the 1.1 they were laid out for: its stick's W and S
//    walk and its arrows turn (the program keeps them so), A and D strafe as 1.2's C buttons, and the up and down
//    arrows, C up and down, walk too, where 1.1 looks; the mouse looks (patches/mouse.c).

#include "patches.h"
#include "game/activemenu.h"
#include "game/bondbike.h"
#include "game/bondgrab.h"
#include "game/bondgun.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/challenge.h"
#include "game/gamefile.h"
#include "game/lang.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "game/pak.h"
#include "game/savebuffer.h"
#include "bss.h"
#include "data.h"

#define CONTROLMODE_EXT 8
// The Combat Simulator's list of styles has 1.1-1.4 and then Ext.
#define MP_CONTROLSTYLE_EXT 4

#define GAMEFILEFLAG_P1_EXT 0x25
#define GAMEFILEFLAG_P2_EXT 0x26

// What src/ext_controls.cpp's recomp_pd_ext_take says was pressed since the last take.
#define EXT_RELOAD     0x01
#define EXT_NEXTWEAPON 0x02
#define EXT_PREVWEAPON 0x04
#define EXT_RADIAL     0x08
#define EXT_CROUCH     0x10

void crouch_apply(s32 presses, bool allowc1buttons);
void mp_init_handicaps(s32 playernum);
s32 mp_get_mpheadnum_by_mpbodynum(s32 bodynum);
s32 mp_get_num_heads2(void);
void mp_calculate_player_title(struct mpplayerconfig *mpplayer);
void mpplayerfile_load_gun_funcs(struct savebuffer *buffer, s32 playernum);

// The current player's tick: whether Ext plays them as they walk, the left stick, and what was pressed.
static bool s_ExtWalking;
static s32 s_ExtMoveX;
static s32 s_ExtMoveY;
static s32 s_ExtPressed;

static bool ext_plays(s32 mpindex)
{
	return g_PlayerConfigsArray[mpindex].controlmode == CONTROLMODE_EXT;
}

RECOMP_PATCH s32 options_get_control_mode(s32 mpchrnum)
{
	// @recomp: Ext reads as 1.2 everywhere but here.
	return ext_plays(mpchrnum) ? CONTROLMODE_12 : g_PlayerConfigsArray[mpchrnum].controlmode;
}

// The game file's Ext bit of a solo, co-op or counter-op player: player 1's at mpindex 4, or 0 in co-op and
// counter-op, player 2's at 5 or 1, as gamefile_apply_options reads them.
static s32 ext_gamefile_flag(s32 mpindex)
{
	return (mpindex == 4 || mpindex == 0) ? GAMEFILEFLAG_P1_EXT : GAMEFILEFLAG_P2_EXT;
}

// Its only caller is gamefile_load_defaults, which then sets the defaults' flags and applies them: so this is where a
// new agent file's players are made Ext (gamefile_apply_options, below).
RECOMP_PATCH void pak_clear_all_bitflags(u8 *flags)
{
	s32 i;

	for (i = 0; i <= GAMEFILEFLAG_4E; i++) {
		pak_set_bitflag(i, flags, false);
	}

	// @recomp
	pak_set_bitflag(GAMEFILEFLAG_P1_EXT, flags, true);
	pak_set_bitflag(GAMEFILEFLAG_P2_EXT, flags, true);
}

RECOMP_PATCH void gamefile_apply_options(struct gamefile *file)
{
	s32 player1 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 0 : 4;
	s32 player2 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 1 : 5;

	options_set_forward_pitch(player1, pak_has_bitflag(GAMEFILEFLAG_P1_FORWARDPITCH, file->flags));
	options_set_autoaim(player1, pak_has_bitflag(GAMEFILEFLAG_P1_AUTOAIM, file->flags));
	options_set_aim_control(player1, pak_has_bitflag(GAMEFILEFLAG_P1_AIMCONTROL, file->flags));
	options_set_sight_on_screen(player1, pak_has_bitflag(GAMEFILEFLAG_P1_SIGHTONSCREEN, file->flags));
	options_set_look_ahead(player1, pak_has_bitflag(GAMEFILEFLAG_P1_LOOKAHEAD, file->flags));
	options_set_ammo_on_screen(player1, pak_has_bitflag(GAMEFILEFLAG_P1_AMMOONSCREEN, file->flags));
	options_set_head_roll(player1, pak_has_bitflag(GAMEFILEFLAG_P1_HEADROLL, file->flags));
	options_set_show_gun_function(player1, pak_has_bitflag(GAMEFILEFLAG_P1_SHOWGUNFUNCTION, file->flags));
	options_set_always_show_target(player1, pak_has_bitflag(GAMEFILEFLAG_P1_ALWAYSSHOWTARGET, file->flags));
	options_set_show_zoom_range(player1, pak_has_bitflag(GAMEFILEFLAG_P1_SHOWZOOMRANGE, file->flags));
	options_set_show_mission_time(player1, pak_has_bitflag(GAMEFILEFLAG_P1_SHOWMISSIONTIME, file->flags));
	options_set_paintball(player1, pak_has_bitflag(GAMEFILEFLAG_P1_PAINTBALL, file->flags));

	options_set_forward_pitch(player2, pak_has_bitflag(GAMEFILEFLAG_P2_FORWARDPITCH, file->flags));
	options_set_autoaim(player2, pak_has_bitflag(GAMEFILEFLAG_P2_AUTOAIM, file->flags));
	options_set_aim_control(player2, pak_has_bitflag(GAMEFILEFLAG_P2_AIMCONTROL, file->flags));
	options_set_sight_on_screen(player2, pak_has_bitflag(GAMEFILEFLAG_P2_SIGHTONSCREEN, file->flags));
	options_set_look_ahead(player2, pak_has_bitflag(GAMEFILEFLAG_P2_LOOKAHEAD, file->flags));
	options_set_ammo_on_screen(player2, pak_has_bitflag(GAMEFILEFLAG_P2_AMMOONSCREEN, file->flags));
	options_set_head_roll(player2, pak_has_bitflag(GAMEFILEFLAG_P2_HEADROLL, file->flags));
	options_set_show_gun_function(player2, pak_has_bitflag(GAMEFILEFLAG_P2_SHOWGUNFUNCTION, file->flags));
	options_set_always_show_target(player2, pak_has_bitflag(GAMEFILEFLAG_P2_ALWAYSSHOWTARGET, file->flags));
	options_set_show_zoom_range(player2, pak_has_bitflag(GAMEFILEFLAG_P2_SHOWZOOMRANGE, file->flags));
	options_set_show_mission_time(player2, pak_has_bitflag(GAMEFILEFLAG_P2_SHOWMISSIONTIME, file->flags));
	options_set_paintball(player2, pak_has_bitflag(GAMEFILEFLAG_P2_PAINTBALL, file->flags));

	options_set_in_game_subtitles(pak_has_bitflag(GAMEFILEFLAG_INGAMESUBTITLES, file->flags));
	options_set_cutscene_subtitles(pak_has_bitflag(GAMEFILEFLAG_CUTSCENESUBTITLES, file->flags));

	// Duplicate
	options_set_paintball(player2, pak_has_bitflag(GAMEFILEFLAG_P2_PAINTBALL, file->flags));

	// @recomp: the file's Ext bits make its players Ext, whatever style its 3 bits read as (1.2, saved for Ext).
	if (pak_has_bitflag(GAMEFILEFLAG_P1_EXT, file->flags)) {
		options_set_control_mode(player1, CONTROLMODE_EXT);
	}

	if (pak_has_bitflag(GAMEFILEFLAG_P2_EXT, file->flags)) {
		options_set_control_mode(player2, CONTROLMODE_EXT);
	}
}

RECOMP_PATCH MenuItemHandlerResult menuhandler_control_style_impl(s32 operation, struct menuitem *item, union handlerdata *data, s32 mpindex)
{
	u16 categories[] = {
		L_OPTIONS_237, // "Single"
		L_OPTIONS_238, // "Double"
	};

	if (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) {
		mpindex = g_Vars.currentplayerstats->mpindex;
	}

	switch (operation) {
	case MENUOP_GET_OPTION_COUNT:
		// @recomp: and Ext
		data->list.value = 9;
		break;
	case MENUOP_GET_OPTGROUP_COUNT:
		data->list.value = 3;
		break;
	case MENUOP_GET_OPTION_TEXT:
		if (data->list.value == CONTROLMODE_EXT) {
			return (s32) "Ext";
		}
		return (s32) lang_get(g_ControlStyleOptions[data->list.value]);
	case MENUOP_GET_OPTGROUP_TEXT:
		if (data->list.value == 2) {
			return (s32) "Modern";
		}
		return (s32) lang_get(categories[data->list.value]);
	case MENUOP_GET_OPTGROUP_START_INDEX:
		data->list.groupstartindex = data->list.value * 4;
		break;
	case MENUOP_CONFIRM:
		options_set_control_mode(mpindex, data->list.value);
		// @recomp: Ext's bit in the game file, which keeps it
		pak_set_bitflag(ext_gamefile_flag(mpindex), g_GameFile.flags, data->list.value == CONTROLMODE_EXT);
		g_Vars.modifiedfiles |= MODFILE_GAME;
		break;
	case MENUOP_GET_SELECTED_INDEX:
		// @recomp: Ext's own entry, not 1.2's
		data->list.value = g_PlayerConfigsArray[mpindex].controlmode;
		g_Menus[g_MpPlayerNum].main.mpindex = mpindex;
		break;
	case MENUOP_ON_OPTION_FOCUS:
		if (g_MenuData.root == MENUROOT_MAINMENU) {
			// @recomp: the picture of the pad has no Ext, so it shows 1.2's
			g_Menus[g_MpPlayerNum].main.controlmode = data->list.value == CONTROLMODE_EXT ? CONTROLMODE_12 : data->list.value;
		}
		break;
	}

	return 0;
}

// A Combat Simulator player's style and their file's Ext bit (OPTION_0100 cleared).
static void ext_set_mp_control_mode(s32 playernum, s32 mode)
{
	g_PlayerConfigsArray[playernum].controlmode = mode;

	if (mode == CONTROLMODE_EXT) {
		g_PlayerConfigsArray[playernum].options &= ~OPTION_0100;
	} else {
		g_PlayerConfigsArray[playernum].options |= OPTION_0100;
	}
}

RECOMP_PATCH MenuItemHandlerResult menuhandler_mp_control_style(s32 operation, struct menuitem *item, union handlerdata *data)
{
	u16 labels[] = {
		L_OPTIONS_239, // "1.1"
		L_OPTIONS_240, // "1.2"
		L_OPTIONS_241, // "1.3"
		L_OPTIONS_242, // "1.4"
	};

	switch (operation) {
	case MENUOP_GET_OPTION_COUNT:
		// @recomp: and Ext
		data->dropdown.value = 5;
		break;
	case MENUOP_GET_OPTION_TEXT:
		if (data->dropdown.value == MP_CONTROLSTYLE_EXT) {
			return (s32) "Ext";
		}
		return (s32) lang_get(labels[data->dropdown.value]);
	case MENUOP_CONFIRM:
		ext_set_mp_control_mode(g_MpPlayerNum,
				data->dropdown.value == MP_CONTROLSTYLE_EXT ? CONTROLMODE_EXT : data->dropdown.value);
		break;
	case MENUOP_GET_SELECTED_INDEX:
		data->dropdown.value = ext_plays(g_MpPlayerNum) ? MP_CONTROLSTYLE_EXT : options_get_control_mode(g_MpPlayerNum);
		break;
	}

	return 0;
}

RECOMP_PATCH void mp_player_set_defaults(s32 playernum, bool autonames)
{
	s32 i;
	s32 j;

	mp_init_handicaps(playernum);

	g_PlayerConfigsArray[playernum].controlmode = CONTROLMODE_11;

	g_PlayerConfigsArray[playernum].options = OPTION_LOOKAHEAD
		| OPTION_SIGHTONSCREEN
		| OPTION_AUTOAIM
		| OPTION_AMMOONSCREEN
		| OPTION_SHOWGUNFUNCTION
		| OPTION_HEADROLL
		| OPTION_0100
		| OPTION_ALWAYSSHOWTARGET
		| OPTION_SHOWZOOMRANGE;

	// @recomp: a new player plays Ext
	ext_set_mp_control_mode(playernum, CONTROLMODE_EXT);

	g_PlayerConfigsArray[playernum].handicap = 128;

	switch (playernum) {
	case 0:
	default:
		g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_DARK_COMBAT;
		break;
	case 1:
		g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CASSANDRA;
		break;
	case 2:
		g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CARRINGTON;
		break;
	case 3:
		g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CILABTECH;
		break;
	}

	g_PlayerConfigsArray[playernum].base.mpheadnum = mp_get_mpheadnum_by_mpbodynum(g_PlayerConfigsArray[playernum].base.mpbodynum);
	g_PlayerConfigsArray[playernum].base.displayoptions = MPDISPLAYOPTION_RADAR | MPDISPLAYOPTION_HIGHLIGHTTEAMS;
	g_PlayerConfigsArray[playernum].fileguid.fileid = 0;
	g_PlayerConfigsArray[playernum].fileguid.deviceserial = 0;

	if (autonames) {
		// "Player 1" etc
		sprintf(g_PlayerConfigsArray[playernum].base.name, "%s %d\n", lang_get(L_MISC_437), playernum + 1);
	} else {
		g_PlayerConfigsArray[playernum].base.name[0] = '\0';
	}

	g_PlayerConfigsArray[playernum].kills = 0;
	g_PlayerConfigsArray[playernum].deaths = 0;
	g_PlayerConfigsArray[playernum].gamesplayed = 0;
	g_PlayerConfigsArray[playernum].gameswon = 0;
	g_PlayerConfigsArray[playernum].gameslost = 0;
	g_PlayerConfigsArray[playernum].time = 0;
	g_PlayerConfigsArray[playernum].distance = 0;
	g_PlayerConfigsArray[playernum].accuracy = 1000;
	g_PlayerConfigsArray[playernum].damagedealt = 0;
	g_PlayerConfigsArray[playernum].painreceived = 0;
	g_PlayerConfigsArray[playernum].headshots = 0;
	g_PlayerConfigsArray[playernum].ammoused = 0;
	g_PlayerConfigsArray[playernum].accuracymedals = 0;
	g_PlayerConfigsArray[playernum].headshotmedals = 0;
	g_PlayerConfigsArray[playernum].killmastermedals = 0;
	g_PlayerConfigsArray[playernum].survivormedals = 0;
	g_PlayerConfigsArray[playernum].title = MPPLAYERTITLE_BEGINNER;

	if (playernum < MAX_PLAYERS) {
		for (i = 0; i < ARRAYCOUNT(g_MpChallenges); i++) {
			for (j = 1; j <= MAX_PLAYERS; j++) {
				challenge_set_completed_by_player_with_num_players(playernum, i, j, false);
			}
		}

		challenge_determine_unlocked_features();
	}

	for (i = 0; i < ARRAYCOUNT(g_PlayerConfigsArray); i++) {
		g_PlayerConfigsArray[playernum].gunfuncs[i] = 0;
	}
}

RECOMP_PATCH void mpplayerfile_load_wad(s32 playernum, struct savebuffer *buffer, bool arg2)
{
	struct fileguid guid;
	u32 stack;
	s32 i;
	s32 j;

	savebuffer_read_string(buffer, g_PlayerConfigsArray[playernum].base.name, 1);
	g_PlayerConfigsArray[playernum].time = savebuffer_read_bits(buffer, 28);

	if (arg2) {
		g_PlayerConfigsArray[playernum].base.mpheadnum = savebuffer_read_bits(buffer, 7);
		g_PlayerConfigsArray[playernum].base.mpbodynum = savebuffer_read_bits(buffer, 7);

		savebuffer_read_guid(buffer, &guid);

		if (g_PlayerConfigsArray[playernum].base.mpheadnum >= mp_get_num_heads2()) {
			if (guid.fileid != 0 && guid.deviceserial != 0) {
				if (g_MenuData.lastperfectheadfile < 11) {
					g_MenuData.lastperfectheadfile++;
					g_MenuData.perfectheadfiles[g_MenuData.lastperfectheadfile].fileguid.fileid = guid.fileid;
					g_MenuData.perfectheadfiles[g_MenuData.lastperfectheadfile].fileguid.deviceserial = guid.deviceserial;
					g_MenuData.perfectheadfiles[g_MenuData.lastperfectheadfile].playernum1 = g_MpPlayerNum;
					g_MenuData.perfectheadfiles[g_MenuData.lastperfectheadfile].playernum2 = g_MpPlayerNum;
				}
			} else {
				g_PlayerConfigsArray[playernum].base.mpheadnum = MPHEAD_DARK_COMBAT;
			}
		}
	} else {
		savebuffer_read_bits(buffer, 7);
		savebuffer_read_bits(buffer, 7);
		savebuffer_read_guid(buffer, &guid);
	}

	g_PlayerConfigsArray[playernum].base.displayoptions = savebuffer_read_bits(buffer, 8);
	g_PlayerConfigsArray[playernum].kills = savebuffer_read_bits(buffer, 20);
	g_PlayerConfigsArray[playernum].deaths = savebuffer_read_bits(buffer, 20);
	g_PlayerConfigsArray[playernum].gamesplayed = savebuffer_read_bits(buffer, 19);
	g_PlayerConfigsArray[playernum].gameswon = savebuffer_read_bits(buffer, 19);
	g_PlayerConfigsArray[playernum].gameslost = savebuffer_read_bits(buffer, 19);
	g_PlayerConfigsArray[playernum].distance = savebuffer_read_bits(buffer, 25);
	g_PlayerConfigsArray[playernum].accuracy = savebuffer_read_bits(buffer, 10);
	g_PlayerConfigsArray[playernum].damagedealt = savebuffer_read_bits(buffer, 26);
	g_PlayerConfigsArray[playernum].painreceived = savebuffer_read_bits(buffer, 26);
	g_PlayerConfigsArray[playernum].headshots = savebuffer_read_bits(buffer, 20);
	g_PlayerConfigsArray[playernum].ammoused = savebuffer_read_bits(buffer, 30);
	g_PlayerConfigsArray[playernum].accuracymedals = savebuffer_read_bits(buffer, 18);
	g_PlayerConfigsArray[playernum].headshotmedals = savebuffer_read_bits(buffer, 18);
	g_PlayerConfigsArray[playernum].killmastermedals = savebuffer_read_bits(buffer, 18);
	g_PlayerConfigsArray[playernum].survivormedals = savebuffer_read_bits(buffer, 16);
	g_PlayerConfigsArray[playernum].controlmode = savebuffer_read_bits(buffer, 2);
	g_PlayerConfigsArray[playernum].options = savebuffer_read_bits(buffer, 12);

	// @recomp: OPTION_0100 cleared is Ext, whose 2 bits were saved as 1.1 (the game saves the style as it is kept)
	if ((g_PlayerConfigsArray[playernum].options & OPTION_0100) == 0) {
		g_PlayerConfigsArray[playernum].controlmode = CONTROLMODE_EXT;
	}

	for (i = 0; i < ARRAYCOUNT(g_MpChallenges); i++) {
		for (j = 1; j < MAX_PLAYERS + 1; j++) {
			challenge_set_completed_by_player_with_num_players(playernum, i, j, savebuffer_read_bits(buffer, 1));
		}
	}

	challenge_determine_unlocked_features();
	mp_calculate_player_title(&g_PlayerConfigsArray[playernum]);
	mpplayerfile_load_gun_funcs(buffer, playernum);
}

// Before bmove_process_input, from bmove_tick (patches/mouse.c): whether Ext plays the current player as they walk,
// which tells the program to play their pad as Ext's (recomp_pd_ext_take), the left stick, and what was pressed. A
// press of LB opens the radial menu, where the game's A (which LB holds) opens it once held for a quarter of a second
// and cycles weapon if let go sooner; invdowntime -1 is the game's own for an A that has already done its part.
void ext_tick_begin(bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	s32 port = options_get_contpad_num1(g_Vars.currentplayerstats->mpindex);
	s32 movex = 0;
	s32 movey = 0;

	s_ExtWalking = ext_plays(g_Vars.currentplayerstats->mpindex)
		&& player->bondmovemode == MOVEMODE_WALK
		&& !player->isdead
		&& player->pausemode == PAUSEMODE_UNPAUSED
		&& !(player->eyespy && player->eyespy->active);
	s_ExtPressed = recomp_pd_ext_take(port, s_ExtWalking, &movex, &movey);

	// The game's dead zone for a 2.x style's second stick.
	s_ExtMoveX = movex < -5 ? movex + 5 : movex > 5 ? movex - 5 : 0;
	s_ExtMoveY = movey < -5 ? movey + 5 : movey > 5 ? movey - 5 : 0;

	if (!s_ExtWalking || !allowc1buttons || g_Vars.tickmode == TICKMODE_CUTSCENE) {
		s_ExtPressed = 0;
		return;
	}

	if ((s_ExtPressed & EXT_RADIAL) && player->activemenumode == AMMODE_CLOSED) {
		am_open();
		player->invdowntime = -1;
	}
}

// After bmove_process_input: what X, Y, B and the left stick's click do, where and when the game's own buttons would.
void ext_tick_end(bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	s32 pressed = s_ExtPressed;

	s_ExtWalking = false;
	s_ExtPressed = 0;

	if (pressed == 0 || g_Vars.lvframenum < 10 || player->activemenumode != AMMODE_CLOSED) {
		return;
	}

	if (pressed & EXT_RELOAD) {
		bgun_reload_if_possible(HAND_RIGHT);
		bgun_reload_if_possible(HAND_LEFT);
	}

	if (pressed & EXT_NEXTWEAPON) {
		bgun_cycle_forward();
	}

	if (pressed & EXT_PREVWEAPON) {
		bgun_cycle_back();
	}

	if (pressed & EXT_CROUCH) {
		crouch_apply(1, allowc1buttons);
	}
}

RECOMP_PATCH void bmove_apply_move_data(struct movedata *data)
{
	// @recomp: Ext's left stick, as a 2.x style's second stick moves her (bmove_process_input): it strafes, aiming or
	// not, and walks unless she aims, where the game's walk reads its stick only while it may look ahead. The d-pad's
	// and the keyboard's steps (1.2's C buttons) go first.
	if (s_ExtWalking && g_Vars.tickmode != TICKMODE_AUTOWALK && g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		if (s_ExtMoveX != 0 && !data->digitalstepleft && !data->digitalstepright) {
			data->analogstrafe = s_ExtMoveX;
			data->unk14 = true;
		}

		if (s_ExtMoveY != 0 && !data->digitalstepforward && !data->digitalstepback
				&& !g_Vars.currentplayer->insightaimmode) {
			data->analogwalk = s_ExtMoveY;
			data->canlookahead = true;
		}
	}

	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbike_apply_move_data(data);
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrab_apply_move_data(data);
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalk_apply_move_data(data);
	}
}
