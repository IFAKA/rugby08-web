# game/overrides/ — game functions written in C

`math/` hot vector and matrix helpers · `match/` a friend's game showing the host's match · `menus/` the party's screens in the front end.

One function per file, grouped by area (`match/`, `menus/`, `input/`, ...). Each file starts with a
comment: the function's name (`tools/query/fn.sh`), what the original does, and why it is replaced.

An override that replaces a function whole (never calls its `_lifted` body) starts with
`RECOMP_ENTER(0xVA)`: under RT_DET the guest scheduler yields every 4096 function entries, so a function
that stops counting its own entry moves every later thread switch and the state hashes part (seen with
math/vec3_transform.c: same arithmetic, hashes apart from frame 13080 until the entry was counted).
