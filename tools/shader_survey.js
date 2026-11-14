// Evaluated in the page by N64Recomp-browser's tools/record_shaders.mjs (tools/shader_plan.json) and play_page.mjs
// (tools/play_plan.json): finds patches/shader_survey.c's struct in RDRAM by its magic words and offers
// stage(), the stage playing (g_StageNum: 0x30 Defection, whose stage the title's intro plays in too, 0x26 the file
// menu's and the Carrington Institute's, 0x5a the title); untilStage(stage, ms), which resolves true once that stage
// plays, false after ms; play(stageindex, ms), which starts that solo mission (its index in mission select's list, 0
// Defection) on Agent as mission select's Accept does and resolves true once the game has taken the request and loaded
// that stage, false after ms (ask from the file menu or a level: the title's intro drops it); inControl(), true once the
// player is in control (the stage's own view, its opening cutscene over or skipped); untilControl(ms), which resolves
// true once the player is, false after ms; clock(), the level's clock, 240 a second of the game's own time
// (g_Vars.lvframe240), for a plan's ["clock", "clock()", 240]; setInvincible(), which keeps the player alive for as long
// as the page runs, as the Invincible cheat does; newFile(ms), which makes an Agent file on the Game Pak from the file
// menu, as New Agent does, and resolves true once it is saved, false after ms or if the save failed (with a console
// line of its error); and complete(ms), which ends the mission playing as completed, as its own script does once its
// objectives are done, and resolves true once the game has taken it (its endscreen follows), false after ms
// (tools/play_plan_full.json plays the game through with them). For the Combat Simulator (tools/play_plan_pads.json):
// mpMenu(ms), which opens its menu from the file menu as the main menu's Combat Simulator does and resolves true once it
// is open, false after ms; joined(), the players in there, a bit each (0x1 player 1, 0x2 player 2), as each
// controller's Start joins its own ("Player 2: Ready!"); and mpStart(stage, slots, ms), which starts a combat match on that stage (0x32
// Skedar) for the players joined, or slots' players where slots isn't 0, and resolves true once the stage plays, false
// after ms. hudMsg() shows a HUD message at the left edge once the player is in control, as the level's "One minute
// left." does, to look at one where the window is wider than 4:3.
(() => {
  if (!window.__survey) {
    for (let a = 0x80000000; a < 0x81800000; a += 4) {
      if (n64r(a) === 0x53484452 && n64r(a + 4) === 0x53525659) { window.__survey = a; break; }
    }
  }
  const s = window.__survey;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  window.stage = () => n64r(s + 24) | 0;
  window.clock = () => n64r(s + 28) | 0;
  window.inControl = () => n64r(s + 40) !== 0;
  window.untilStage = async (stage, ms) => {
    const end = Date.now() + ms;
    while (window.stage() !== stage && Date.now() < end) await sleep(100);
    return window.stage() === stage;
  };
  window.play = async (stageindex, ms) => {
    const before = n64r(s + 8), loads = n64r(s + 36);
    n64w(s + 16, stageindex);
    n64w(s + 12, 1);
    const end = Date.now() + ms;
    while (n64r(s + 8) === before && Date.now() < end) await sleep(50);
    if (n64r(s + 8) === before) return false;
    const target = n64r(s + 32) | 0;
    while ((n64r(s + 36) === loads || window.stage() !== target) && Date.now() < end) await sleep(100);
    return n64r(s + 36) !== loads && window.stage() === target;
  };
  window.hudMsg = () => { n64w(s + 88, 1); return 'stage 0x' + window.stage().toString(16); };
  window.setInvincible = () => { n64w(s + 20, 1); return 'stage 0x' + window.stage().toString(16); };
  window.newFile = async (ms) => {
    n64w(s + 52, 1);
    const end = Date.now() + ms;
    while (n64r(s + 56) === 0 && Date.now() < end) await sleep(100);
    const result = n64r(s + 56) | 0;
    if (result === -1) console.log(`newFile: the save failed, pak error ${n64r(s + 60) | 0}`);
    return result === 1;
  };
  window.untilControl = async (ms) => {
    const end = Date.now() + ms;
    while (!window.inControl() && Date.now() < end) await sleep(100);
    return window.inControl();
  };
  window.joined = () => n64r(s + 84) & 0xf;
  window.mpMenu = async (ms) => {
    n64w(s + 64, 1);
    const end = Date.now() + ms;
    while ((n64r(s + 80) | 0) !== 3 && Date.now() < end) await sleep(100);
    return (n64r(s + 80) | 0) === 3;
  };
  window.mpStart = async (stage, slots, ms) => {
    const before = n64r(s + 76), loads = n64r(s + 36);
    n64w(s + 72, slots);
    n64w(s + 68, stage);
    const end = Date.now() + ms;
    while (n64r(s + 76) === before && Date.now() < end) await sleep(50);
    if (n64r(s + 76) === before) return false;
    while ((n64r(s + 36) === loads || window.stage() !== stage) && Date.now() < end) await sleep(100);
    return n64r(s + 36) !== loads && window.stage() === stage;
  };
  window.complete = async (ms) => {
    const before = n64r(s + 48);
    n64w(s + 44, 1);
    const end = Date.now() + ms;
    while (n64r(s + 48) === before && Date.now() < end) await sleep(50);
    return n64r(s + 48) !== before;
  };
  return s ? 'survey at ' + s.toString(16) : 'no survey struct: a page built without patches/shader_survey.c';
})()
