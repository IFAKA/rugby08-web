# Rugby 08 PC controls (authoritative)

Sources, in order of authority:
1. The game's own keyboard map, `FUN_00821AF0` (device type 8 = keyboard), which binds logical pad
   buttons to DirectInput keys, and the per-frame reader `FUN_0081FD80`, which turns them into
   controller bits. Mouse = device type 9, joypads = type 5/1 (`Data/controltypes.csv`).
2. The official EA manual (PS2, archive.org `ps2_Rugby_08_USA`, pages 1, 3 and 4): what each pad
   button does on each screen.
3. The PC readme (keys named for Buttons 1-8) agrees with 1 and 2 everywhere it overlaps.

## Keyboard (logical button -> key -> PS2 button)

| Logical | Key | PS2 | Bit |
|---|---|---|---|
| 0 | S | X (cross) | 0x1 |
| 1 | Q | Square | 0x2 |
| 2 | W | Triangle | 0x4000 |
| 3 | E | Circle | 0x8000 |
| 10 | A | L1 | 0x100 |
| 13 | D | R1 | 0x800 |
| 11 | Left Shift | L2 | 0x200 |
| 14 | Space | R2 | 0x1000 |
| 12 | Left Ctrl | START | 0x400 |
| 15 | Left Alt | SELECT | 0x2000 |
| 4 | Esc | (keyboard only) | 0x4 |
| 5 | Enter | (keyboard only) | 0x8 |
| 6/7/8/9 | Up/Down/Left/Right arrows | left stick | 0x10-0x80 |
| 16/17/18/19 | Numpad 4/6/8/2 | right stick | 0x10000-0x80000 |

No key is bound to L3/R3 (manual: collapse scrum/maul, use hands, kill ball).
The right stick (sidestep, hand-off, shoulder charge) needs a numeric keypad.

## What each button does (manual), with the keyboard key

Attacking: run arrows, sprint Space, pass left/right A/D (hold = long pass, tap before receiving =
quick pass, A then D = dummy), offload S (when prompted), score try S, sidestep/hand-off/shoulder
charge numpad (90 / 180 / 0 degrees from running direction), dive on loose ball S, defensive
touchdown E (behind own line).

Defending: run arrows, sprint Space, change player Left Shift, tackle = run into the carrier, high
tackle hold D then run in, dive tackle S, jump to catch E, call a mark Q (behind own 22).

Set plays (d-pad on PS2): the arrows are the keyboard's left stick; no separate d-pad keys.

Scrums: hook Q, drive arrows. Attacking: feed S, pass to backs A/D, pick-up and run Q/E,
scrum-half kick W.
Rucks: bind S. Attacking: pick-up and kick W, pass to backs A/D, pass to forwards Left Shift/Space,
pick-up and run Q/E.
Mauls: drive arrows, bind S. Attacking: kick W, pass to backs A/D, to forwards Left Shift/Space,
second-row run Q/E.
Line-outs: throw / intercept to front/middle/back S/E/W, angle the throw Left/Right.
Kicking (hold to power, arrows to direct): punt S, grubber W, up-and-under Q, drop goal E.
Place kicks: aim arrows, start/stop the gauge S. Tap kicks: S/Q/E.
Pause: START (Left Ctrl) on the keyboard; the readme and forum players say Esc (logical 4, bit 0x4).

## Mouse (device type 9)

Movement is the left stick; left/middle/right buttons are logical 0/2/3 (S/W/E equivalents).
Menus: the mouse drives the pointer.

## Devices and team selection

Team Select assigns the device you move left/right to a team. The device you assign is your match
controller: assign with the mouse and the mouse is your "pad" (no free cursor, no keyboard pause);
assign with the keyboard to use the keys above.

Known bug (2026-10-10, open): in our build the keyboard's Left/Right reach the game (controller bits
0x40/0x80 and front-end messages 6/4) but never move the keyboard icon at Team Select, so only the
mouse can be assigned. Up/Down call TeamSelectToggleTeamUp/Down (they return "not handled" while
the icon is in the middle column). See PROGRESS.md.
