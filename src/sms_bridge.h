/*
 * SMS/GG Bridge Header
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define SMS_SCREEN_WIDTH   256
#define SMS_SCREEN_HEIGHT  192
#define GG_SCREEN_WIDTH    160
#define GG_SCREEN_HEIGHT   144
#define SMS_AUDIO_SAMPLE_RATE 22050

bool     sms_bridge_init(int sample_rate);
int      sms_bridge_load_rom(const char *path);
void     sms_bridge_run_frame(bool draw);
uint8_t* sms_bridge_get_framebuffer(int *width, int *height);
uint16_t* sms_bridge_get_palette(void);
void     sms_bridge_get_audio(int16_t **left, int16_t **right, int *num_samples);
void     sms_bridge_set_input(uint32_t buttons);
void     sms_bridge_shutdown(void);
bool sms_bridge_save_state(const char *path);
bool sms_bridge_load_state(const char *path);
bool     sms_bridge_is_gamegear(void);

// Button masks
#define SMS_BTN_UP      0x01
#define SMS_BTN_DOWN    0x02
#define SMS_BTN_LEFT    0x04
#define SMS_BTN_RIGHT   0x08
#define SMS_BTN_A       0x10
#define SMS_BTN_B       0x20
#define SMS_BTN_START   0x40

#ifdef __cplusplus
}
#endif
