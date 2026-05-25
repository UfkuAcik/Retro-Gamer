/*
 * Lynx Bridge — C++ implementation wrapping Handy CSystem
 */
#include "lynx_bridge.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "handy/handy.h"

#include <esp_heap_caps.h>

// Mikie pixel format enum (from mikie.h)
#ifndef MIKIE_PIXEL_FORMAT_16BPP_565_BE
#define MIKIE_PIXEL_FORMAT_16BPP_565_BE 2
#endif
#ifndef MIKIE_NO_ROTATE
#define MIKIE_NO_ROTATE 0
#endif

static CSystem *lynxSystem = nullptr;

extern "C" {

bool lynx_bridge_init(int sample_rate)
{
    // Allocate framebuffer in PSRAM (160*160*2 = 51200 bytes, square for rotation)
    gPrimaryFrameBuffer = (UBYTE *)heap_caps_calloc(LYNX_SCREEN_WIDTH * LYNX_SCREEN_WIDTH, 2, MALLOC_CAP_SPIRAM);
    if (!gPrimaryFrameBuffer) {
        printf("[Lynx] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // Allocate audio buffer in PSRAM
    gAudioBuffer = (SWORD *)heap_caps_calloc(HANDY_AUDIO_BUFFER_LENGTH * 2, sizeof(SWORD), MALLOC_CAP_SPIRAM);
    if (!gAudioBuffer) {
        printf("[Lynx] ERROR: Failed to allocate audio buffer!\n");
        return false;
    }
    gAudioEnabled = 1;
    gAudioBufferPointer = 0;

    printf("[Lynx] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int lynx_bridge_load_rom(const char *path)
{
    printf("[Lynx] Loading ROM: %s\n", path);

    if (lynxSystem) { delete lynxSystem; lynxSystem = nullptr; }

    lynxSystem = new CSystem(path, MIKIE_PIXEL_FORMAT_16BPP_565_BE, LYNX_AUDIO_SAMPLE_RATE);

    if (!lynxSystem || lynxSystem->mFileType == HANDY_FILETYPE_ILLEGAL) {
        printf("[Lynx] ROM load failed!\n");
        if (lynxSystem) { delete lynxSystem; lynxSystem = nullptr; }
        return -1;
    }

    // No rotation
    lynxSystem->mMikie->SetRotation(MIKIE_NO_ROTATE);

    printf("[Lynx] ROM loaded. Screen: %dx%d\n", LYNX_SCREEN_WIDTH, LYNX_SCREEN_HEIGHT);
    return 0;
}

void lynx_bridge_run_frame(bool draw)
{
    if (!lynxSystem) return;
    gAudioBufferPointer = 0;
    lynxSystem->UpdateFrame(draw);
}

uint16_t* lynx_bridge_get_framebuffer(void)
{
    return (uint16_t *)gPrimaryFrameBuffer;
}

int16_t* lynx_bridge_get_audio(int *num_samples)
{
    *num_samples = gAudioBufferPointer / 2; // stereo pairs
    return (int16_t *)gAudioBuffer;
}

void lynx_bridge_set_input(uint32_t buttons)
{
    if (!lynxSystem) return;
    ULONG lynxBtns = 0;
    if (buttons & LYNX_BTN_UP)    lynxBtns |= BUTTON_UP;
    if (buttons & LYNX_BTN_DOWN)  lynxBtns |= BUTTON_DOWN;
    if (buttons & LYNX_BTN_LEFT)  lynxBtns |= BUTTON_LEFT;
    if (buttons & LYNX_BTN_RIGHT) lynxBtns |= BUTTON_RIGHT;
    if (buttons & LYNX_BTN_A)     lynxBtns |= BUTTON_A;
    if (buttons & LYNX_BTN_B)     lynxBtns |= BUTTON_B;
    if (buttons & LYNX_BTN_OPT1)  lynxBtns |= BUTTON_OPT1;
    if (buttons & LYNX_BTN_OPT2)  lynxBtns |= BUTTON_OPT2;
    lynxSystem->SetButtonData(lynxBtns);
}

void lynx_bridge_shutdown(void)
{
    if (lynxSystem) { delete lynxSystem; lynxSystem = nullptr; }
    if (gPrimaryFrameBuffer) { heap_caps_free(gPrimaryFrameBuffer); gPrimaryFrameBuffer = NULL; }
    if (gAudioBuffer) { heap_caps_free(gAudioBuffer); gAudioBuffer = NULL; }
    gAudioEnabled = 0;
}

bool lynx_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool lynx_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}

} // extern "C"
