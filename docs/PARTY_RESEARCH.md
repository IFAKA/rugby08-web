# Party research (pt.research, 2026-10-13)

What the online party (docs/plans/2026-10-13-online-party.md) builds on: the game's modes, its front
end, its controllers and how a human-controlled player moves. Static research (Ghidra on
rugby08_deobf.gpr, the lifted code in gen/recomp, tools/query/callgraph.py); nothing here was run unless
it says so. **(C)** = confirmed by reading the code, **(I)** = inferred, to be measured before relying on it.

## Modes

### The front end is data, not classes
- **(C)** No per-screen classes in RTTI (only generic `Fe*` widgets): screens are numbered ids; behaviour
  lives in named callbacks, in four {name string, function} tables (~700 entries): `0x991870..0x992358`
  main menu, Team Select, MY RUGBY, challenge; `0x992378..0x992930` tournaments and World League;
  `0x992B08..0x992F10` in-game menus and training; `0x9933B0..0x993508` save/load.
- **(C)** Helpers: `FUN_004ED320()` = the front end `[0x013834E8]`; `FUN_00519410(screen)` its id word
  (+4); `FUN_004F1380(id, flag)` goes to a screen; `FUN_004F0700(n, 0)` a modal popup;
  `FUN_004215F0()` the setup block `0x9B6830`.
- **(C)** Front-end fields: `FE+0x24` tournament type (0 none; the per-type tournament state T starts
  there); `FE+0x28..+0x40` tournament settings (`+0x34` difficulty, `+0x38/+0x40` option bits);
  `FE+0x31400` previous screen id; `FE+0x3144C` the screen to show after the match.
- Screen ids: **(C)** 0x32 main menu (`GotoFEMainMenu` 0x4F6F50), 6 Play Now Team Select, 7 Stadium
  Select, 0xF tournament Team Select, 0x15F Starting XV, 0x15E tournament Squad Select, 0x1A0A practice
  controller select, 0x320 challenge select, 0x31D challenge Team Select, 0x31F Change Lineup,
  0x25E/0x98/0x99 in-match controller reassign, 0xA8D save; **(I)** 0xB Game Modes, 0x78 tournament
  select, 0x161 a second launching lineup screen.

| Type (FE+0x24) | Competition | SettingsDone | Team Select | Hub |
|---|---|---|---|---|
| 1 | World Cup (WC1999/2003/2007.rsg) | 0x5595A0 | 0x79 | 0x7A |
| 2 | Tri Nations | 0x5596D0 | 0x8D | 0x8E |
| 3 | Six Nations | 0x559800 | 0xA1 | 0xA2 |
| 4 | Lions Tour | 0x559260 | 0x49D / 0xB5 | 0xB6 |
| 5 | Ten Nations | 0x559950 | 0x119 | 0x11A |
| 6 | European Cup | 0x559140 | 0xC9 | 0xCA |
| 7 | Super 12/14 | 0x559A80 | 0xDD | 0xDE |
| 8 | Zurich Premiership | 0x559B80 | 0x123 | 0x124 |
| 10 | World League ("MasterLeague") | 0x559400 | 0x12D | 0x12E / 0x138 |

Settings-done calls `FUN_005C8D30(type, …)` (creates the competition); schedule loaders derive from
`CRugbyScheduleGenerationLoader` (vtable 0x8E3B18). Type 9 has no fixtures (hub falls back to 10).

### Controller assignment (every mode)
- **(C)** Side arrays: `DAT_009C8820[4]` Home, `DAT_009C881C[4]` Away (controller indexes 0..8, 0xFF
  empty); tags per device "H"/"A"/"" (`FUN_0081B610`); `FUN_004EAF00` swaps the sides; `FUN_004E9640` /
  `FUN_004E9690` any Home / Away controller; `FUN_004E9440` apply; `FUN_004E9520` reset.
