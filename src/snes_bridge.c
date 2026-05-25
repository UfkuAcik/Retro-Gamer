/*
 * SNES Bridge — snes9x emulator bridge
 * Provides stub functions required by snes9x and bridge API
 */

#include "snes_bridge.h"
#include "snes9x/snes9x.h"
#include "snes9x/memmap.h"
#include "snes9x/gfx.h"
#include "snes9x/apu.h"
#include "snes9x/soundux.h"
#include "snes9x/cpuexec.h"
#include "snes9x/ppu.h"
#include "snes9x/snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

static uint8_t *snes_screen_buf = NULL;
static int16_t *snes_audio_buf = NULL;
static uint32_t snes_joypad = 0;
static int snes_fb_height = SNES_SCREEN_HEIGHT;

#define SNES_AUDIO_BUF_LEN 1024

// ==== snes9x port/stub functions ====

bool S9xInitDisplay(void) {
    GFX.Pitch = SNES_SCREEN_WIDTH * 2;
    GFX.ZPitch = SNES_SCREEN_WIDTH;
    GFX.Screen = snes_screen_buf;
    GFX.SubScreen = (uint8_t*)heap_caps_calloc(GFX.Pitch * SNES_HEIGHT_EXTENDED, 1, MALLOC_CAP_SPIRAM);
    GFX.ZBuffer = (uint8_t*)heap_caps_calloc(GFX.ZPitch * SNES_HEIGHT_EXTENDED, 1, MALLOC_CAP_SPIRAM);
    GFX.SubZBuffer = (uint8_t*)heap_caps_calloc(GFX.ZPitch * SNES_HEIGHT_EXTENDED, 1, MALLOC_CAP_SPIRAM);
    return GFX.Screen && GFX.SubScreen && GFX.ZBuffer && GFX.SubZBuffer;
}

void S9xDeinitDisplay(void) {
    if (GFX.SubScreen) { heap_caps_free(GFX.SubScreen); GFX.SubScreen = NULL; }
    if (GFX.ZBuffer) { heap_caps_free(GFX.ZBuffer); GFX.ZBuffer = NULL; }
    if (GFX.SubZBuffer) { heap_caps_free(GFX.SubZBuffer); GFX.SubZBuffer = NULL; }
}

uint32_t S9xReadJoypad(int32_t port) {
    return port == 0 ? snes_joypad : 0;
}

bool S9xReadMousePosition(int32_t which, int32_t *x, int32_t *y, uint32_t *buttons) { return false; }
bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *buttons) { return false; }
bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *j) { (void)j; }

// ==== Bridge API ====

bool snes_bridge_init(int sample_rate)
{
    // SNES screen buffer (256 * 239 * 2 = ~122KB)
    snes_screen_buf = (uint8_t*)heap_caps_calloc(SNES_SCREEN_WIDTH * SNES_HEIGHT_EXTENDED, 2, MALLOC_CAP_SPIRAM);
    if (!snes_screen_buf) {
        printf("[SNES] ERROR: Failed to allocate screen buffer!\n");
        return false;
    }

    snes_audio_buf = (int16_t*)heap_caps_calloc(SNES_AUDIO_BUF_LEN * 2, sizeof(int16_t), MALLOC_CAP_SPIRAM);

    // Configure SNES settings
    memset(&Settings, 0, sizeof(Settings));
    Settings.CyclesPercentage = 100;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.SoundPlaybackRate = sample_rate;
    Settings.SoundInputRate = sample_rate;
    Settings.DisableSoundEcho = true; // Save CPU
    Settings.InterpolatedSound = false;
    Settings.APUEnabled = true;
    Settings.NextAPUEnabled = true;
    Settings.Shutdown = true;

    if (!S9xInitDisplay()) { printf("[SNES] Display init failed\n"); return false; }
    if (!S9xInitMemory()) { printf("[SNES] Memory init failed\n"); return false; }
    if (!S9xInitAPU()) { printf("[SNES] APU init failed\n"); return false; }
    if (!S9xInitSound(0, 0)) { printf("[SNES] Sound init failed\n"); return false; }
    if (!S9xInitGFX()) { printf("[SNES] GFX init failed\n"); return false; }

    printf("[SNES] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int snes_bridge_load_rom(const char *path)
{
    printf("[SNES] Loading ROM: %s\n", path);

    if (!LoadROM(path)) {
        printf("[SNES] ROM load failed!\n");
        return -1;
    }

    S9xSetPlaybackRate(Settings.SoundPlaybackRate);
    snes_fb_height = SNES_SCREEN_HEIGHT;

    printf("[SNES] ROM loaded: %s\n", Memory.ROMName);
    return 0;
}

void snes_bridge_run_frame(bool draw)
{
    IPPU.RenderThisFrame = draw;
    GFX.Screen = snes_screen_buf;
    S9xMainLoop();
}

uint16_t* snes_bridge_get_framebuffer(int *width, int *height)
{
    *width = SNES_SCREEN_WIDTH;
    *height = snes_fb_height;
    return (uint16_t*)snes_screen_buf;
}

int16_t* snes_bridge_get_audio(int *num_samples)
{
    S9xMixSamples((void*)snes_audio_buf, SNES_AUDIO_BUF_LEN * 2);
    *num_samples = SNES_AUDIO_BUF_LEN;
    return snes_audio_buf;
}

void snes_bridge_set_input(uint32_t buttons)
{
    snes_joypad = buttons;
}

void snes_bridge_shutdown(void)
{
    S9xDeinitDisplay();
    if (snes_screen_buf) { heap_caps_free(snes_screen_buf); snes_screen_buf = NULL; }
    if (snes_audio_buf) { heap_caps_free(snes_audio_buf); snes_audio_buf = NULL; }
}

bool snes_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool snes_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
