/*
 * MSX Bridge — Connects fMSX emulator to FabGL VGA/Audio/Input
 * For Olimex ESP32-SBC-FabGL Rev B
 * 
 * NOTE: MSX emulation requires BIOS files in /retro-go/bios/:
 *   MSX.ROM   - MSX1 BIOS (required)
 *   MSX2.ROM  - MSX2 BIOS (optional)
 *   MSX2EXT.ROM - MSX2 Extended BIOS (optional)
 *   DISK.ROM  - Disk BIOS (optional, for .dsk files)
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// MSX display constants
#define MSX_WIDTH   256
#define MSX_HEIGHT  192

// Audio
#define MSX_AUDIO_SAMPLE_RATE 22050

// Initialize the MSX emulator
bool msx_bridge_init(int sample_rate);

// Load a ROM/DSK file
bool msx_bridge_load_rom(const char *path);

// Run one frame of emulation
void msx_bridge_run_frame(bool draw);

// Get framebuffer (RGB565)
uint16_t* msx_bridge_get_framebuffer(void);

// Get audio buffer
int16_t* msx_bridge_get_audio(int *num_samples);

// Set input buttons
void msx_bridge_set_input(uint32_t buttons);

// Save/load state
bool msx_bridge_save_state(const char *path);
bool msx_bridge_load_state(const char *path);

// Shutdown
void msx_bridge_shutdown(void);

// Check if MSX BIOS is available on SD card
bool msx_bridge_bios_available(void);

// MSX key mappings
#define MSX_KEY_UP     0x01
#define MSX_KEY_DOWN   0x02
#define MSX_KEY_LEFT   0x04
#define MSX_KEY_RIGHT  0x08
#define MSX_KEY_FIRE_A 0x10
#define MSX_KEY_FIRE_B 0x20
#define MSX_KEY_START  0x40
#define MSX_KEY_SELECT 0x80

#ifdef __cplusplus
}
#endif