- **(C)** Side locks `DAT_009C8824` (Home) / `DAT_009C8828` (Away) (setters `FUN_004E96E0/F0`): the
  per-device move handler refuses a locked side (0x4EA9A3, 0x4EAA4C).
- **(C)** `ControllerTeamSelectInit` 0x4F93E0 (screens 6, 0xF, 0x31D, 0x1A0A); `OnTeamSelectDone`
  0x4FA360: 0x1A0A/0x98/0x99/0x31D refuse humans on both sides; 6 and 0xF need one controller (else
  popup 0x13); then `FUN_004F9C70`: 6 → 7 Stadium; 0xF → load fixture and launch; 0x31D → 0x31F.
- **(C)** In a match: `IgControllerTeamSelectInit` 0x57FD00 = the pause menu's Reassign Controllers
  (same locks).

### Human teams in competitions
- **(C)** `FUN_004F7770(type, teamId)` = 1 if user-controlled (flag byte 0 = human). Toggled on the
  tournament Team Select by `OnTournamentToggleTeamControlledBy` 0x5578E0. Flags: World Cup T+0x4A8
  [pool*5+i]; Tri Nations T+0x24[3]; Six Nations T+0x30[6]; Ten Nations T+0x4A[10] (via FE+0x64);
  Euro Cup T+0x14[6*4]; Super 14 T+0x4C[14]; Zurich T+0x44[12]. Lions Tour: always the Lions (0x65).
  World League: one user team `*FUN_005CE680()` (manager `[0xA30B08]`).
- **(C+I)** Several teams may be human in WC, TN, 6N, 10N, Euro, S14, Zurich: a CPU team's side is
  locked per fixture; two human teams meet head-to-head; up to 4 controllers per side (co-op). Lions
  Tour and World League: one human team, co-op on one side only.

### Match start and end (every mode)
- **(C)** Launch `FUN_004F3D10` (hook `ms:launch`) sets `FE+0x3144C` from the current screen: 7 → 0x32;
  0xF/0x161 → the type's hub (0x334 unknown type; World League 0x13A); else 0x12E (World League) or
  0x32; `DAT_009BEE58` → 0x2DA (I: challenge results). Callers: `StadiumSelectOnDoneX` 0x4FCDD0,
  `FUN_004F9C70`, `ForcedTutorialX` 0x4F7330, Change Lineup (0x4DAFE8, `OnChangeLineupInit` 0x4DF190),
  `StartBackend` 0x4F4160, `WorldLeagueBackendState::vf1` 0x5DEFE0.
- **(C)** End: front-end re-entry `FUN_00565890` reads `FE+0x3144C`: a hub → record the result
  (`FUN_0052B2F0` → `FUN_0052B0B0`; WC `FUN_00554F40`; round advance via `0x539540`; TN `FUN_0053C420`);
  0x13A → `WorldLeaguePostGameState` (vf1 0x5DF1D0); else profile/stats `FUN_00512590` and the screen.
  Then `FUN_004E9520` resets all controller assignments.
- **(C)** Quit: `QuitToFrontEnd` 0x5761B0 (confirm popups 0x2E/0x2F, tournaments 0x30/0x31) →
  `FUN_00576140`.

### The modes
1. **PLAY NOW** (`OnEnterPlayNow` 0x4F6EC0): 6 Team Select (Starting XV 0x15F optional) → 7 Stadium
   Select (`StadiumSelectInit` 0x4FCA20: stadium, half length, difficulty) → launch → 0x32. 1-4
   controllers per side, both sides. One match. `InitRematch` 0x5797C0 not analysed.
