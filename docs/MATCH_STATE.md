# The live match in guest memory (on.match_map, in progress)

Found with `runtime/core/match_state.c` probes (RT_MS_SCAN, RT_MS_DUMP, RT_MS_SNAP) on the native
det run of `tests/replays/match_play.txt` (keyboard home team, kickoff ~15000, rucks/scrums/lineouts
up to 30000), RTTI vtables (`analysis/vtables.json`) and Ghidra constructors. Units: metres, seconds.

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
| +0x70 | CRugbyRuck* (0 when no ruck) |
| +0x84 | Setplay Overlord*, +0x88 CollisionMaster*, +0x8C PathFinder* |
| +0x98 | BallManager* |
| +0x9C | AiStateMachine<CRugby>*: +4 = current state; its vtable names the phase (KickoffState, FormationState, RunState, FreeState, LineOutState, PuntState, GoalKickState, DropOut22State, IKickState, QuickOptionState, EmptyState) |
| +0xB8 | CReferee* |
| +0xDC | Difficulty Manager*, +0xE4 Impact Manager* |

## CRugbyTeam (vtable 0x008E6734, 0x1A4 bytes, ctor FUN_00615370(rugby, side, 15))
+0xC team setup block (in 0x009B6830), +0x10..+0x20 player pointers that change (roles: carrier etc.),
+0xD4 CRugbyPlayer*[15], +0x11C side byte, +0x128 player count byte, +0x18C another team (opponent).

## CRugbyPlayer (vtable 0x008E5E00, 0x240 bytes, ctor FUN_0060a1a0(team, number 1..15))
+0x0A index byte, +0x78 shirt number byte, +0x90 team*, +0x84/+0x88/+0x8C live position x/y/z
(matches the ball while held), +0xA0 heading (degrees), +0xA4/+0xAC and +0xB0/+0xB8 target/previous
positions, +0x3C stamina-like (84..100). To do: velocity, animation id/time, role.

## BallManager (vtable 0x008E6B74, 0x1DC bytes, ctor FUN_00621ca0)
+0x30/+0x34/+0x38 position, +0x3C/+0x40/+0x44 velocity (in flight; gravity in y), +0x58 3x3 rotation,
+0x114 holder CRugbyPlayer* (0 when loose/in flight; within 0.6 m of the ball 95% of frames),
+0x118 last holder, +0x11C previous holder.

## Still to find
Score, match clock (HUD shows mm:ss), controlled player per pad, RNG, animation state per player.
