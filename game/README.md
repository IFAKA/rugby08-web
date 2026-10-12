# game/ — changing the game itself, in small hand-written files

The game's code is the lifted `gen/` (never edited). To change what a function does, override it here.

- `overrides/<area>/<name>.c`: one function per file, `GAME_OVERRIDE(sub_X)` (`include/game.h`). Find the
  function by name or address with `tools/query/fn.sh` (names: `tools/analysis/symbols.py`, from RTTI).
- After adding or removing an override: `tools/lift/lift.py` (14 s; renames the lifted body to sub_X_lifted),
  then build. Prove what it changes with `tools/check/regress.sh` (or what it must not change).
