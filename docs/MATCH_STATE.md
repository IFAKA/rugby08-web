# The live match in guest memory (on.match_map)

Read by `runtime/core/match_state.c` (API: `runtime/include/match_state.h`; `RT_MS_LOG=N` traces one
`ms:` line every N frames). Found with the probes in `runtime/debug/match_probe.c` on the native det run
of `tests/replays/match_play.txt` (keyboard home team; open play, rucks, scrums, lineouts, a penalty, a
try and its conversion up to frame 30000), RTTI vtables (`analysis/vtables.json`) and Ghidra
constructors. Check: `tools/net/match_state_check.sh` (evaluation `tools/net/match_state_eval.py`).
Units: metres, game seconds. Pitch frame: origin at the centre spot, x across (touch near +-35; players
reach +-36.6 at lineouts), z along (play seen in -64..+11), y up.

## Verified against the screen (same run, `logs/ms/p_*.png`, shots every 600 frames)
- Clock: HUD 01:47 at f14400 = decoded 107.1 s; HUD 29:28 at f29400 = 1768.2 s.
- Score: HUD 0-0 until the New Zealand penalty (~f17450), 0-3 until the try (~f26800), 0-10 after the
  conversion (~f27700); the decoded score matches at every sample.
- Phases (CRugby state): Free = open play; Formation = rucks, mauls and scrums ("SCRUM TO" on the HUD);
  LineOut; GoalKick (the conversion); Kickoff; Empty = cutscenes (walk-out, replays: players parked).
- In open play the clock runs exactly 88 game seconds per 600 frames for this match length (81 of 81
  samples); it stops at dead balls.

## Roots
- `0x00A31988` CRugby* (the match; set by its ctor FUN_0062b2b0, 0 outside a match). `0x00A31980` = 1 once built.
- `0x009B6830` match setup (static, image): returned by FUN_004215f0. Home team block at +0,
  away at +0xC60 (each team's CRugbyTeam+0xC points at its block); +0x18C0/+0x18C7 per-team extras.

## CRugby (vtable 0x008E8E80, ctor FUN_0062b2b0)
| off | what |
|---|---|
| +0x50, +0x54 | CRugbyTeam* (side 1, side 0 in the runs so far: attack/defence order?) |
| +0x58, +0x5C | float, grows with match time (64.4 at f17000, 197.1 at f25000) |
| +0x60 | u32 counter, grows ~1/frame in play |
| +0x64 | CRugbyLineOut* |
| +0x68, +0x6C | CRugbyTeam* home (side 0), away (side 1) |
| +0x70 | CRugbyRuck* (stays set after a ruck ends: not a "ruck now" flag) |
| +0x84 | Setplay Overlord*, +0x88 CollisionMaster*, +0x8C PathFinder* |
| +0x98 | BallManager* |
| +0x9C | AiStateMachine<CRugby>*: +4 = current state; its vtable names the phase (KickoffState 0x8ED0A4, FormationState 0x8ECFF4, RunState 0x8ED194 (not seen), FreeState 0x8ED010, LineOutState 0x8ED0C8, PuntState 0x8ED110, GoalKickState 0x8ED088, DropOut22State 0x8ECFA8, IKickState 0x8ED168, QuickOptionState 0x8ED050, EmptyState 0x8ED134) |
| +0xB8 | CReferee* |
| +0xDC | Difficulty Manager*, +0xE4 Impact Manager* |

## CRugbyTeam (vtable 0x008E6734, 0x1A4 bytes, ctor FUN_00615370(rugby, side, 15))
+0xC team setup block (in 0x009B6830), +0x10..+0x20 player pointers that change (roles: carrier etc.),
+0xD4 CRugbyPlayer*[15], +0x11C side byte, +0x128 player count byte, +0x18C another team (opponent).

## CRugbyPlayer (vtable 0x008E5E00, 0x240 bytes, ctor FUN_0060a1a0(team, number 1..15))
+0x0A index byte, +0x78 shirt number byte, +0x90 team*, +0x84/+0x88/+0x8C live position x/y/z
(matches the ball while held), +0xA0 heading (degrees), +0xA4/+0xAC and +0xB0/+0xB8 target/previous
positions, +0x3C stamina-like (84..100), +0x10 its record in the team setup block. Off the field for
a cutscene the game parks a player at x = z = 95.25. +0xBC PlayerStateMachine (+4 the current state):
PassLineState 0x8ED82C, ForwardState 0x8ED3A0, GuardState 0x8ED4D4, EmptyPlayerState 0x8ED984,
FullBackState 0x8EDA2C, KickState 0x8ED968, SceneLeavingState 0x8EDB44, HumanState 0x8ED9D8 (one player at a time:
likely the controlled one), ScrumHalfState 0x8EDAF0, OnGroundState 0x8EDA80, SequenceState 0x8EDAB8.

## BallManager (vtable 0x008E6B74, 0x1DC bytes, ctor FUN_00621ca0)
+0x30/+0x34/+0x38 position, +0x3C/+0x40/+0x44 velocity (in flight; gravity in y), +0x58 3x3 rotation,
+0x114 holder CRugbyPlayer* (0 when loose/in flight; within 0.6 m of the ball 95% of frames),
+0x118 last holder, +0x11C previous holder.

## Clock and score (in the match setup, 0x009B6830)
- setup+0x18F0 float: match clock in game seconds (HUD mm:ss; 447.3 = 07:27 at f17000). The clock HUD
  reads this offset (FUN_0058a680).
- setup+0xBB8 home team id, setup+0x1818 away team id (team block +0xBB8 each; NZ = 13 in the runs).
- setup+0x1908: score events, singly linked list of 0x14-byte nodes {u16 scorer player id, u32 type,
  u32 team id, float clock, next}. Added by FUN_005ce320(setup, team id, player id, type, time, notify)
  (FUN_005ce32a appends and posts a CScoreData message 0xA7); freed by FUN_005ce520.
  Types: 0 try ("adding try", FUN_00616e20) 5 points; 1 conversion 2 points (the try + conversion took
  the HUD from 0-3 to 0-10); 2 and 3 the 3-point kicks (penalty, drop goal: FUN_00430b30 gives 1 or 2
  from the kick kind; the penalty at f17450 was type 3).
  The list feeds the scorers widget (FUN_00577e60) and the CScoreData message. The scoreboard reads the
  team stats instead: [CRugbyTeam+0x34] -> 4 bytes {tries, conversions, penalties, drop goals}, score =
  FUN_00632C60(stats) = 5a + 2b + 3c + 3d (COverlayItemScoreBox::vf1 -> FUN_0057ABF0; also crowd,
  commentary, result and standings code, 83 callers). The game bumps them with the cdecl
  FUN_00632D00 (try), D20 (conversion), D40 (drop goal), D60 (penalty) next to each FUN_005ce320 call.
  A friend adds the host's events with both (runtime/core/match_puppet.c score()).
- Per-player match stats in the team block's player records (100 bytes each, record 0 at block+0;
  CRugbyPlayer+0x10 points at its record): +0x48 u16 tries, +0x3E..+0x43 kick counters.

