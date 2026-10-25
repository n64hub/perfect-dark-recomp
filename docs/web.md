# The web page

The game plays in a browser: built with Emscripten on N64Recomp-browser's `frontend/` into
`PerfectDarkRecompiled.html` (the launcher, the recomp's menus, SDL2's audio and keyboard, the ROM from Load ROM, and
the saves kept in IndexedDB; `lib/N64Recomp-browser/frontend/CMakeLists.txt`'s header says what each part does), drawn
through WebGPU. The page is built from your ROM like the desktop program, so it is never built in CI and never
published with the game in it.

What is the game's own stays here: the `EMSCRIPTEN` branches of `CMakeLists.txt` that make the game a page,
`tools/build_web.sh`, the prebuilt shader list (`src/rt64_shaders.txt`, recorded by `tools/shader_plan.json`; the
list's header says how to record it again), the play plans (`tools/play_plan.json`, `tools/play_plan_extended.json`,
`tools/play_plan_full.json`) and the survey that drives them (`patches/shader_survey.c`, `tools/shader_survey.js`). The rest is
N64Recomp-browser's: `docs/porting.md` (WebGPU and the shaders, prebuilt shader lists, Emscripten), `docs/playing.md`
(playing and measuring a page), `docs/forks.md` and `docs/findings.md`. Every play of this game's page is a line in
N64Recomp-browser's `plays.jsonl` (game `pd`), never a record here.

## Build it

After `tools/build_decomp.sh` ([building.md](building.md)), on Linux with Docker:

```bash
tools/build_web.sh                  # [cpus] [cmake arguments]
```

It builds `build/web/PerfectDarkRecompiled.html` with its `.js`, `.wasm` (about 16 MB) and `.data` (about 1.4 MB: the
recomp's fonts and menus, nothing of the game) in `tools/docker/Dockerfile.web`, the image every N64Recomp-browser page
is built in, with ccache in `build/ccache-web`, so a rebuild after a patch takes under a minute. The script's header
says each step. Two things differ from the desktop build:

- The patches library (`patches/`, built for MIPS) is recompiled by the native N64Recomp that `tools/recompile.sh`
  builds, not the runtime's, which Emscripten would build for the browser. Its C, and the recompiled code's, is
  compiled with implicit declarations an error: a call to an undeclared function links to a trap in WebAssembly.
- RT64 builds its pipelines from `src/rt64_shaders.txt` (`RT64_PREBUILT_SHADER_LIST`), in sections: the boot's before
  the game starts, and a level's the first time the level draws.

Serve `build/web` over HTTP from localhost (any static server; the page brings its own `coi_serviceworker.js` for the
headers threads need) and open `PerfectDarkRecompiled.html` in Chrome or Firefox with WebGPU, then Load ROM.

## The survey

A play plan or a shader recording needs the game in each level without playing its menus by hand.
`patches/shader_survey.c` is a struct in RDRAM with two magic words, which `tools/shader_survey.js` finds from the page;
the script asks for a solo mission by writing its index, and the game starts it between frames as mission select's
Accept does. It also says which stage plays, whether the player is in control, and the level's clock, which the plans
hold their waits on (`["clock", "clock()", 240]`), and it keeps the player invincible. For the whole-game plan it
also makes an Agent file on the Game Pak at the file menu, as New Agent does, so each mission's end has a file to
save to, and ends a mission as completed, as the mission's own script does once its objectives are done, with every
objective counted complete (the debug build's `debug_force_all_objectives_complete`): the endscreen and Continue then
play as after a player's mission. Its header says each field. It does nothing unless a script asks.

## The plans

`tools/play_plan.json` (about 2 minutes) boots, waits for the prebuilt shaders, shoots the intro and the file menu,
starts *dataDyne Central - Defection* on Agent, skips its opening cutscene and plays a minute of the game's time in
it: walking, strafing, turning, firing and aiming. `tools/play_plan_extended.json` (about 4 minutes) plays Defection,
then *Villa* and *Chicago*. `tools/play_plan_full.json` (about 40 minutes, for a card that asks for the whole game)
plays all 21 solo missions on Agent in the game's order: each mission's opening cutscene to its end, the extended
plan's moves, then the survey's end, the endscreen, and Continue and Accept into the next mission, the game's own way
from one to the next; Skedar Ruins' Continue plays a minute of the credits, and the survey starts the four special
assignments from there (its header says each step). Played through in Chrome and Firefox on a desktop GPU, every
mission ends Completed with no WebGPU error or exception. Each mission's load has one gap of about 300 ms. The opening
cutscenes of Defection, Chicago, Infiltration and Skedar Ruins stall for 130-150 ms every few seconds. Each stall is
the game's own frame of 8 VIs, on the desktop build and a console too: each shot's start stops and restarts the
controllers' polling (N64Recomp-browser's `docs/findings.md`). Infiltration's cutscene presents only 36-65 a second
for about 50 s in Firefox (160-180 in Chrome, against 230 in the rest of the play). The levels themselves have at most
one gap over 100 ms a mission. Play them with N64Recomp-browser's `tools/play_page.mjs` (its `docs/playing.md`). They
press the keyboard's defaults (`src/mouse.cpp`): W and S walk, A and D strafe, the arrows turn, Q fires, Left Shift
aims and Space skips a cutscene.

## The enhancements

The page has the desktop program's options with the same defaults (README, Modern enhancements): Aspect Ratio Expand
with the game drawing to the window's edges, HUD Placement 16:9, Field of View Original, Mouse Sensitivity 50%. They
cost the page nothing measurable: in headless Chrome at 1256x645 on a desktop GPU, uncapped, the regular plan
presented 341.6 and 345.2 a second against 333.3 and 340.4 for the page without them, interleaved (N64Recomp-browser's
`plays.jsonl`, set `pd-ws1`), the GPU as busy, no shader missed. With Mouse Sensitivity above 0% the page asks for
pointer lock as the game starts, which headless Chrome refuses (`WrongDocumentError`, an exception in the play's
report). The mouse in a player's browser, headed, hasn't been played.

## The light glares

A light's glare shows where the depth under it, which the scheduler reads from RDRAM when the RDP is done with a
display list, is the light's own. On the page render to RAM's copy into RDRAM lands while the next list is drawn
(`src/main.cpp`), so the scheduler read each list's depth samples before they were there, those of the list three
before it, and a glare flickered as Joanna walked towards its light or away. `patches/artifacts.c` reads them a list
later on the page, in a fourth list (its header says how). A probe that read each sample the scheduler had taken
again a list later, in Defection's first 50 s by the regular plan's moves in Chrome: on the page from Pages 16173 of
20070 had changed since, with the patch 0 of 16395. A level's reset clears the four lists' flags of whether their
samples were taken, without which a list written again without the gun (G5 Building's opening cutscene) had its
screen positions read as pointers and the page trapped. The route from the file menu through G5 Building, Air Base,
Crash Site, Defense and Skedar Ruins plays in Chrome and Firefox. The desktop build keeps the game's three lists.

## The shader list

`src/rt64_shaders.txt` holds 784 shaders, which RT64 builds as 306 pipelines: 42 for the boot, then a section
a solo mission (21 sections of 34-132, a shader in the section of every mission that draws it). It was recorded
in headless Chrome at 1256x645 by `tools/record_shaders.sh`, a page load a mission, about 25 minutes in all; its
header says what each section played and what it leaves out. The plans' plays then missed 19 more, added by hand
at the end of the boot's, Defection's and Villa's sections: 10 in Chrome, different ones each run (the boot's as
Defection loads, before its section starts), and 9 that Firefox draws each time Defection starts and Chrome never
has. With them the regular plan misses none in either browser. A minute in each solo mission, started as the plans
start Defection, then missed 20 more in seven of them (Villa 2, G5 Building 5, Crash Site 3, Rescue 3, Defense 5,
Skedar Ruins 1, Maian SOS 1), each added at the end of its mission's section; with them a minute in each of the
seven misses none in Chrome. Plays of a route through 12 solo missions from each one's start (forward, four turns,
a strafe, a shot, aiming, forward and back) then missed 28 more in five (G5 Building 13, Air Base 5, Crash Site 4,
Defense 5, Skedar Ruins 1), Chrome and Firefox missing different flags of the same combiners, added the same way.
Then the route from the published page missed 2 more in Air Base, the boot as it plays by itself for 45 s (the
publish's boot check) 3, and Firefox 1 more as the intro's Defection loads and 1 in Skedar Ruins; with them the
route through those five misses none in either browser (N64Recomp-browser's `plays.jsonl`). The whole-game plan,
which watches each mission's opening cutscene to its end and plays its endscreen, then missed 80 more in twelve
missions, nearly the same ones in Chrome and Firefox (the list's header has them by mission), and its next run in
each, with them, 2 more. Firefox waits about 3 s for the boot's section before the first frame, about 3 s for
Defection's as it starts and 1-2 s for a later mission's as it loads, on a black screen; Chrome builds the whole list
behind the launcher in about 8 s.

## Frame rate

The game ticks at most once a retrace with at most two graphics tasks out, and counts each frame's length in VIs (the
decompilation's `src/lib/main.c` and `src/game/timing.c`), so it draws up to 60 a second wherever it can. Its own
controller reads hold it to 30, which `patches/frame_rate.c` undoes. What it draws, read from `g_Vars` and the
scheduler (`g_Sched`, `g_MainNumGfxTasks`) a few milliseconds at a time and from Chrome traces of every thread:

- The boot's logos: 60, a tick every VI.
- The title, the file menu and a level, as the game is: 30, capped or not (the desktop build 29-55). PD's
  scheduler sends the main loop a retrace at every RDP done as well as at every VI (`__scHandleTasks`,
  `src/lib/sched.c`), and the main loop ticks at any retrace half a frame or more after the last tick started
  (`main_loop`, `src/lib/main.c`), so a tick can start between VIs. A tick that finds no controller sample newer than
  the last tick's (`joy_debug_joy`, `src/lib/joy.c`) stops and restarts the controller polling to read one, and the
  scheduler answers each of those at its next retrace (`joys_handle_retrace`): the tick takes two VIs, and the poll it
  restarted has its sample only at the VI after. The retraces queued meanwhile start the next tick as soon as this one
  ends, before that VI, so it finds no sample either. Once a tick misses, the ticks after it miss too, for as long as
  a tick's work after its controller read is shorter than a VI, as it is on a fast CPU; on a console the game draws
  at 15-30. The runtime's controller polling has no part in it: `osContStartReadData` (N64ModernRuntime) posts its
  message at once, so a poll started at one VI has its sample by the next, as on a console.
- That chain, measured on the desktop build in Defection (patches that count ticks and the messages queued at each,
  and a switch flipped in memory, 20 s each way in turn, twice, on an idle host through Dozen): as the game is, 36-38
  frames a second, 411-451 of 710-753 ticks found no new sample, as many as took two VIs, and a tick started with
  1.1-1.3 retraces already queued. With a tick that finds no sample while a poll is out put off to the next retrace,
  58-60, every frame one VI, no tick without a sample or with a retrace queued.
- So `patches/frame_rate.c` puts such a tick off, retrace by retrace, until the poll's sample comes, two VIs at most
  (its header says why there and not by draining the queued retraces). Put off only once, the desktop build drew
  57-60 but the page fell back to 30 for 10-20 s at a time: a slow frame starts the chain with two or more retraces
  queued. The desktop build with it, beside the build without it, each started and driven the same way on an idle
  host through Dozen, 20 s a level reached through the survey, frames a second by the game's own clock
  (`osGetCount`, which the script's ran ahead of by up to 5% in some windows, in either build): Defection 59.9 (38.4
  without), Investigation 60.0 (30.0), Chicago 59.2 (48.8), every frame one VI but 1-4 in 20 s; the file menu 58
  (38), the next menu 60 in both. The game's clock (`lvframe240`) keeps to `osGetCount`'s in both. The title's
  attract sequence draws 60 in its scenes either way, with frames of 8-10 VIs as a scene changes in both. Walking and
  turning read the keys as before. With polling stopped (a pak's menu) no poll is out, so the game restarts it as it
  always has: that is from the code, as no pak's menu was played.
- The page, in headless Chrome at 1256x645 on a desktop GPU, the regular plan uncapped, interleaved with the page
  without the patch (base, new, new, base, whole GPU turns on an idle host; N64Recomp-browser's `plays.jsonl`, set
  `pd-tick2`), its game's frames counted from `g_Vars.thisframestart240` by N64Recomp-browser's
  `tools/frame_lengths.js`: the boot and menus 57-59 a second in both, every frame about one VI; in Defection 57-59 for
  most of the minute, 38-50 in the plan's firing, against 30 throughout for the page without it, every frame two VIs
  (one of its plays had a 10 s stretch at 57-59, the chain broken by slow lists). Presents 343.6 and 342.6 a second
  against 326.3 and 338.3, held by the uncapped rate (360) in all four, no WebGPU errors, no sound short, no shader
  missed. In Firefox, the regular plan with its default prefs: no WebGPU errors or exceptions, no shader missed, 23
  sound frames short with the host half busy with another build, Defection 42-56 frames of the game a second under
  presents its compositor holds near 60.
- Render to RAM's late copy (`src/main.cpp`) is not what held a level at 30: a page whose display lists go ahead with
  the previous copy still pending draws Defection at 29-31 like this one, capped and uncapped, and a Chrome trace of
  30 s there has RT64's Gfx thread idle 90% and the game's main thread all in `joy_debug_joy`, waiting in `osRecvMesg`
  for the scheduler's retraces (N64Recomp-browser's docs/findings.md, Ruled out, measured). Lists that run slow break
  the chain: with two graphics tasks out a retrace starts no tick and the queue empties, so the next tick starts at a
  VI with its sample. That fits the 55-60 of the first capped plays, without the prebuilt shader list, 18 frames in
  20 VIs at Defection's start. The CPU wrote to no framebuffer with a late copy pending in the shader recording's 21
  levels or in the plays.
- Beside GoldenEye's page from Pages, before the patch, the regular plans played in turn uncapped, two of each, on an
  idle host: GoldenEye 358 and 347 presents a second, held by the uncapped rate (360), and Perfect Dark 340 and 329,
  with no WebGPU errors and no short sound in either. A present is RT64's interpolated frame, so even at 30 frames of
  the game a second Perfect Dark's page presented near the rate its display allows.
- Capped, though, at a display above the game's 60, the page presented far fewer: on a 240 Hz display, Defection at
  61-62 presents a second in Chrome and 55-67 in Firefox, the game at 58 and at 42-52. Each frame told RT64 its own
  rate (60 over its own retraces), and a frame of two VIs among frames of one, a few a second in a level, read as 30:
  RT64 planned two VIs of presents for it and dropped half when the next frame came a VI later. RT64's pacing counts
  drops as the GPU falling behind, paces to a multiple of the latest rate, and tries the display's rate again after
  10-120 s, so the rate it paced to flipped between 30, 60 and 120 with the frames (about 130 changes in a play, read
  from a test build that printed each one) and its tries back up failed within a second. The rate a frame gives RT64
  is a guess at how long it stays on screen, since its interpolated frames are drawn before the next one comes, so
  `patches/frame_rate.c` gives the fastest of the last three frames' instead, as RT64 guesses from the VIs itself.
  The same tree with and without it, the regular plan at the display's 240 Hz on a desktop GPU, interleaved, whole GPU
  turns on an idle host (N64Recomp-browser's `plays.jsonl`, set `pd-fps`), Defection: Chrome 116-117 presents a
  second against 61-62, Firefox 216-219 against 55-67 with the game at 58 against 42-52; no WebGPU errors. Chrome's
  stay at 120: its sleeps to a 240 Hz display's slots end late, as other pages' do there.

`frametime_calculate` (`src/game/timing.c`) spins on `osGetCount` with no call that lets another thread run, but the
main loop's half-frame wait before a tick leaves it little to spin: in the logos 66 of 599 calls spun, 9.7 ms in 10 s
and 6.9 ms at most, and in Defection none. No sound was short in those plays.

A mission's opening cutscene held one frame of 8 VIs (133 ms) as each of its shots started, every 1-7 s, on the
desktop build and the page alike, and on a console too: `player_start_cutscene2` stops every controller's rumble
(`paks_stop`), and the game's `joy_stop_rumble` (`src/lib/joy.c`) stops the controllers' polling for each of the 4
ports and starts it again, each answered at the scheduler's next retrace (N64Recomp-browser's docs/findings.md, Ruled
out, measured). The polling stops so a console can reach the pak; here the runtime's motor calls answer at once
beside it, so `patches/paks.c` stops the rumble without stopping the polling. The page with it beside the page Pages
served, Defection's opening cutscene watched for 60 s in headless Chrome on a desktop GPU, Pages, build, build, Pages
in whole GPU turns (N64Recomp-browser's `plays.jsonl`, set `pd-cutscene-rumble`): no frame of 7-8 VIs in either
play of the build against 17 and 14, and 1 present gap over 100 ms in each (the mission's load) against 10 and 12;
no WebGPU errors, no shader missed.

A phone's CPU holds the game to 25-33 frames a second (N64Recomp-browser's docs/phones.md), and those mix frames of
one and two VIs, which the fastest of three tells RT64 are 60: RT64 drew about one frame for each, 46-47 a second. So
on the page, while the game can't keep 60 up, its ticks are held to every other VI and each frame says 30
(`recomp_pd_hold_at_30`, `src/pd_natives.cpp`, says when; `patches/frame_rate.c`). The intro on a Galaxy S21 in
Chrome, 140 s from Start Game with no input, with and without it, interleaved, big cores capped at 1.04 GHz: drawn
51.5 and 54.4 a second against 45.9 and 46.8, presents 49.5 and 48.8 over the last minute against 44.2 and 46.6, and
55-57 over the whole intro against 50-52. The desktop page, the regular plan uncapped on a desktop GPU, held once, for
10 s in Defection's firing (two seconds of 50 frames); presents 354 against 356, both at the uncapped rate.
