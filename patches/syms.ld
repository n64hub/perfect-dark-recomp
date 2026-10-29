/* Native functions the patches call (src/pd_natives.cpp): N64Recomp turns a call to one of these absolute addresses
 * into a call to the C function of the same name (patches.toml, use_absolute_symbols). */
recomp_pd_relocate = 0x8F000000;
recomp_pd_crash = 0x8F000004;
recomp_pd_read_rom = 0x8F000008;
recomp_pd_motor_init = 0x8F00000C;
recomp_pd_mouse_playing = 0x8F000010;
recomp_pd_mouse_take = 0x8F000014;
recomp_pd_mouse_take_wheel = 0x8F000018;
recomp_pd_widen_fov = 0x8F00001C;
recomp_pd_get_aspect_ratio = 0x8F000020;
recomp_pd_mouse_aim_held = 0x8F000024;
recomp_pd_render_to_ram_late = 0x8F000028;
recomp_pd_hold_at_30 = 0x8F00002C;
recomp_pd_crouch_take = 0x8F000030;
recomp_pd_keyboard_port = 0x8F000034;
