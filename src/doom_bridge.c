/*
 * DOOM Bridge — prboom engine integration for FabGL/ESP32
 *
 * Architecture: DOOM runs D_DoomMain() which enters D_DoomLoop() — an infinite loop.
 * We run this on a separate FreeRTOS task and synchronize via a frame-ready flag.
 * The I_* platform functions are implemented here instead of in main.c.
 */

#include "doom_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/* prboom headers */
#include "prboom/doomtype.h"
#include "prboom/doomstat.h"
#include "prboom/doomdef.h"
#include "prboom/d_main.h"
#include "prboom/g_game.h"
#include "prboom/i_system.h"
#include "prboom/i_video.h"
#include "prboom/i_sound.h"
#include "prboom/i_main.h"
#include "prboom/m_argv.h"
#include "prboom/m_fixed.h"
#include "prboom/m_misc.h"
#include "prboom/r_draw.h"
#include "prboom/r_fps.h"
#include "prboom/s_sound.h"
#include "prboom/st_stuff.h"
#include "prboom/info.h"
#include "prboom/z_zone.h"
#include "prboom/w_wad.h"
#include "prboom/v_video.h"
#include "prboom/sounds.h"

/* ================================================================ */
/*  Static data                                                      */
/* ================================================================ */

static uint8_t *doom_framebuffer = NULL;  // SCREENWIDTH * SCREENHEIGHT, 8-bit indexed
static uint16_t doom_palette[256];         // Current palette in RGB565
static volatile uint32_t doom_buttons = 0;
static volatile bool doom_frame_ready = false;
static volatile bool doom_running = false;
static TaskHandle_t doom_task_handle = NULL;

// Variables expected by prboom
int snd_card = 1;
int mus_card = 1;
int snd_samplerate = 11025;
int current_palette = 0;

// DOOM argv
static const char *doom_argv[8];

/* ================================================================ */
/*  I_Video — Platform video implementation                          */
/* ================================================================ */

void I_StartFrame(void) { }
void I_UpdateNoBlit(void) { }

void I_FinishUpdate(void)
{
    doom_frame_ready = true;
    // Yield to let the main loop blit
    vTaskDelay(1);
}

bool I_StartDisplay(void) { return true; }
void I_EndDisplay(void) { }

void I_SetPalette(int pal)
{
    // Build palette from WAD and convert to RGB565
    uint16_t *palette = V_BuildPalette(pal, 16);
    if (palette) {
        for (int i = 0; i < 256; i++)
            doom_palette[i] = palette[i];
        Z_Free(palette);
    }
    current_palette = pal;
}

void I_InitGraphics(void)
{
    for (int i = 0; i < 3; i++) {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENWIDTH;
    }
    screens[0].data = doom_framebuffer;
    screens[0].not_on_heap = true;

    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENWIDTH;
}

void I_ShutdownGraphics(void) { }

void I_UpdateVideoMode(void) {
    I_InitGraphics();
}

/* ================================================================ */
/*  I_System — Platform system implementation                        */
/* ================================================================ */

int I_GetTimeMS(void)
{
    return (int)(esp_timer_get_time() / 1000);
}

int I_GetTime(void)
{
    return I_GetTimeMS() * TICRATE * realtic_clock_rate / 100000;
}

void I_uSleep(unsigned long usecs)
{
    vTaskDelay(usecs / 1000 / portTICK_PERIOD_MS + 1);
}

void I_SafeExit(int rc)
{
    printf("[DOOM] Exit requested with code %d\n", rc);
    doom_running = false;
    vTaskDelete(NULL);
}

const char *I_DoomExeDir(void)
{
    return "/sd/doom";
}

const char *I_SigString(char *buf, size_t sz, int signum)
{
    snprintf(buf, sz, "signal %d", signum);
    return buf;
}

/* ================================================================ */
/*  I_Sound — Minimal sound (stubs for now, audio through DAC later) */
/* ================================================================ */

void I_InitSound(void)
{
    snd_channels = 8;
    snd_samplerate = 11025;
    printf("[DOOM] Sound initialized (minimal)\n");
}

void I_ShutdownSound(void) { }
void I_SetChannels(void) { }
int I_GetSfxLumpNum(sfxinfo_t *sfxinfo) { return sfxinfo->lumpnum; }

int I_StartSound(int id, int channel, int vol, int sep, int pitch, int priority)
{
    return channel;
}

void I_StopSound(int handle) { }
bool I_SoundIsPlaying(int handle) { return false; }
bool I_AnySoundStillPlaying(void) { return false; }
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) { }

void I_InitMusic(void) { }
void I_ShutdownMusic(void) { }
void I_UpdateMusic(void) { }
void I_SetMusicVolume(int volume) { }
void I_PauseSong(int handle) { }
void I_ResumeSong(int handle) { }
void I_StopSong(int handle) { }
void I_UnRegisterSong(int handle) { }
int I_RegisterSong(const void *data, size_t len) { return 0; }
void I_PlaySong(int handle, int looping) { }

/* ================================================================ */
/*  I_Input — Keyboard input from PS/2                               */
/* ================================================================ */

// Key variables from g_game.c (extern)
extern int key_right, key_left, key_up, key_down;
extern int key_fire, key_use, key_strafe, key_speed;
extern int key_escape, key_enter, key_map, key_weapontoggle;
extern int key_backspace;

