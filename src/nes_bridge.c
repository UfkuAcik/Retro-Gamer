/*
 * NES Bridge Implementation
 * Connects nofrendo NES emulator to our FabGL-based system
 */

#include "nes_bridge.h"
#include "nofrendo.h"
#include "nes/nes.h"
#include "nes/input.h"
#include "nes/state.h"

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

static nes_t *nes = NULL;
static uint8_t *nes_framebuffer = NULL;
static uint32_t current_buttons = 0;

// Blit callback — nofrendo calls this when a frame is ready
static void blit_callback(uint8 *bmp)
{
    // Nothing to do here — we read nes->vidbuf directly
    (void)bmp;
}

bool nes_bridge_init(int sample_rate)
{
    // Allocate framebuffer in PSRAM
    nes_framebuffer = (uint8_t *)heap_caps_calloc(NES_PITCH * NES_HEIGHT, 1, MALLOC_CAP_SPIRAM);
    if (!nes_framebuffer) {
        printf("[NES] ERROR: Failed to allocate framebuffer in PSRAM!\n");
        return false;
    }

    // Initialize NES with auto-detect system, requested sample rate, mono
    nes = nes_init(SYS_DETECT, sample_rate, false, NULL);
    if (!nes) {
        printf("[NES] ERROR: nes_init failed!\n");
        return false;
    }

    // Set our blit callback
    nes->blit_func = blit_callback;

    // Set the video buffer to our PSRAM-allocated framebuffer
    nes_setvidbuf(nes_framebuffer);

    printf("[NES] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int nes_bridge_load_rom(const char *path)
{
    if (!nes) return -1;

    printf("[NES] Loading ROM: %s\n", path);

    int ret = nes_loadfile(path);

    if (ret == 0) {
        printf("[NES] ROM loaded successfully. System: %s, Refresh: %d Hz\n",
               nes->system == SYS_NES_PAL ? "PAL" : "NTSC",
               nes->refresh_rate);

        // Run two dummy frames for state initialization (as retro-go does)
        nes_emulate(false);
        nes_emulate(false);
    } else {
        printf("[NES] ROM load failed! Error code: %d\n", ret);
    }

    return ret;
}

uint8_t* nes_bridge_run_frame(bool draw)
{
    if (!nes) return NULL;

    // Update controller input
    input_update(0, current_buttons);

    // Run one frame
    nes_emulate(draw);

    return nes_framebuffer;
}

int16_t* nes_bridge_get_audio(int *num_samples)
{
    if (!nes || !nes->apu) {
        *num_samples = 0;
        return NULL;
    }
    *num_samples = nes->apu->samples_per_frame;
    return nes->apu->buffer;
}

void nes_bridge_set_input(uint32_t buttons)
{
    current_buttons = buttons;
}

void nes_bridge_shutdown(void)
{
    if (nes) {
        nes_shutdown();
        nes = NULL;
    }
    if (nes_framebuffer) {
        heap_caps_free(nes_framebuffer);
        nes_framebuffer = NULL;
    }
}

int nes_bridge_get_refresh_rate(void)
{
    return nes ? nes->refresh_rate : 60;
}

bool nes_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool nes_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
