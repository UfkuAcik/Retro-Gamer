/*
 * GW Bridge — Game & Watch LCD emulator bridge
 */

#include "gw_bridge.h"
#include "gw-emulator/gw_system.h"
#include "gw-emulator/gw_romloader.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

static uint16_t *gw_fb = NULL;
static uint32_t  gw_buttons_state = 0;

// ROM data — actual storage (gw_romloader.c references these via rom_manager.h extern)
unsigned char *GW_ROM_DATA = NULL;
unsigned int GW_ROM_DATA_LENGTH = 0;

// gw_system uses gw_get_buttons() to read input
unsigned int gw_get_buttons(void)
{
    return gw_buttons_state;
}

bool gw_bridge_init(void)
{
    // GW renders to 320x240 RGB565
    gw_fb = (uint16_t *)heap_caps_calloc(GW_SCREEN_W * GW_SCREEN_H, 2, MALLOC_CAP_SPIRAM);
    if (!gw_fb) {
        printf("[GW] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    gw_system_sound_init();

    printf("[GW] Initialized.\n");
    return true;
}

int gw_bridge_load_rom(const char *path)
{
    printf("[GW] Loading ROM: %s\n", path);

    // Read ROM file into memory
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[GW] Failed to open: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    GW_ROM_DATA_LENGTH = ftell(f);
    fseek(f, 0, SEEK_SET);

    GW_ROM_DATA = (unsigned char *)heap_caps_malloc(GW_ROM_DATA_LENGTH, MALLOC_CAP_SPIRAM);
    if (!GW_ROM_DATA) {
        printf("[GW] Failed to allocate ROM buffer!\n");
        fclose(f);
        return -1;
    }

    fread(GW_ROM_DATA, 1, GW_ROM_DATA_LENGTH, f);
    fclose(f);

    // Configure and start emulator
    if (!gw_system_config()) {
        printf("[GW] Config failed!\n");
        return -1;
    }

    if (!gw_system_romload()) {
        printf("[GW] ROM load failed!\n");
        return -1;
    }

    gw_system_start();

    printf("[GW] ROM loaded. Size: %u bytes\n", GW_ROM_DATA_LENGTH);
    return 0;
}

void gw_bridge_run_frame(void)
{
    gw_system_run(GW_SYSTEM_CYCLES);
    gw_system_blit(gw_fb);
}

uint16_t* gw_bridge_get_framebuffer(void)
{
    return gw_fb;
}

void gw_bridge_set_input(uint32_t buttons)
{
    gw_buttons_state = 0;
    if (buttons & GW_BTN_LEFT)  gw_buttons_state |= GW_BUTTON_LEFT;
    if (buttons & GW_BTN_UP)    gw_buttons_state |= GW_BUTTON_UP;
    if (buttons & GW_BTN_RIGHT) gw_buttons_state |= GW_BUTTON_RIGHT;
    if (buttons & GW_BTN_DOWN)  gw_buttons_state |= GW_BUTTON_DOWN;
    if (buttons & GW_BTN_A)     gw_buttons_state |= GW_BUTTON_A;
    if (buttons & GW_BTN_B)     gw_buttons_state |= GW_BUTTON_B;
    if (buttons & GW_BTN_TIME)  gw_buttons_state |= GW_BUTTON_TIME;
    if (buttons & GW_BTN_GAME)  gw_buttons_state |= GW_BUTTON_GAME;
}

void gw_bridge_shutdown(void)
{
    gw_system_reset();
    if (gw_fb) { heap_caps_free(gw_fb); gw_fb = NULL; }
    if (GW_ROM_DATA) { heap_caps_free(GW_ROM_DATA); GW_ROM_DATA = NULL; }
}

bool gw_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool gw_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
