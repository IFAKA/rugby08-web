# game/overrides/ — game functions written in C

`math/` hot vector and matrix helpers.

One function per file, grouped by area (`match/`, `menus/`, `input/`, ...). Each file starts with a
comment: the function's name (`tools/query/fn.sh`), what the original does, and why it is replaced.

An override that replaces a function whole (never calls its `_lifted` body) starts with
`RECOMP_ENTER(0xVA)`: under RT_DET the guest scheduler yields every 4096 function entries, so a function
that stops counting its own entry moves every later thread switch and the state hashes part (seen with
math/vec3_transform.c: same arithmetic, hashes apart from frame 13080 until the entry was counted).

Only part of a function: `GAME_BLOCK(start, end) { ... }` (game/include/game.h) replaces the lifted code
from `start` up to `end` inside whatever function holds it (the hot loop, while the setup around it stays
lifted; like N64Recomp's instruction hooks). The lift fails if any jump from outside lands inside the
range. The block leaves the registers, flags and frame bytes as that code would; calls inside it
push the dummy return address and restore `g_cur_func`, as RECOMP_CALL does.