2. **Tournaments 1-8**: settings → tournament Team Select (`OnTournamentTeamSelectInit` 0x54B810, teams
   toggled human/CPU; `OnTournamentTeamSelectContinue` 0x557C60 needs one human team) → Squad Select
   0x15E (option) → hub (`OnTournamentMenuInit` 0x54D8F0: fixtures, standings, trees, stats, squad,
   save `MemcardSaveTournament` 0x5ADA20). **Next match** `OnTournamentNextMatch` 0x54D480: resets
   controllers, simulates CPU fixtures (`FUN_00526360`, `FUN_005254E0`, `FUN_00525580`) up to the next
   human fixture, loads it (`FUN_005259C0`) → 0xF Team Select (CPU sides locked) → launch; no stadium
   screen. After: hub, result, round advance; champion `OnTournamentChampScreenInit` 0x539910.
3. **World League** (10): settings → 0x12D (one user team) → hub 0x12E/0x138 (seasons: trade windows,
   recruitment, end of season); next match → 0xF → `WorldLeagueBackendState` (ctor `FUN_005DEFA0`)
   launches and simulates the week; after: `WorldLeaguePostGameState` → hub.
4. **Challenge** (`CMissionManager`): 0x320 → 0x321 → (I) 0x31D Team Select → 0x31F Change Lineup
   (two-sided states `DAT_009C874C`/`DAT_009C8750`) → launch; CPU side locked, humans on one side only;
   results 0x511070 → 0x335.
5. **Training / Tutorial**: 0x1A0A controller select → `ForcedTutorialX` → launch; exactly one
   controller, on Away; drills switch inside the match (`IgTrainingStart*` 0x59BA70-0x59BD60).
6. **MY RUGBY**: no match (options, trophies, squads, editors, save/load).
7. **Multiplayer**: local only (shared Team Select); no online/LAN in the exe.

**Party modes**: PLAY NOW (both sides), tournaments 1-8 (human teams, co-op/head-to-head), Lions Tour
and World League (co-op on one side), Challenge (co-op on the human side). Training is single-player.
Open: which controller drives hub/fixture screens (I: any device drives menus).

## Main menu item

The front end is **data-driven**: screens are layout files (`.rfe`, XML or binary XML) loaded into a
generic `FeScreen`; menu items are widgets in those files; an item's action is a callback name or a
`fez:` script. **(C)** 0x991C80 is not an action table: it is one entry (`HeadTesterRotatePC` →
0x574380, a stub) of the main callback table 0x991870..0x992358 (349 entries).

### Screen system
- **(C)** Front end [0x013834E8] (getter `FUN_004ED320`; source `src/fe/femain.cpp`, `loadfe.cpp`): +4
  current FeScreen, +0x18 modal popup screen, +0x2FB14 loaded screens, +0x2FB18..+0x2FB30 font slots,
  +0x31400 previous screen id, +0x31414 screen registry {records, count}, +0x31498 "switching".
- **(C)** FeScreen (0x5C bytes, ctor 0x83C9B0, vtable 0x0090F838 = only the destructor): +4 id (word,
  getter 0x519410), +0xC current window, +0x24 focused button (setter 0x437ED0), +0x28 button under the
  mouse, +0x2C screen-level message handlers. FeWindow (0xD4, ctor 0x8437D0, vtable 0x0090F8D0); find
  window 0x421470, find button 0x421450 → 0x83E790.
- **(C)** Widgets: vf0 HandleMessage(msg, out, flag), vf4 HitTest(x, y), vf5 Draw; (I) vf1/vf2 anim
  attributes, vf3 destructor. FeTextButton vtable 0x8DB220 (draw 0x84C0C0), FeGraphicTextButton 0x8DB278,
  FeListControl 0x8DB2C0.
- **(C)** Messages (parser 0x59F560): Down/Up/Right/Left press 0/2/4/6 (release +1), X 8, Square 0xA,
  Triangle 0xC, Circle 0xE, L1 0x10, R1 0x12, L2 0x14, R2 0x16, Start 0x18, Select 0x1A, XPressMouse
  0x1C, MouseOver 0x1E, MouseOff 0x1F, TransitionIn 0x20, TransitionOut 0x21. `FeScreen::SendMessage`
  0x83CF40: focused object, then the window, then the screen's handlers; 4 = not handled, 5 = handled.
