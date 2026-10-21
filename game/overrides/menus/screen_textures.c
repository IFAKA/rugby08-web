/* sub_004EBD60 -- after FeSetCurrentScreen(id): keeps the loaded background texture packages in step
 * with the screen's registry entry (0x4EBD6A). For an id the registry does not know, its texture list
 * is null and its first loop never advances (a hang). The party's screens (runtime/core/fe_screens.c)
 * are drawn with the main menu's own textures: they go through as the main menu. */
#include "game.h"
#include "fe_party.h"

GAME_OVERRIDE(sub_004EBD60) {
    uint32_t id = GAME_ARG(0) & 0xFFFF;
    if (id == FE_SCR_ONLINE || id == FE_SCR_PARTY || id == FE_SCR_JOIN) wr32(g_esp + 4, FE_SCR_MAIN);
    sub_004EBD60_lifted();
}
