/*
 * SMS Bridge Implementation
 * Connects smsplus Sega Master System / Game Gear emulator to FabGL
 */

#include "sms_bridge.h"
#include "smsplus/shared.h"

#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

static uint8_t *sms_fb = NULL;
static uint16_t sms_palette_cache[32]; // RGB565 palette cache

bool sms_bridge_init(int sample_rate)
{
    // Allocate framebuffer in PSRAM (256*192 = 49152 bytes, 8-bit paletted)
    sms_fb = (uint8_t *)heap_caps_calloc(SMS_SCREEN_WIDTH * SMS_SCREEN_HEIGHT, 1, MALLOC_CAP_SPIRAM);
    if (!sms_fb) {
        printf("[SMS] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // Configure smsplus options
    system_reset_config();
    option.sndrate = sample_rate;
    option.overscan = 0;
    option.extra_gg = 0;
    option.console = 0;  // Auto-detect from ROM

    printf("[SMS] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int sms_bridge_load_rom(const char *path)
{
    printf("[SMS] Loading ROM: %s\n", path);

    // Detect console type from extension
    const char *ext = strrchr(path, '.');
    if (ext) {
        ext++;
        if (strcasecmp(ext, "gg") == 0) {
            option.console = 3; // Game Gear
        } else if (strcasecmp(ext, "sg") == 0) {
            option.console = 5; // SG-1000
        } else if (strcasecmp(ext, "col") == 0) {
            option.console = 6; // Colecovision
        } else {
            option.console = 0; // Auto (SMS)
        }
    }

    if (!load_rom_file(path)) {
        printf("[SMS] ROM load failed!\n");
        return -1;
    }

    // Set up bitmap for rendering
    bitmap.width = SMS_SCREEN_WIDTH;
    bitmap.height = SMS_SCREEN_HEIGHT;
    bitmap.pitch = bitmap.width;
    bitmap.data = sms_fb;

    // Power on the system
    system_poweron();

    // Sync initial palette
    for (int i = 0; i < PALETTE_SIZE; i++)
        palette_sync(i);
    render_copy_palette(sms_palette_cache);

    printf("[SMS] ROM loaded. Console: %s, Viewport: %dx%d\n",
           IS_GG ? "Game Gear" : "Master System",
           bitmap.viewport.w, bitmap.viewport.h);

    return 0;
}

void sms_bridge_run_frame(bool draw)
{
    system_frame(!draw);
    if (draw) {
        render_copy_palette(sms_palette_cache);
    }
}

uint8_t* sms_bridge_get_framebuffer(int *width, int *height)
{
    *width = bitmap.viewport.w;
    *height = bitmap.viewport.h;
    return sms_fb + bitmap.viewport.x;
}

uint16_t* sms_bridge_get_palette(void)
{
    return sms_palette_cache;
}

void sms_bridge_get_audio(int16_t **left, int16_t **right, int *num_samples)
{
    *left = snd.stream[0];
    *right = snd.stream[1];
    *num_samples = snd.sample_count;
}

void sms_bridge_set_input(uint32_t buttons)
{
    input.pad[0] = 0x00;
    input.pad[1] = 0x00;
    input.system = 0x00;

    if (buttons & SMS_BTN_UP)    input.pad[0] |= INPUT_UP;
    if (buttons & SMS_BTN_DOWN)  input.pad[0] |= INPUT_DOWN;
    if (buttons & SMS_BTN_LEFT)  input.pad[0] |= INPUT_LEFT;
    if (buttons & SMS_BTN_RIGHT) input.pad[0] |= INPUT_RIGHT;
    if (buttons & SMS_BTN_A)     input.pad[0] |= INPUT_BUTTON2;
    if (buttons & SMS_BTN_B)     input.pad[0] |= INPUT_BUTTON1;

    if (IS_GG) {
        if (buttons & SMS_BTN_START) input.system |= INPUT_START;
    } else {
        if (buttons & SMS_BTN_START) input.system |= INPUT_PAUSE;
    }
}

void sms_bridge_shutdown(void)
{
    system_poweroff();
    system_shutdown();
    if (sms_fb) { heap_caps_free(sms_fb); sms_fb = NULL; }
}

bool sms_bridge_is_gamegear(void)
{
    return IS_GG ? true : false;
}

bool sms_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool sms_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