- **(C)** Registry id → file: a screens XML in data.gob (`<Global Number="182"><Screen ID FileName
  PackageName>`) loaded by 0x4EC580 into FE+0x31414 (0x48-byte records, lookup 0x4EB9B0; asserts
  `iIndex <= m_iNumScreens`). Ids: 2 title, **50 main_menu_pc**, 6 Play Now Team Select, 7 stadium, 8
  start backend, 10 game modes, 13 extras, 14 my rugby, 120 wc_settings, 366 MessageBox, **999
  keyboard**, 6666 practice controller select.
- **(C)** `FeSetCurrentScreen(id, bool)` 0x4F1380 (body 0x4F138A): TransitionOut to the old screen,
  previous id → +0x31400, sound, find in the loaded list (0x83EB40) or unload (0x4ECB30, I) and load
  (0x4EBAE0), FE+4 = it, 0x4EBD60(id), TransitionIn. **No screen stack**: Back is each screen's Triangle
  handler (`fez:GoToScreen(N)` / `ScreenGoBack`). Modal popups 0x4F0700(id) (table 0x9906F8) run a
  **blocking nested loop** 0x4EFE10 while FE+0x18 is set.
- **(C)** Loading: 0x4EBAE0(id) → `Hbs::Xml::Document` (binary if it starts `00 BE EF FF`, **else
  TinyXML text**) → 0x5A06D0(data, 0, screenList, &FE+4, size) → LoadScreen 0x59FE10 (ID,
  DefaultButton, DefaultWindow, ShowFrame, `Msg*`; children Window 0x5A07E0, buttons factory 0x5A38B0,
  TextButton 0x5A1D90, AnimationDesc 0x5A4200). The gob has plain-XML screens too (test screen 9002):
  **screens we write as XML text load**.

### The main menu (screen 50, main_menu_pc.rfe, binary)
- **(C)** `MsgTransitionIn="MainMenuInit"` (0x4F7260); window 2000; items are TextButtons (250×26,
  yPos 0/25/…/150, `FontColor=bfbfbfff`, `PickRegion`, `MouseHighlightable`, `MsgMouseOver=
  OnMenuItemHighlight`, `MsgMouseOff=OnMenuItemUnhighlight`, `MsgUpPress=FeSelectableObjectOnUp`,
  `MsgDownPress=FeSelectableObjectOnDown`, animations `MenuItem{One..Seven}Trans{In,Out}`):
  PLAY NOW `$460` → `OnEnterPlayNow`; RUGBY WORLD CUP 2007 `$2298` → `fez:GoToScreen(120)`; CHALLENGE
  MODE `$2121` → `OnGameModesMenuEnterChallengeMode`; GAME MODES `$461` → `fez:GoToScreen(10)`; MY
  RUGBY `$464` → `fez:GoToScreen(14)`; EA SPORTS EXTRAS `$463` → `fez:GoToScreen(13)`; QUIT `$358` →
  `onQuitGame`.
- **(C)** Text: `$N` → StringTable::Get 0x80F750 (`strings/stablesku.english`); any other text is used as
  is (`Text="ONLINE"` needs no string entry).
- **(C)** Actions: 0x59F270 stores per message a function pointer or a `fez:` script (button handler
  list +0x8C, insert 0x84BEF0). Name → function 0x4E9220 searches 0x990550 (42 `Fe*`), 0x991870 (349),
  0x992378 (183), 0x992B08 (129), 0x9933B0 (43). **Callback ABI: cdecl `int cb(FeScreenObject *)`**.
  `fez:` interpreter 0x5A55B0: GoToScreen(N), ScreenGoBack, Callback(name), SendMessage::*,
  SetCurrent::*, Animation, SetWindowState, SetFontColor, SendBtnMsg, SendScreenMsg; prefixes
  `fezpc:`/`fezmouse:`/`fezkeyboard:`.
