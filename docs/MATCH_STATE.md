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
  There is no stored score total: the HUD and the scorers list (FUN_00577e60) read this list.
- Per-player match stats in the team block's player records (100 bytes each, record 0 at block+0;
  CRugbyPlayer+0x10 points at its record): +0x48 u16 tries, +0x3E..+0x43 kick counters.

## Match setup: everything that defines a match (on.match_setup)
Two runs identical except for one Stadium Select choice differ, in static data, only in these bytes
(the rest is menu and heap noise, equal in every variant):
- +0x0000..+0x18C0 both teams' blocks (rosters, player records), written when Team Select is confirmed;
  +0x18C0..+0x18CD per-team extras (FUN_004BFBA0). No heap pointers: equal across runs with different
  heaps, so the bytes can move between games.
- +0x18D0.. conditions and wind (+0x18D8, +0x18DC, wind vector floats +0x18E0/+0x18E8), +0x18EC half
  length (index; 5 mins = 1, 10 mins = 2), +0x1902 stadium id (Murrayfield 29), +0x190C difficulty
  (Club 0, Pro 1): written by Stadium Select's commit FUN_004FCDD0 from its toggles.
- Launch: FUN_004FCDD0 then calls FUN_004F3D10, as do the other modes' commits (7 callers): the step
  that starts loading the match. Hooked (tools/hooks.json "ms:launch", runtime/core/match_setup.c):
  the host records +0..+0x1908 and +0x190C (MS_SETUP_BYTES); a friend's own setup is replaced there.
  Verified: a friend that visited MY RUGBY and chose Portugal v Australia at Tbilisi plays the host's
  Scotland v New Zealand at Murrayfield; 1000 frames after launch its picture differs from the host's
  by 1.5 (mean per channel), its own match by 27 (tools/net/match_setup_check.sh).

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
  +0xF00 30 x {x, y, z, heading} with positions in centimetres; +0x10E0 30 x u16 (sequence/state);
  +0x111C ball position (metres), +0x1128 ball 4x4 matrix; +0x1168 u8; +0x116C timestamp (s);
  +0x1170..+0x117C; +0x117D 8 controller-marker bytes (4 home, 4 away); +0x1188 flags.
- Code: FUN_0045A560 UpdatePlayers (thiscall on the manager: frames -> replay actors, ball, markers),
  FUN_0045B1F0 picks frames A/B/single, FUN_00459C00 advances playback by dt (decodes records),
  FUN_00458620 / FUN_004589AA Huffman delta decoders (1100 ints per frame), FUN_00457A40 / FUN_00457C40
  frame <-> packed ints (cm and radians as ints). Highlights save these records to disk.
- Recording only appends compressed records at the stream's cursor (frames A/B stay at whatever was
  last played back: 49.7 s while the live match was at 16200). Decoding: thiscall
  FUN_00458620(ecx = manager+0x3C, the Huffman tree; frame, record, add=1): frame += record's deltas
  (add=0 subtracts, for rewinding). Record: +0 u16 length to the next record, +2 u16 offset of the
  Huffman bits, raw bytes from +8, the length repeated before the next record (walk backwards).
- Who builds poses: live players through PoseSource<HbsActor> (ctor 0x477540), ~30 per frame from the
  recorder (behind protector trampolines at 0x45C330); in playback through ReplayPoseSource (ctor
  0x487410) from 0x455540, the pose coming from frame A/B's 128-byte blocks via FUN_0045B6D0 ->
  FUN_0071AC50(out, blockA, blockB, t, team). Render players (StaticPlayer@PlayerManager x31) only
  hold transforms (position in cm at +0x74).
- Presentation in playback (pause menu > Replay): replay camera and a control bar, no score/clock.

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