## Match setup: everything that defines a match (on.match_setup)
Two runs identical except for one Stadium Select choice differ, in static data, only in these bytes
(the rest is menu and heap noise, equal in every variant):
- +0x0000..+0x18C0 both teams' blocks (rosters, player records), written when Team Select is confirmed;
  +0x18C0..+0x18CD per-team extras (FUN_004BFBA0). No heap pointers: equal across runs with different
  heaps, so the bytes can move between games.
- The block is 0x1924 bytes (the menu's FUN_005259C0 clears that much). +0x191C kits: bits 0-1 home,
  2-3 away, non-zero = the alternate kit (Team Select's kit control; read by the match loader
  FUN_00402529 after launch). +0x1910..+0x1918 are written by match init.
- +0x18D0.. conditions and wind (+0x18D8, +0x18DC, wind vector floats +0x18E0/+0x18E8), +0x18EC half
  length (index; 5 mins = 1, 10 mins = 2), +0x1902 stadium id (Murrayfield 29), +0x190C difficulty
  (Club 0, Pro 1): written by Stadium Select's commit FUN_004FCDD0 from its toggles.
- Launch: FUN_004FCDD0 then calls FUN_004F3D10, as do the other modes' commits (7 callers): the step
  that starts loading the match. Hooked (tools/hooks.json "ms:launch", runtime/core/match_setup.c):
  the host records +0..+0x1908, +0x190C and the kits +0x191C (MS_SETUP_BYTES); a friend's own setup is
  replaced there.
  Verified: a friend that visited MY RUGBY and chose Portugal v Australia at Tbilisi plays the host's
  Scotland v New Zealand at Murrayfield; 1000 frames after launch its picture differs from the host's
  by 1.5 (mean per channel), its own match by 27 (tools/net/match_setup_check.sh).

## Camera (match view)
FUN_0073C080() (the CDirector, global 0x00A604F8) is 0 during the match: the view comes from the
CameraManager (vtable 0x8CCC94; +0xD0 the embedded LookAtCamera: +0x48 eye, +0x58 target, +0x68 up,
+0x04 the view matrix; +0x84 the projection, perspective at +0x34; +0x1E4 the controller). Once per
Present the controller's FUN_006E8260(manager, dt, camera, projection) computes 16 floats {eye xyz 1,
target xyz 1, up xyz 1, fov, aspect, near, far} (cm, degrees; FUN_006E9180, then shake/zoom modifiers
FUN_006EF020/040), applies them (FUN_00789BE0(camera, &eye, &target, &up); FUN_0075EC70(projection,
aspect / 1.3333 * fov, aspect, near, far); FUN_0075F800(projection, camera+4)) and keeps them at
controller+0x54. A friend re-applies the host's 16 floats after its own step
(game/overrides/match/camera_update.c, runtime/core/match_camera.c). The 2D overlay camera
(FUN_008267A0, 0x009FAE58) is separate.