- **(C)** Highlight: OnMenuItemHighlight 0x4E9010 moves a bar window (id = button id + 0x14) to the item.
  `OnEnterPlayNow` 0x4F6EC0: profile flag +0x26C bit 0 clear → screen 6, else the first-run tutorial
  questions (popups 1/2 → 0x1A0A or 0x13C8; measured: screens 0x16E, 0x13C8 before 6 on a new profile).

### Text and input
- **(C)** Fonts: 0x5A15E0 parses FontColor, Font, FontSize, DropShadow, Justification; font lookup
  0x59E900: "1"/fontbod FE+0x2FB18, "2"/fonttil +0x2FB1C, "3".."6" +0x2FB20..+0x2FB2C (the FFN fonts).
  FeTextButton draw 0x84C0C0: text `char*` +0x158, FeFont::DrawText 0x83DCE0 (font at button+0xC8) →
  glyphs 0x829AC0.
- **(C)** Per-frame FE: RugbyFrontEnd::vf3 0x418B80 → 0x566660 → 0x5641A0 → input 0x4EF840 → 0x4EF5D0 →
  per binding 0x4EE950 (auto-repeat) → SendMessage. Bindings (15 × 0x28, message at +0xC): 0x990F90
  shared, 0x9911E8 pad 1, 0x991440 pad 2.
- **(C)** Navigation: FeSelectableObjectOnUp/Down/Left/Right 0x4E6440/0x4E65B0/0x4E6720/0x4E6890 (neighbour
  ids in the button, Up at +0x9C; others I). Mouse (device type 9): 0x4EF230 hit-tests every button (pick
  rect +0xC4), sends MouseOff/MouseOver, focuses; a click goes to the hovered object (screen+0x28).

### Recommended approach (pt.menu)
Overrides: `game/overrides/<dir>/X.c` with `GAME_OVERRIDE(sub_XXXXXXXX)` (game/include/game.h), then
`tools/lift/lift.py` renames the original `sub_X_lifted`, so every caller (direct or virtual) reaches ours.
1. **Our screens as XML text held in the runtime** (no EA bytes): override `sub_004EBAE0`; for our ids
   (e.g. 5100 ONLINE, 5101 CREATE, 5102 LOBBY) put the XML in guest memory and call
   `sub_005A06D0(buf, 0, [FE+0x2FB14], FE+4, len)`; other ids → the original. Same widgets, fonts,
   colours, handlers and animations as the main menu; Triangle → `fez:GoToScreen(50)`.
2. **Our callbacks**: names resolve through 0x4E9220; map new names to unused developer callbacks taken
   over as entry points (HeadTester* 0x573C90..0x5743E0: 0x574380/0x574390/0x5743A0 have no callers) or
   override the lookup.
3. **ONLINE in the main menu**: for id 50, take the game's own main_menu_pc.rfe at runtime, insert a
   TextButton `ONLINE` after PLAY NOW, shift the later items, fix Up/Down neighbours (needs the
   binary-XML decoder); or patch the loaded screen after MainMenuInit (insert a cloned TextButton).
4. **JOIN letter grid**: the game's own keyboard screen 999 (OnKeyboardInit 0x4FEA90, OnKeyboardStart
   0x4FF560 confirm → back to FE+0x31400, Triangle 0x4FD010, Backspace 0x4FF490; typed text in button 0).
5. **Lobby**: XML screen whose TextButton texts the runtime rewrites each frame (+0x158).
- Risks (I): 0x4EBD60(id) / unload / texture packages may expect a registry entry for our ids; binary RFE
  decoder not done; ICF-folded tiny setters; popups' nested loop affects online pacing;
  FeSetCurrentScreen special-cases some ids (0x15E, 0x32, 0x2696). EA bytes stay out of the repo.

## Controller hot-plug

