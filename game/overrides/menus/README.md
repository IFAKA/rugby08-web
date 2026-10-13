# game/overrides/menus/ — the front end, in C (pt.menu)

The party's screens live in the game's own front end (runtime/core/fe_screens.c, fe_party.c):
- `screen_file.c`: the screen file loader (0x5A06D0): the main menu gets its ONLINE item (RT_FE_DUMP=DIR writes
  every screen file it parses).
- `screen_load.c`: the registry's load (0x4EBAE0): the party's screen ids, which the registry does not know.
- `screen_textures.c`: the texture step after a screen switch (0x4EBD60): the party's screens use the main menu's.
- `party_action.c`: an unused developer callback (0x574380) that the party's buttons name as their X press.
