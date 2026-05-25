/*
 * rg_system.h — Stub for prboom compatibility
 * Replaces retro-go's rg_system with minimal stubs for Arduino/FabGL
 */
#ifndef RG_SYSTEM_H
#define RG_SYSTEM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp_timer.h>

// Logging
#define RG_LOG_PRINTF 0
#define RG_PANIC(msg) do { printf("PANIC: %s\n", msg); while(1) vTaskDelay(1000); } while(0)

static inline void rg_system_vlog(int level, const char *tag, const char *fmt, va_list args) {
    vprintf(fmt, args);
}

// Timer
static inline int64_t rg_system_timer(void) {
    return esp_timer_get_time();
}

static inline void rg_usleep(unsigned long us) {
    vTaskDelay(us / 1000 / portTICK_PERIOD_MS + 1);
}

// File extension match (case insensitive)
static inline bool rg_extension_match(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    return strcasecmp(dot + 1, ext) == 0;
}

// Storage stubs — no ZIP support, WAD files are loaded directly
static inline bool rg_storage_unzip_file(const char *path, void *unused, void **data, size_t *size, int flags) {
    (void)unused; (void)flags;
    // Not supported - return false
    return false;
}

// Task
typedef void (*rg_task_func_t)(void *arg);
#define RG_TASK_PRIORITY_2 2

static inline void rg_task_create(const char *name, rg_task_func_t func, void *arg, int stack, int prio, int core) {
    // Create a FreeRTOS task
    xTaskCreatePinnedToCore(func, name, stack + 2048, arg, prio, NULL, core);
}

// Misc
#define RG_COUNT(a) (sizeof(a) / sizeof(a[0]))
#define RG_MIN(a,b) ((a) < (b) ? (a) : (b))
#define RG_MAX(a,b) ((a) > (b) ? (a) : (b))

// Base paths — SD card
#define RG_BASE_PATH_ROMS   "/sd"
#define RG_BASE_PATH_SAVES  "/sd"

// Display info
static inline int rg_display_get_width(void) { return 320; }
static inline int rg_display_get_height(void) { return 200; }

// Unused stubs
#define NS_APP "doom"

static inline int rg_settings_get_number(const char *ns, const char *key, int def) { return def; }
static inline void rg_settings_set_number(const char *ns, const char *key, int val) {}

#endif // RG_SYSTEM_H