**Short answer (C):** the game never enumerates devices again while it runs (no hot-plug); on
DIERR_INPUTLOST/NOTACQUIRED it only re-acquires. But it has a console-style "active controller
disconnected" watcher that reads each controller's **attached** flag (CControl+0x2580), and Team Select
shows and assigns only attached controllers. On PC only the constructor writes that flag (from
DIDC_ATTACHED). So the runtime can drive hot-plug by toggling it on controller slots that exist from boot.

### CPCControlMgr (vtable 0x0090F09C; bases CControlMgr 0x0090ECA0, Win32Thread)
- **(C)** Game+0x10 (getter `FUN_00813090`), ctor `FUN_0081F110` → CControlMgr ctor `FUN_0081A380(9)` +
  INPUT_init `FUN_00890B70` (once, guarded by `DAT_00AA895C`; device table `DAT_00AA67D8` stride 0x10C,
  max 32; `EnumDevices(0, FUN_008908C0, &DAT_00AA8958)`, count `DAT_00AA8958`; DirectInput 7 API).
- **(C)** Fields: +0xC entry count, +0x14 active controllers (recounted each poll), +0x18 / +0x1C
  `CControl*[9]` (poll list / lookup list), +0x48 semaphore "HBCONTROLS", +0x4C the CSV path.
- **(C)** vtable: vf2 0x0081F5A0 thread proc (the poll runs on its own thread), vf3 0x0081F600 frame
  update, vf6 0x0081F630 poll (reads a device only if `+0x2580 && +0x2574==7 && !+0x25C0`), **vf7
  0x0081F1A0 = Enumerate(const char *csv)**: assigns csv, memsets +0x18, `FUN_0081F300`; boot call
  `mgr->vf7("data/controltypes.csv")` at 0x0041431C.
- **(C)** `FUN_0081F300` builds one entry per DirectInput device in enumeration order (mouse 0,
  keyboard 1, pads 2…): first keyboard → CKeyboard (vtable 0x0090F0F4, guard `DAT_00A7EF40`), first mouse
  (guard `DAT_00A7EF3F`), others CPCControl (ctor `FUN_0081F6F0` → CControl ctor `FUN_0081AF50`).
- **(C)** CControl: +0x2558 side "H"/"A"/"" (`FUN_0081B610`), +0x2570 type (9 mouse, 8 keyboard, 5
  joystick), +0x2574 state (7 ready), +0x257C **active** (vf4), +0x2580 **attached** (getter
  `FUN_0044BA50`; ctor: DIDCAPS dwFlags & DIDC_ATTACHED via `FUN_0079CA10`), +0x25E4 DirectInput
  device index, vf5 0x0081AE30 IsDisconnected = active && !attached.
- **(C)** PhysicalInputBlock (vtable 0x008E8DE8, ctor `FUN_0062F800`) holds the CControl at +4;
  CPlayerControl+0x1C the block; CRugbyTeam+0xC0[4] (count +0xD0) the CPlayerControls (add
  `FUN_00615D90`, remove `FUN_00615CC0`). Team Select keeps entry indices only.

### Why the boot-time re-enumeration crashed, and why not to do it
- **(I)** `FUN_00871EE0` is `strlen`: vf7 starts with `std::string::assign(arg)`, so a call without a
  valid csv argument is strlen(NULL).
- **(C)** vf7 would not find new devices anyway: it never calls EnumDevices (only INPUT_init does,
  once). **docs/MATCH_STATE.md's chain "vf7 → FUN_0081F300 → FUN_007ACD70 EnumDevices" is wrong**:
  `FUN_007ACD70` is the DirectInputCreateA thunk. A second vf7 would leak the old controllers, skip
  mouse/keyboard (static guards) and shift entries, and freeze existing input blocks. Not a path.

### Existing runtime paths
- **(C)** Device loss: `FUN_0079C980` (GetState) re-acquires on 0x8007000C/0x8007001E, else returns a
  saved/zero buffer; nothing marks the controller detached.
