/*
 * GW Bridge Header — Game & Watch LCD emulator
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>

#define GW_SCREEN_W  320
#define GW_SCREEN_H  240

bool     gw_bridge_init(void);
int      gw_bridge_load_rom(const char *path);
void     gw_bridge_run_frame(void);
uint16_t* gw_bridge_get_framebuffer(void);
void     gw_bridge_set_input(uint32_t buttons);
void     gw_bridge_shutdown(void);
bool gw_bridge_save_state(const char *path);
bool gw_bridge_load_state(const char *path);

// Button masks
#define GW_BTN_LEFT   0x01
#define GW_BTN_UP     0x02
#define GW_BTN_RIGHT  0x04
#define GW_BTN_DOWN   0x08
#define GW_BTN_A      0x10
#define GW_BTN_B      0x20
#define GW_BTN_TIME   0x40
#define GW_BTN_GAME   0x80

#ifdef __cplusplus
}
#endif
