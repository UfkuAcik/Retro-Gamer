/*
 * MSX Bridge — Stub implementation
 * Full fMSX integration requires BIOS files.
 * This stub allows the project to compile and shows an error
 * message when MSX ROMs are loaded without BIOS.
 */
#include "msx_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

static uint16_t *msx_framebuffer = NULL;
static bool msx_initialized = false;

bool msx_bridge_bios_available(void) {
    FILE *f = fopen("/retro-go/bios/MSX.ROM", "rb");
    if (f) { fclose(f); return true; }
    // Also try /bios/MSX.ROM
    f = fopen("/bios/MSX.ROM", "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool msx_bridge_init(int sample_rate) {
    (void)sample_rate;
    
    if (!msx_bridge_bios_available()) {
        printf("[MSX] ERROR: MSX.ROM BIOS not found!\n");
        printf("[MSX] Please place MSX.ROM in /retro-go/bios/\n");
        return false;
    }

    // Allocate framebuffer in PSRAM
    msx_framebuffer = (uint16_t *)heap_caps_calloc(MSX_WIDTH * MSX_HEIGHT, 2, MALLOC_CAP_SPIRAM);
    if (!msx_framebuffer) {
        printf("[MSX] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // TODO: Initialize fMSX core when BIOS is available
    // This requires porting fMSX from retro-go's fmsx/ component
    printf("[MSX] Stub initialized — full emulation pending BIOS + fMSX port\n");
    msx_initialized = true;
    return true;
}

bool msx_bridge_load_rom(const char *path) {
    if (!msx_initialized) return false;
    printf("[MSX] Would load ROM: %s\n", path);
    // TODO: Load cartridge/disk into fMSX
    // For now, display a "BIOS needed" message on the framebuffer
    // Write "MSX" text into framebuffer as a placeholder
    for (int y = 80; y < 112; y++) {
        for (int x = 96; x < 160; x++) {
            msx_framebuffer[y * MSX_WIDTH + x] = 0x07E0; // Green
        }
    }
    return true;
}

void msx_bridge_run_frame(bool draw) {
    (void)draw;
    // TODO: Run one frame of fMSX emulation
}

uint16_t* msx_bridge_get_framebuffer(void) {
    return msx_framebuffer;
}

int16_t* msx_bridge_get_audio(int *num_samples) {
    if (num_samples) *num_samples = 0;
    return NULL;
}

void msx_bridge_set_input(uint32_t buttons) {
    (void)buttons;
    // TODO: Map input to MSX keyboard/joystick
}

bool msx_bridge_save_state(const char *path) {
    (void)path;
    printf("[MSX] Save state not yet implemented\n");
    return false;
}

bool msx_bridge_load_state(const char *path) {
    (void)path;
    printf("[MSX] Load state not yet implemented\n");
    return false;
}

void msx_bridge_shutdown(void) {
    if (msx_framebuffer) {
        free(msx_framebuffer);
        msx_framebuffer = NULL;
    }
    msx_initialized = false;
    printf("[MSX] Shutdown\n");
}
