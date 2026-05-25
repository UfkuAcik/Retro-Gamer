/*
 * GB Bridge Implementation
 * Connects gnuboy emulator to our FabGL-based system
 */

#include "gb_bridge.h"
#include "gnuboy/gnuboy.h"

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

static uint16_t *gb_framebuffer = NULL;
static int16_t  *gb_audiobuffer = NULL;
static int       gb_audio_samples = 0;

#define GB_AUDIO_BUFFER_LEN 2048

// Video callback - called by gnuboy when a frame is ready
static void gb_video_cb(void *buffer)
{
    // Framebuffer is already set via gnuboy_set_framebuffer
    (void)buffer;
}

// Audio callback - called by gnuboy with audio data
static void gb_audio_cb(void *buffer, size_t length)
{
    // length is in bytes, each sample is 4 bytes (stereo 16-bit)
    gb_audio_samples = length >> 2;  // Convert to sample count
}

bool gb_bridge_init(int sample_rate)
{
    // Allocate framebuffer in PSRAM (160*144*2 = 46080 bytes for RGB565)
    gb_framebuffer = (uint16_t *)heap_caps_calloc(GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT, 2, MALLOC_CAP_SPIRAM);
    if (!gb_framebuffer) {
        printf("[GB] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // Allocate audio buffer in PSRAM
    gb_audiobuffer = (int16_t *)heap_caps_calloc(GB_AUDIO_BUFFER_LEN * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!gb_audiobuffer) {
        printf("[GB] ERROR: Failed to allocate audio buffer!\n");
        return false;
    }

    // Initialize gnuboy: stereo 16-bit audio, RGB565 Big Endian video
    int ret = gnuboy_init(sample_rate, GB_AUDIO_STEREO_S16, GB_PIXEL_565_BE, &gb_video_cb, &gb_audio_cb);
    if (ret < 0) {
        printf("[GB] ERROR: gnuboy_init failed!\n");
        return false;
    }

    gnuboy_set_framebuffer(gb_framebuffer);
    gnuboy_set_soundbuffer(gb_audiobuffer, GB_AUDIO_BUFFER_LEN);

    printf("[GB] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int gb_bridge_load_rom(const char *path)
{
    printf("[GB] Loading ROM: %s\n", path);

    int ret = gnuboy_load_rom_file(path);
    if (ret < 0) {
        printf("[GB] ROM load failed! Error: %d\n", ret);
        return ret;
    }

    // Set default DMG palette for non-color games
    gnuboy_set_palette(GB_PALETTE_DMG);

    // Hard reset
    gnuboy_reset(true);

    printf("[GB] ROM loaded. HW type: %s\n",
           gnuboy_get_hwtype() == GB_HW_CGB ? "Game Boy Color" :
           gnuboy_get_hwtype() == GB_HW_SGB ? "Super Game Boy" : "Game Boy");

    return 0;
}

void gb_bridge_run_frame(bool draw)
{
    gnuboy_run(draw);
}

uint16_t* gb_bridge_get_framebuffer(void)
{
    return gb_framebuffer;
}

int16_t* gb_bridge_get_audio(int *num_samples)
{
    *num_samples = gb_audio_samples;
    return gb_audiobuffer;
}

void gb_bridge_set_input(uint32_t buttons)
{
    gnuboy_set_pad(buttons);
}

void gb_bridge_shutdown(void)
{
    gnuboy_free_rom();
    if (gb_framebuffer) { heap_caps_free(gb_framebuffer); gb_framebuffer = NULL; }
    if (gb_audiobuffer) { heap_caps_free(gb_audiobuffer); gb_audiobuffer = NULL; }
}

bool gb_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool gb_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}

// ============================================================================
//  GB Palette Control
// ============================================================================
static const char *gb_palette_names[] = {
    "Classic Green",   // GB_PALETTE_DMG
    "Pocket Gray",     // GB_PALETTE_MGB0
    "Pocket Light",    // GB_PALETTE_MGB1
    "GBC Auto",        // GB_PALETTE_CGB
    "SGB Auto",        // GB_PALETTE_SGB
    "Brown",           // 0
    "Red",             // 1
    "Dark Brown",      // 2
    "Pastel",          // 3
    "Orange",          // 4
    "Yellow",          // 5
    "Blue",            // 6
    "Dark Blue",       // 7
    "Grayscale",       // 8
    "Green",           // 9
    "Dark Green",      // 10
    "Inverted",        // 11
};

// Map our UI index to gnuboy palette enum
static const gb_palette_t gb_pal_map[] = {
    GB_PALETTE_DMG, GB_PALETTE_MGB0, GB_PALETTE_MGB1,
    GB_PALETTE_CGB, GB_PALETTE_SGB,
    GB_PALETTE_0, GB_PALETTE_1, GB_PALETTE_2, GB_PALETTE_3,
    GB_PALETTE_4, GB_PALETTE_5, GB_PALETTE_6, GB_PALETTE_7,
    GB_PALETTE_8, GB_PALETTE_9, GB_PALETTE_10, GB_PALETTE_11,
};

static int gb_current_pal_idx = 0; // UI index

void gb_bridge_set_palette(int palette_id) {
    int count = sizeof(gb_pal_map) / sizeof(gb_pal_map[0]);
    if (palette_id < 0 || palette_id >= count) return;
    gb_current_pal_idx = palette_id;
    gnuboy_set_palette(gb_pal_map[palette_id]);
    printf("[GB] Palette: %s\n", gb_palette_names[palette_id]);
}

int gb_bridge_get_palette(void) {
    return gb_current_pal_idx;
}

int gb_bridge_get_palette_count(void) {
    return sizeof(gb_pal_map) / sizeof(gb_pal_map[0]);
}

const char* gb_bridge_get_palette_name(int id) {
    int count = sizeof(gb_palette_names) / sizeof(gb_palette_names[0]);
    if (id < 0 || id >= count) return "Unknown";
    return gb_palette_names[id];
}

// ============================================================================
//  SRAM (Battery Save) Functions
// ============================================================================
bool gb_bridge_sram_dirty(void) {
    return gnuboy_sram_dirty();
}

bool gb_bridge_save_sram(const char *path) {
    if (!path || !*path) return false;
    int ret = gnuboy_save_sram(path, false);
    printf("[GB] SRAM saved: %s (ret=%d)\n", path, ret);
    return ret == 0;
}

bool gb_bridge_load_sram(const char *path) {
    if (!path || !*path) return false;
    int ret = gnuboy_load_sram(path);
    printf("[GB] SRAM loaded: %s (ret=%d)\n", path, ret);
    return ret == 0;
}