- **(C)** Disconnect watcher: RugbyGame vf1 0x00423D00 → `FUN_00423DD0` each frame: `FUN_0081ACE0` = 0
  when an active controller is detached; with Game+0x134 set (`FUN_004049C0`) it pauses into the
  in-game controller screen 0x25E (0x98/0x99) in a match, or goes to controller select (6/0xF) with a
  popup in the menus; then `FUN_0081AED0` deactivates all controllers.
- **(C)** Team rebuild: `FUN_0062CFC0(team)` removes the team's CPlayerControls, `FUN_0062D080` adds
  one per entry 1..min(count-1, 4) (`FUN_0044BA20`) whose side matches and that is attached and active.
  Callers: `FUN_004FA360` (Team Select / Reassign commit, + `FUN_004E9440` activates assigned
  controllers), `FUN_006323D0` (half-time side swap), `FUN_004BDFEB`.

### How Team Select builds its icons
- **(C)** Per-frame update `FUN_004EA790` (body 0x004EA7A2): for idx 1..8 `c = FUN_0081A5A0(mgr, idx,
  4)` (0 unless 1 <= idx <= min(count, 4)); if `c && c+0x2580` it handles that controller's left/right
  (sets the side) and counts it (at most 4). Icons: only controllers present **and attached** show
  (`FUN_004E98A0(idx, 8)`); widget id = idx*10+0x14. So an entry with +0x2580 = 0 is "present but
  disconnected": no icon, no side, skipped by team building.

### Design (pt.controllers)
1. **Fixed slots from boot**: our DirectInput enumerates mouse, keyboard, then pad slots (<= 7). A slot
   without a player answers GetCapabilities without DIDC_ATTACHED: the game's constructor leaves it
   detached, Team Select shows only real devices. Detached slots return a neutral state (POV 0xFFFFFFFF).
