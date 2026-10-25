#ifndef PATCHES_H
#define PATCHES_H

// A function in .recomp_patch replaces the game's function of the same name (patches.toml's strict_patch_mode).
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))

#include <ultra64.h>
#include "constants.h"
#include "types.h"

// Natives (syms.ld, src/pd_natives.cpp).

// The segments syms/relocs.txt lists offsets in.
#define PD_SEG_LIB     0
#define PD_SEG_DATA    1
#define PD_SEG_INFLATE 2
#define PD_SEG_GAME    3

// Shifts the pointer words syms/relocs.txt lists in a segment, once the game has unzipped it into RDRAM, from the
// retail ROM's TLB-mapped addresses to the relink's.
void recomp_pd_relocate(s32 segment);
// Stops the program with a message.
void recomp_pd_crash(const char *message);
// The word of the player's ROM at an offset into it.
void recomp_pd_read_rom(u32 offset, u32 *dst);
// The runtime's osMotorInit: 0 when a Rumble Pak is in the controller, which then rumbles through the runtime.
s32 recomp_pd_motor_init(OSMesgQueue *mq, OSPfs *pfs, s32 channel);
// Whether the mouse plays (src/mouse.cpp): Mouse Sensitivity above 0% and the pointer locked to the game.
s32 recomp_pd_mouse_playing(void);
// The mouse's motion since the last take, in the PC port's units, right and down positive.
void recomp_pd_mouse_take(f32 *dx, f32 *dy);
// The wheel's notches since the last take, up positive.
s32 recomp_pd_mouse_take_wheel(void);
// Whether the mouse's aim is held: the right button, or a key bound to L or R (Left Shift), not a pad's L or R.
s32 recomp_pd_mouse_aim_held(void);
// Field of View (src/config.cpp): the angle the player's view is drawn with for one the game sets, in degrees.
f32 recomp_pd_widen_fov(f32 fovy);
// Aspect Ratio (src/config.cpp): the window's width over its height with Expand, at least the one given; with Original
// the one given.
f32 recomp_pd_get_aspect_ratio(f32 original);
// Whether render to RAM's copy into RDRAM lands a display list late (src/main.cpp): 1 on the page, 0 on the desktop.
s32 recomp_pd_render_to_ram_late(void);
// Whether the game's ticks are held to every other VI (src/pd_natives.cpp, patches/frame_rate.c): called once a tick,
// which it counts. On the page while the game can't keep 60 up, never on the desktop.
s32 recomp_pd_hold_at_30(void);

#endif