## Controllers
CRugbyTeam+0xC0..+0xCC CPlayerControl*[4] (one per human on that side, 0 = none).
CPlayerControl (vtable 0x008EE9D8): +0xC team, +0x10 controlled CRugbyPlayer* (0 at set pieces),
+0x14 current set-piece object (CRugbyRuck / CRugbyLineOut), +0x1C PhysicalInputBlock* (its pad).

## Replay system (PlayBack::CReplayManager, src/rep/replaymanager.cpp): the puppet path
The game records every match frame and can redraw the match from those records with its logic
stopped (instant replays, highlights). Found 2026-10-12 (snapshot during the f27100 cutscene).
- Replay manager: one heap object (0x024BFE00 in that run; HbsActor@Ai replay actors x30 at +0x12A0
  are its signature). +0x08 interpolation fraction (fixed point), +0x0C/+0x10 counters, +0x14..+0x1C
  the compressed record stream (records carry their u16 length, walkable both ways), +0x24 cursor,
  +0x28/+0x2C decoded frames A/B, +0x30 a single frame, +0x38 flags (bit 2: interpolate A->B, bit 3:
  single frame). While recording the cursor sits at the tail and A/B are the last two frames.
- Frame (0x118C bytes): +0x000 30 x 128-byte per-player animation block (ids, times, weights);
  +0xF00 30 x {x, y, z, w} with positions in 1/52.5 m (w is not the heading); +0x10E0 30 x u16 (sequence/state);
  +0x111C ball position (metres), +0x1128 ball 4x4 matrix; +0x1168 u8; +0x116C timestamp (s);
  +0x1170..+0x117C; +0x117D 8 controller-marker bytes (4 home, 4 away); +0x1188 flags.
- Code: FUN_0045A560 UpdatePlayers (thiscall on the manager: frames -> replay actors, ball, markers),
  FUN_0045B1F0 picks frames A/B/single, FUN_00459C00 advances playback by dt (decodes records),
  FUN_00458620 / FUN_004589AA Huffman delta decoders (1100 ints per frame), FUN_00457A40 / FUN_00457C40
  frame <-> packed ints (cm and radians as ints). Highlights save these records to disk.
- Frame B (+0x2C) is the frame the recorder wrote last: in live play all 30 players match the live
  ones within 0.8 cm and the ball exactly, at every sample (RT_MS_REPLAY_CHECK, 13500..19800); only
  cutscenes (recording paused, players staged or parked) differ. Slot k = team (k / 15), player k % 15.
  Positions are in 1/52.5 m units (52.5 = 1 / 0.01905: three quarters of an inch), the ball in metres.
- The records are portable and lossy: another game decodes a host's records with its own decoder
  (identical pose blocks and ball over 3310 frames from one raw key, RT_MS_STREAM_CHECK); positions come
  out in whole 1/52.5 m units (up to 2.7 cm off), headings in whole degrees. ~377 bytes per frame.
- The stream becomes a ring (the oldest records are dropped), so decoding must start from a known frame. Decoding: thiscall
  FUN_00458620(ecx = manager+0x3C, the Huffman tree; frame, record, add=1): frame += record's deltas
  (add=0 subtracts, for rewinding). Record: +0 u16 length to the next record, +2 u16 offset of the
  Huffman bits, raw bytes from +8, the length repeated before the next record (walk backwards).
- Who builds poses: live players through PoseSource<HbsActor> (ctor 0x477540), ~30 per frame from the
  recorder (behind protector trampolines at 0x45C330); in playback through ReplayPoseSource (ctor
  0x487410) from 0x455540, the pose coming from frame A/B's 128-byte blocks via FUN_0045B6D0 ->
  FUN_0071AC50(out, blockA, blockB, t, team). Render players (StaticPlayer@PlayerManager x31) only
  hold transforms (position in cm at +0x74).
- Presentation in playback (pause menu > Replay): replay camera and a control bar, no score/clock.

## The friend side of the puppet (on.match_puppet, in progress)
- Playback mode is a separate game mode (BackendState+0x2220 0x16 -> 0x14, manager flag byte +0x38
  gains 0x0C, commander +0x18 camera focus): replay camera and control bar, no score/clock. So friends
  stay in the normal match and their live players take the host's frame instead.