2. **Join/leave = toggle +0x2580** on the main thread at a frame boundary (CControl found in
   `mgr+0x1C[e]` by +0x25E4 = the slot's DirectInput index). Never delete controllers or call vf7.
3. **Leaving** in menus: side "" and active 0 first; in a match either let the watcher pause into
   Reassign Controllers (Game+0x134) or rebuild the teams as `FUN_004FA360` does (I).
4. **Four players**: entries 1..4 only (three places: `FUN_0081A5A0`, `FUN_0044BA20`, the counter in
   `FUN_004EA7A2`). Keyboard + 3 pads = 4 players; with 4 pads, move the keyboard entry past index 4 in
   both +0x18 and +0x1C (I: safe, identity uses +0x25E4).

## Player movement

Units: positions in 1/52.5 m (52.49344 per metre, FUN_00723620), speeds m/s, headings in turns wrapped
to (-0.5, 0.5] (FUN_00706F90, FUN_004AC5B0), actor time in ticks of 1/50 s.

### Pad → desired heading and speed (inside CRugby::Update logic)
- **(C)** CRugby::Update `0x0062ED50` → rules `0x0062E3A0` → CRugbyTeam update `0x00616230(team, dt)`
  → per human `0x006160F0`: CPlayerControl::Update `0x006AE0F0(dt)`, then the human step `0x006BE540`
  on the controlled player (control+0x10); then `FUN_0060CFE0` per player (via `0x00616790`).
- **(C)** CPlayerControl::Update reads the input block (+0x1C) sticks; `0x006AD9B0` → {x, y, mag <= 1,
  angle = atan2(x, y)}: left stick control+0x20..+0x2C, right +0x30..+0x3C, previous +0x40..+0x5C.
  Button 0x200 = manual player switch (event 0xB2). Angle getter `0x006AD670(1)` is camera-relative:
  `atan2(camFwd.x, camFwd.z) - angle`.
- **(C)** Human step `0x006BE540`: only in state HUMAN (0x11, via [player+0xBC]); with no task
  (player+0x94 == 0) → HumanMove `0x006BE760`; always the action buttons `0x006BECB0`. A task (kick,
  pass, tackle, catch) owns movement.
- **(C)** HumanMove: idle → target = own position, speed 0; moving → target = pos + 320 units (6.1 m,
  0x8DAEEC) toward the stick; gait `0x006BE660`; `speed = 0x00655F70(player, gait, 0)` → actor vf9;
  **stick magnitude does not scale speed**; `0x00721900` sets actor+0x7C = atan2(dx, dz) / 2π.
- **(C)** Gait (player+0x5C): 2 run, 3 sprint (button 0x1000 held, stamina player+0x3C > 8 to start, > 0
  to keep).
- **(C)** Run speed: per-player cache float[30] at `0x00A32038`, refilled only in CRugby::Update
  (`0x00655A40`): `4.5 + pace/100 * 3.5` m/s (pace = record byte +0x23). Sprint (`0x00655E30`) by pace:
  95+ 9.5; 85-95 9.0-9.5; 75-85 8.5-9.0; 65-75 8.0-8.5; 55-65 7.5-8.0; <55 7.0-7.5.
- **(C)** Acceleration (`0x00655B40` → actor+0x10C): to run speed in 1.0 s; run → sprint in 0-4 s by the
  acceleration rating (record +0x24). Stamina drains while sprinting, regains 5-10/s.

### Locomotion: FIFAActor at player+0xD8 (vtable 0x8D7068, update vf1 `0x00723F30`)
- **(C)** The presentation step `0x00401480(dt)` (dt × 50 ticks) **still runs on a friend**. Per tick:
  target heading +0x70 = wrap(+0x7C), target speed +0x68 = +0x80 (max +0x108, default 9.0); while in the
  MoveGroup (locomotion): speed +0x6C steps toward +0x68 by ±(+0x10C)/tick (default 0.4); heading +0x74
  moves by a PD controller (`0x00722790`, gains filled at runtime) capped at 0.02 turn/tick (~360°/s).
- **(C)** Root motion moves the player (`0x0070EF50`); per-tick delta at +0xEC..+0xF4 (= player
  +0x1C4..+0x1CC). Setters used by the puppet: slot 0x1C `0x004AC780` (writes +0x74 and +0x7C),
  position `0x0070ED50`.
- **(C)** The replay frame's +0x117D[c] is the shirt number (player+0x78) of controller c's player: a
  friend can find its own player in the host's frame.

### Running it for one player on the friend: not as is
- **(C)** `0x006AE0F0` switches players and posts events; HumanMove needs state/task, the run-speed cache
  (zero on a friend: only CRugby::Update fills it, I), stamina, acceleration, set-play and offside
  queries. The actor path runs on the friend but moves a player only in its MoveGroup, and
  ms_puppet_place overwrites position and heading anyway.

### Design (pt.predict): a small kinematic model on the friend
- Predict only the friend's own player, only while the host reports open play, state HUMAN, no task;
  snap otherwise. Add to the host frame per human player: state, task != 0, gait, stamina, actor
  speed/heading, run and sprint speeds.
- Input: the friend's own pad; `a = camYaw - atan2(sx, sy)`; sprint = button 0x1000 and stamina.
- Per 0.02 s tick: `vt = moving ? (sprint ? vSprint : vRun) : 0`; `v += clamp(vt - v, ±acc)`;
  `h += clamp(K * wrap(a/2π - h), ±0.02)`; `P += v * 0.02 * 52.49 * (sin 2πh, 0, cos 2πh)`.
- Reconcile: keep (tick, input, state); on host frame n re-simulate from host(n) through the stored
  inputs; show prediction + an offset decaying over ~100-150 ms; snap when the error > 2 m.
- Measure first (native probe on the controlled player): stick, camera yaw, actor +0x7C/+0x74/+0x6C/
  +0x10C, gait, stamina, task, state, the cache; fit K and the ramps; offline, run the model on
  recorded inputs against the host path delayed 3-8 frames and report the error (p50/p95) and snaps.
