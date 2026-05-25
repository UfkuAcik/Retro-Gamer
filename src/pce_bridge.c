/*
 * PCE Bridge — PC Engine / TurboGrafx-16 emulator bridge
 * Implements osd_* callbacks required by pce-go core
 */

#include "pce_bridge.h"
#include "pce-go/pce-go.h"
#include "pce-go/pce.h"
#include "pce-go/psg.h"

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

static uint8_t  *pce_fb = NULL;
static uint8_t   pce_input_state = 0;
static int       pce_fb_width = 256;
static int       pce_fb_height = 224;
static bool      pce_frame_done = false;
static uint16_t  pce_pal_cache[256];

// OSD callbacks required by pce-go core
uint8_t *osd_gfx_framebuffer(int width, int height)
{
    if (width > 0 && height > 0) {
        pce_fb_width = width;
        pce_fb_height = height;
    }
    return pce_fb;
}

void osd_input_read(uint8_t joypads[8])
{
    joypads[0] = pce_input_state;
    for (int i = 1; i < 8; i++) joypads[i] = 0;
}

void osd_vsync(void)
{
    pce_frame_done = true;
}

bool pce_bridge_init(int sample_rate)
{
    // Allocate framebuffer in PSRAM (352+16) * 242 = ~89KB
    pce_fb = (uint8_t *)heap_caps_calloc(XBUF_WIDTH * XBUF_HEIGHT, 1, MALLOC_CAP_SPIRAM);
    if (!pce_fb) {
        printf("[PCE] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    if (InitPCE(sample_rate, false) != 0) {
        printf("[PCE] ERROR: InitPCE failed!\n");
        return false;
    }

    // Build palette (RGB565)
    uint16_t *pal = (uint16_t *)PalettePCE(16);
    if (pal) {
        for (int i = 0; i < 256; i++) {
            // PCE palette is in native endian, swap bytes for our usage
            pce_pal_cache[i] = (pal[i] << 8) | (pal[i] >> 8);
        }
        free(pal);
    }

    printf("[PCE] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int pce_bridge_load_rom(const char *path)
{
    printf("[PCE] Loading ROM: %s\n", path);

    int ret = LoadFile(path);
    if (ret != 0) {
        printf("[PCE] ROM load failed! Error: %d\n", ret);
        return ret;
    }

    printf("[PCE] ROM loaded. Screen: %dx%d\n", pce_fb_width, pce_fb_height);
    return 0;
}

void pce_bridge_run_frame(void)
{
    pce_frame_done = false;
    // pce_run() runs CPU for one frame worth of cycles then returns
    pce_run();
}

uint8_t* pce_bridge_get_framebuffer(int *width, int *height)
{
    *width = pce_fb_width;
    *height = pce_fb_height;
    return pce_fb + 16; // Skip the 16-byte scratch area
}

uint16_t* pce_bridge_get_palette(void)
{
    return pce_pal_cache;
}

void pce_bridge_set_input(uint32_t buttons)
{
    pce_input_state = (uint8_t)buttons;
}

void pce_bridge_shutdown(void)
{
    ShutdownPCE();
    if (pce_fb) { heap_caps_free(pce_fb); pce_fb = NULL; }
}

bool pce_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool pce_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