- A frame's 128-byte player block is that player's animation channel (HbsSequenceChannel, player+0xDC):
  {.., .., -1, id 0x14, sequence 0x2B6, rate 1.875, time 86.08, weight 0.222, -2 end}. The time equals
  the live CannedSequenceInstance+0x28 (via player+0xDC -> +0x28 -> [0]) and a copy at channel+0xC.
- The match's per-frame step is CRugby vtable slot 1 = FUN_0062ED30 -> FUN_0062ED50(dt 0.02),
  thiscall: unless paused it advances the clock (setup+0x18F0, half length), match time (+0x58/+0x60),
  then ball FUN_00622B70, teams FUN_00655AE0/655A40, FUN_0062F270, rules FUN_0062E3A0, FUN_00603D70/E90,
  FUN_0064BF40, FUN_00682A20, the phase state machine (vtable+0x28), FUN_0062EBA0; then the camera/
  replay block FUN_0073C080..; then actors FUN_004046F0, FUN_00405570, FUN_00401480, FUN_0062EB50;
  always FUN_00404C90, FUN_006231F0, FUN_0062E900. A friend overrides this function (game/overrides)
  to run presentation and animation but take positions, channels and the ball from the host's frame.
- Root of the replay system: BackendState* at 0x009B65EC, +0x233C PlayBackCommander, +4 manager.
- The recorder is FUN_0045AB60 (thiscall on the manager): it clears frame B (or +0x30), then per live
  player writes the actor position (FUN_004377F0 -> FUN_0042C2D0), the heading (FUN_004AC650, which
  normalises it with the loop FUN_004AC5B0: a huge heading never leaves it) and the pose block
  (FUN_0071A2A0(actor+4, &frame[k*0x80])). Frame B is uninitialised until the first record is written
  (time 2e-44, headings 1e33): record only once the stream grew (match_replay.c).
- Posing: FUN_004039B0(actor, channel, k), per actor per frame. When the commander plays a replay
  (FUN_00457690) and k < 30, the actor (the live one: there are no separate replay bodies on screen) is
  posed by a ReplayPoseSource{commander, rig, k} (ctor 0x487410, vf0 0x405020: bind pose then
  FUN_0045B6D0 decoding block k of frame A, blended with B by +8); otherwise by its own PoseSource{rig,
  flag} (ctor 0x477540, vf0 0x405510). Both are temporaries. The 30 HbsActor objects at manager+0x12A0
  are not the live players' actors (CRugbyPlayer+0xD8, same class 0x008D7068, actor+4 the
  HbsSequenceChannel 0x008D7144).
- Phase: CRugby+0x9C AiStateMachine, +4 the current state; states are built once by the factory
  (RugbyStateFactory, CRugby+0xA0, ctor FUN_0068CD70) and found by id (state+8: Free 7, Kickoff 8,
  ...). FUN_0062DA80(id) (thiscall on CRugby) is the change every caller uses (referee, set pieces,
  phase states): factory get, machine switch FUN_00610850, event 0x18 and listeners.
- Friend (measured 2026-10-12 with tools/net/match_compare.py, a host recording and the friend's own
  recorder): skipping CRugby::Update's logic, posing through the replay branch with the host frame as
  frame A (game/overrides/match/pose_player.c, replay_playing.c), and placing the actors (vtable slot
  0x1C heading, FUN_0070ED50 position) and the ball (FUN_0044B9E0 position, FUN_00620800 matrix) after
  the presentation step, the friend's recorder writes the host's positions and headings exactly (0 cm,
  0.0) and the ball exactly at every sample. The camera follows the phase (see above).

## Player motion is animation-driven
No velocity is stored in CRugbyPlayer or any object it points at (per-frame position deltas correlate
with no field, r < 0.7, 22k samples): FIFAANIM root motion moves the players. So the state to replicate
per player is position, heading and the animation, not a velocity. Player sub-objects: +0xBC
PlayerStateMachine, +0xF8 MoveGroupInstance, +0x19C CannedSequenceInstance, +0x180..+0x188 MoveGroup*
(the moves available), +0xDC HbsSequenceChannel. Player +0x21C and the instance's +0x28 advance
0.9167 per frame (55/60): animation time. To do: the instance's sequence id and how the game picks it.

## Still to find
RNG, animation id per player, which pad index each PhysicalInputBlock is.

## Probes (runtime/debug/match_probe.c)
RT_MS_SCAN=F,... objects by vtable + image refs; RT_MS_DUMP=A-B/S with RT_MS_DUMP_EXTRA=VA:SIZE,... and
RT_MS_DUMP_SUB=PLAYEROFF:SIZE,... -> logs/ms/dump.bin; RT_MS_SNAP=F,... whole image+heap -> logs/ms/snap_F.bin.