void I_StartTic(void)
{
    static uint32_t prev_buttons = 0;
    uint32_t buttons = doom_buttons;
    uint32_t changed = prev_buttons ^ buttons;
    event_t event = {0};

    if (changed) {
        struct { uint32_t mask; int *key; } keymap[] = {
            {DOOM_BTN_UP,     &key_up},
            {DOOM_BTN_DOWN,   &key_down},
            {DOOM_BTN_LEFT,   &key_left},
            {DOOM_BTN_RIGHT,  &key_right},
            {DOOM_BTN_FIRE,   &key_fire},
            {DOOM_BTN_FIRE,   &key_enter},
            {DOOM_BTN_USE,    &key_use},
            {DOOM_BTN_STRAFE, &key_speed},
            {DOOM_BTN_STRAFE, &key_strafe},
            {DOOM_BTN_STRAFE, &key_backspace},
            {DOOM_BTN_RUN,    &key_escape},
            {DOOM_BTN_WEAPON, &key_map},
        };
        for (int i = 0; i < (int)(sizeof(keymap)/sizeof(keymap[0])); i++) {
            if (changed & keymap[i].mask) {
                event.type = (buttons & keymap[i].mask) ? ev_keydown : ev_keyup;
                event.data1 = *keymap[i].key;
                D_PostEvent(&event);
            }
        }
    }
    prev_buttons = buttons;
}

void I_Init(void)
{
    snd_channels = 8;
    snd_samplerate = 11025;
    snd_MusicVolume = 0;  // Disable music to save CPU
    snd_SfxVolume = 8;
}

/* ================================================================ */
/*  DOOM Task — Runs D_DoomMain in a separate FreeRTOS task          */
/* ================================================================ */

static char doom_wad_path[256] = {0};

static void doom_task(void *arg)
{
    printf("[DOOM] Task started\n");

    doom_running = true;

    // Setup argv
    myargv = doom_argv;
    myargc = 3;
    doom_argv[0] = "doom";
    doom_argv[1] = "-iwad";
    doom_argv[2] = doom_wad_path;
    doom_argv[3] = NULL;

    // Prefer PSRAM for all allocations
    heap_caps_malloc_extmem_enable(0);

    Z_Init();
    D_DoomMain(); // Never returns (enters D_DoomLoop)

    printf("[DOOM] D_DoomMain returned unexpectedly\n");
    doom_running = false;
    vTaskDelete(NULL);
}

/* ================================================================ */
/*  Bridge API                                                       */
/* ================================================================ */

bool doom_bridge_init(void)
{
    // Allocate framebuffer in PSRAM (320x200)
    doom_framebuffer = (uint8_t *)heap_caps_calloc(
        DOOM_SCREEN_WIDTH * DOOM_SCREEN_HEIGHT, 1, MALLOC_CAP_SPIRAM);
    if (!doom_framebuffer) {
        printf("[DOOM] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // Allocate mobjinfo in PSRAM and copy from flash (saves ~11KB DRAM)
    extern const mobjinfo_t mobjinfo_rom[];
    extern mobjinfo_t *mobjinfo;
    mobjinfo = (mobjinfo_t *)heap_caps_malloc(
        NUMMOBJTYPES * sizeof(mobjinfo_t), MALLOC_CAP_SPIRAM);
    if (mobjinfo) {
        memcpy(mobjinfo, mobjinfo_rom, NUMMOBJTYPES * sizeof(mobjinfo_t));
    }

    // Set DOOM screen dimensions
    SCREENWIDTH = DOOM_SCREEN_WIDTH;
    SCREENHEIGHT = DOOM_SCREEN_HEIGHT;

    memset(doom_palette, 0, sizeof(doom_palette));

    printf("[DOOM] Bridge initialized\n");
    return true;
}

int doom_bridge_load_wad(const char *path)
{
    printf("[DOOM] Loading WAD: %s\n", path);
    strncpy(doom_wad_path, path, sizeof(doom_wad_path) - 1);

    // Launch DOOM in a separate task with large stack (prboom is stack-heavy)
    BaseType_t ret = xTaskCreatePinnedToCore(
        doom_task,
        "doom",
        32768,           // 32KB stack
        NULL,
        1,               // Priority
        &doom_task_handle,
        0                // Core 0
    );

    if (ret != pdPASS) {
        printf("[DOOM] ERROR: Failed to create task!\n");
        return -1;
    }

    // Wait for DOOM to initialize (max 5 seconds)
    for (int i = 0; i < 500 && !doom_running; i++) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (!doom_running) {
        printf("[DOOM] ERROR: Timeout waiting for DOOM init!\n");
        return -1;
    }

    return 0;
}

void doom_bridge_run_frame(void)
{
    // DOOM runs in its own task. We just check if a new frame is ready.
    if (doom_frame_ready) {
        doom_frame_ready = false;
    }
}

uint8_t *doom_bridge_get_framebuffer(int *width, int *height)
{
    if (width) *width = DOOM_SCREEN_WIDTH;
    if (height) *height = DOOM_SCREEN_HEIGHT;
    return doom_framebuffer;
}

uint16_t *doom_bridge_get_palette(void)
{
    return doom_palette;
}

void doom_bridge_set_input(uint32_t buttons)
{
    doom_buttons = buttons;
}

void doom_bridge_shutdown(void)
{
    if (doom_task_handle) {
        doom_running = false;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        vTaskDelete(doom_task_handle);
        doom_task_handle = NULL;
    }

    if (doom_framebuffer) {
        heap_caps_free(doom_framebuffer);
        doom_framebuffer = NULL;
    }

    printf("[DOOM] Shutdown complete\n");
}

bool doom_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool doom_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
