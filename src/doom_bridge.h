/*
 * DOOM Bridge Header — prboom port for ESP32 FabGL
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>

#define DOOM_SCREEN_WIDTH  320
#define DOOM_SCREEN_HEIGHT 200

bool      doom_bridge_init(void);
int       doom_bridge_load_wad(const char *path);
void      doom_bridge_run_frame(void);
uint8_t*  doom_bridge_get_framebuffer(int *width, int *height);
uint16_t* doom_bridge_get_palette(void);
void      doom_bridge_set_input(uint32_t buttons);
void      doom_bridge_shutdown(void);
bool doom_bridge_save_state(const char *path);
bool doom_bridge_load_state(const char *path);

#define DOOM_BTN_UP      0x01
#define DOOM_BTN_DOWN    0x02
#define DOOM_BTN_LEFT    0x04
#define DOOM_BTN_RIGHT   0x08
#define DOOM_BTN_FIRE    0x10
#define DOOM_BTN_USE     0x20
#define DOOM_BTN_STRAFE  0x40
#define DOOM_BTN_RUN     0x80
#define DOOM_BTN_WEAPON  0x100

#ifdef __cplusplus
}
#endif
