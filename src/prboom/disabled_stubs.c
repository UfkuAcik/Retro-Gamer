/*
 * Stub definitions for disabled prboom modules
 * Only d_server.c remains disabled (network multiplayer — not needed on ESP32)
 *
 * NOTE: OPL audio modules (dbopl.c, opl.c, oplplayer.c, opl_queue.c,
 * midifile.c, mus2mid.c) are now FULLY ENABLED and use PSRAM for
 * large lookup tables (WaveTable, MulTable, opl_chip).
 */

#include <stddef.h>
#include "doomtype.h"
#include "doomdef.h"
#include "d_event.h"
#include "doomstat.h"

/* ===== d_server.c stubs ===== */
/* Network multiplayer is not supported on ESP32 single-player device */
void D_CheckNetGame(void) {
    netgame = false;
    deathmatch = false;
    consoleplayer = displayplayer = 0;
}
