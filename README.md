# Perfect Dark: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A port of the N64 version of *Perfect Dark* (USA, revision 1) to PC and to the web, made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) +
[RT64](https://github.com/rt64/rt64)): the game's own MIPS code translated to C and run natively, with RT64 drawing
it. Its symbols come from the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) (MIT), pinned
at `lib/perfect_dark`. It is not an emulator and it contains no game data: you supply your own ROM.

**Status: the desktop build plays a mission.** With a desktop GPU,
*dataDyne Central - Defection* plays from its briefing to its end screen on Agent, with sound. The game's menus work:
the agent file, the Perfect Menu, mission select, the briefing, retry and next mission. A save survives a restart: the
agent file, the completed mission and the next one it unlocks. The game draws 59-60 frames a second in its levels,
every frame one VI, with its clock at real time, where its own controller reads held it to 30 (a patch puts off the
tick that would restart them, [docs/web.md](docs/web.md), Frame rate). RT64 interpolates to the display's rate (see
[docs/building.md](docs/building.md) for the build and its findings). On Windows (clang-cl, drawing through D3D12)
it builds and plays too: the launcher, Load ROM, Start Game, a new agent file and Defection with sound, and the save
is there at the next start. Releases for Windows (a zip: unzip it and run `PerfectDarkRecompiled.exe` from that
folder) and Linux on x86-64 (a `.tar.gz`: extract it and run `PerfectDarkRecompiled` in that folder) are on the
[Releases page](https://github.com/n64hub/perfect-dark-recomp/releases), without the ROM. The Linux build needs
glibc 2.35 or newer (Ubuntu 22.04, Debian 12 or later), a Vulkan driver, and GTK 3 and FreeType, which desktop
distributions have; it brings its own SDL2. Or build it as docs/building.md says.

Still wrong or not yet checked:

- On Linux under WSL2 the GPU device was lost twice through Mesa's Dozen, once after 22 minutes of play, once in a
  sweep through missions' starts that left the program stuck in the host's GPU driver and froze the whole Windows
  host. So the Linux build runs on the CPU (lavapipe) only; whether Dozen or the game's drawing caused it isn't known,
  and the Windows build, which draws through D3D12 without Dozen, hasn't been played that long.
- Speech (MP3): guards' lines start and play to their end in the game's MP3 player, but nobody has listened to them.
- Clouds and water draw through a patch (the game draws them in a way RT64 doesn't), seen in Villa and Chicago but
  not compared with a console's.
- Rumble hasn't been felt on a pad, the Windows build's sound has only been written to a file (SDL's disk driver),
  not heard through a device, and it hasn't been played longer than a few minutes.
- The enhancements were played on the desktop build in Defection at 1280x720, each on and off: the level drawn
  to the window's edges, the ammo at its side, the mouse turning, tilting, aiming and changing weapon, and Field of
  View. Not yet looked at with Expand: the HUD's messages and the mission timer at the left edge, the radar, the
  fades and the cutscene bars. On the page the mouse hasn't been played: the plays are headless, and headless
  Chrome refuses the pointer lock.

**The web page plays at [n64hub.com/perfect-dark](https://n64hub.com/perfect-dark/)**,
from your own ROM through its Load ROM ([docs/web.md](docs/web.md)). In Chrome and Firefox it
boots with no WebGPU errors, builds its shaders from a prebuilt list with a section a mission, and plays Defection,
Villa and Chicago, each started by a script, its sound playing.

## Modern enhancements

Each is an option in the settings menu (its General and Graphics tabs), and each has an Original value that plays as
the console does. They are the game's own code (`patches/`, `src/`), taken from what the decompilation's PC port
([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)) and GoldenEye's recompilation offer:

- **High resolution and smooth motion**: RT64 draws at the window's size with 2× MSAA instead of 320×220, and
  interpolates the game's frames up to the display's refresh rate.
- **Widescreen**: the 3D widens to the window's aspect ratio, and the game draws what the wider view shows: rooms,
  props and guards past the 4:3 edges aren't culled or cut off. The mission timer, the HUD's messages, the ammo and
  the radar move out to the window's edges; the crosshair and the gun stay centred. Split screen stays 4:3. The
  game's own 16:9 setting (a squeezed picture for a widescreen TV) reads as Normal while Expand widens, in its menu
  and in a game file saved then too.
- **Field of view**: Joanna sees 70 to 100 degrees from the top of the screen to the bottom, where the game shows 60.
  A gun's zoom and the Farsight magnify as much as ever, and the gun in her hand is drawn at the game's angle, as the
  PC port draws it; cutscenes and the menus keep the game's angle.
- **Mouse look and aim**: the mouse turns Joanna and tilts her view, a count turning her 0.055 degrees at 50% (the
  PC port's default), finer when zoomed. Left click fires (Z), right click and Left Shift aim, as
  GoldenEye64Recomp-web's mouse aims: the crosshair shows and the weapons that zoom zoom, the wheel zooms the sniper
  rifle further, and she walks, strafes and turns as she does without aiming, the mouse turning her with the crosshair
  at the centre. The game's own aim mode, which a pad's R still holds, would stop her: its stick moves the crosshair.
  The wheel changes weapon. The game's Auto-Aim, which pulls the crosshair onto a target, and its Look Ahead, which
  levels the view as she walks, are off while the mouse plays, aiming or not, and the options again at 0%.
  The pointer is locked to the game while Mouse Sensitivity is above
  0%; the keyboard's defaults are a PC's, W and S walking, A and D strafing, the arrows turning, E or R using what
  is in front of her (a door, a console) and reloading otherwise (the game's B, which does both; held, it changes the
  gun's function), Left Shift aiming (R), Space changing weapon (A) and Q firing (Z). C crouches her to a squat and
  stands her back up, as GoldenEye64Recomp-web's Crouch does, the mouse playing or not: a key of its own that the
  Controls tab doesn't list.

Settings are saved as JSON in `%LOCALAPPDATA%\PerfectDarkRecompiled\` (`~/.config/PerfectDarkRecompiled/` on Linux).
For the game as it shipped, set every Original value:

| Enhancement | Option | Key | Default | Original |
|---|---|---|---|---|
| Widescreen 3D | Aspect Ratio | `graphics.json` `ar_option` | `"Expand"` | `"Original"` (4:3) |
| Widescreen HUD | HUD Placement | `graphics.json` `hr_option` | `"Clamp16x9"` (at most 16:9's edges; `"Expand"`: the window's) | `"Original"` |
| Field of view | Field of View | `graphics.json` `field_of_view` | `"Original"` (`"70"`, `"80"`, `"90"`, `"100"`) | `"Original"` (60°) |
| Mouse look and aim | Mouse Sensitivity | `general.json` `mouse_sensitivity` | `50` | `0` (no mouse) |
| Mouse look and aim | Invert Mouse Y | `general.json` `mouse_invert_y` | `"Off"` (`"On"`: up looks down) | `"Off"` |
| Rendering resolution | Resolution | `graphics.json` `res_option` | `"Auto"` (the window's size) | `"Original"` |
| Anti-aliasing | MS Anti-Aliasing | `graphics.json` `msaa_option` | `"MSAA2X"` | `"None"` |
| Smooth motion | Framerate | `graphics.json` `rr_option` | `"Display"` | `"Original"` |

Field of View is Original by default, as the game was drawn for its 60 degrees and Expand already widens the view.

Left out:

- **A game rate above 60, or uncapped**: the game already ticks up to 60 a second (it counts each frame's length in
  VIs), and RT64 interpolates above that. Ticking faster needs the PC port's fixes to footsteps, automatic fire and
  doors, which assume at most 60.
- **Mouse in the menus, on the hoverbike, the CamSpy and the Slayer's rocket**, and a reload key apart from use: the
  game's buttons do those (the Controls tab binds them).
- **Widescreen split screen**, and the PC port's smaller options (crosshair sway, gun position, the extra HUD
  scaling).

## The ROM

*Perfect Dark (U) (V1.1)*, the USA release's second revision (`NPDE`, version 01), as a big-endian `.z64` of 32 MiB
with MD5 `e03b088b6ac9e0080440efed07c1e40f` (the decompilation's `ntsc-final`). Other regions and revisions are not
supported.

## License

GPL-3.0 ([LICENSE](LICENSE)). The decompilation at `lib/perfect_dark` is a submodule under its own MIT license.
