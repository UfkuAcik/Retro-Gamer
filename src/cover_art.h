/*
 * Cover Art Loader for Retro-Gamer
 * Loads cover art from /romart/{emu}/ in retro-go compatible format
 * Supports: filename-based .png (as raw) and .art (raw RGB565 LE)
 * 
 * For VGA8 (64 colors), we convert RGB565 -> SBGR2222 palette index
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Cover art dimensions (retro-go standard)
#define COVER_WIDTH   86
#define COVER_HEIGHT  120

// Initialize cover art system (call once)
void cover_init(void);

// Load cover art for a ROM file
// rom_path: full ROM path (e.g. "/roms/nes/Mario.nes")
// emu_short: emulator short name (e.g. "nes")
// out_buf: buffer for SBGR2222 pixel data (COVER_WIDTH * COVER_HEIGHT bytes)
// Returns true if cover found and loaded
bool cover_load(const char *rom_path, const char *emu_short, uint8_t *out_buf);

// Free any cached resources
void cover_free(void);

#ifdef __cplusplus
}
#endif
