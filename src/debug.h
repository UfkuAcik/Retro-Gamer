/*
 * ============================================================================
 *  RETRO-GAMER — Debug System
 * ============================================================================
 *  Compile-time togglable debug output for both Serial and VGA overlay.
 *
 *  Usage:
 *    #define DEBUG_LEVEL 2   // 0=off, 1=errors, 2=+warnings, 3=+info, 4=+verbose
 *    #include "src/debug.h"
 *
 *    DBG_ERROR("Init", "SD card mount failed: %d", err);
 *    DBG_WARN("Audio", "Buffer underrun at frame %d", frame);
 *    DBG_INFO("NES", "ROM loaded: %s (%d KB)", name, size/1024);
 *    DBG_VERBOSE("Input", "Keys: 0x%04X", buttons);
 *
 *  VGA Overlay:
 *    DBG_OSD("FPS:%d MEM:%dKB", fps, freeHeap/1024);
 *    dbg_osd_show() / dbg_osd_hide()  — toggle on-screen display
 *
 *  Memory Stats:
 *    dbg_print_memory()  — dumps DRAM/PSRAM usage to Serial
 * ============================================================================
 */

#ifndef RETRO_GAMER_DEBUG_H
#define RETRO_GAMER_DEBUG_H

#include <stdio.h>
#include <stdarg.h>
#include <esp_heap_caps.h>

// ============================================================================
//  DEBUG_LEVEL: Set at compile time via build flags or here
//    0 = OFF  (no debug output, zero overhead)
//    1 = ERR  (errors only)
//    2 = WARN (errors + warnings)
//    3 = INFO (errors + warnings + info)
//    4 = VERB (everything)
// ============================================================================
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3  // Default: show errors, warnings, info
#endif

// ============================================================================
//  DEBUG_VGA_OSD: Enable on-screen debug overlay
//    0 = OFF (no VGA overlay)
//    1 = ON  (shows debug text on VGA screen)
// ============================================================================
#ifndef DEBUG_VGA_OSD
#define DEBUG_VGA_OSD 1
#endif

// ============================================================================
//  Serial Debug Macros
// ============================================================================
#if DEBUG_LEVEL >= 1
  #define DBG_ERROR(tag, fmt, ...) \
    printf("\033[31m[ERR][%s] " fmt "\033[0m\n", tag, ##__VA_ARGS__)
#else
  #define DBG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 2
  #define DBG_WARN(tag, fmt, ...) \
    printf("\033[33m[WRN][%s] " fmt "\033[0m\n", tag, ##__VA_ARGS__)
#else
  #define DBG_WARN(tag, fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 3
  #define DBG_INFO(tag, fmt, ...) \
    printf("[INF][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
  #define DBG_INFO(tag, fmt, ...) ((void)0)
#endif

#if DEBUG_LEVEL >= 4
  #define DBG_VERBOSE(tag, fmt, ...) \
    printf("\033[90m[VRB][%s] " fmt "\033[0m\n", tag, ##__VA_ARGS__)
#else
  #define DBG_VERBOSE(tag, fmt, ...) ((void)0)
#endif

// ============================================================================
//  Memory Debug Helpers
// ============================================================================
static inline void dbg_print_memory(void) {
#if DEBUG_LEVEL >= 1
    printf("\n===== MEMORY REPORT =====\n");
    printf("DRAM  free: %6d bytes (min: %d)\n",
        (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (int)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    printf("PSRAM free: %6d bytes (min: %d)\n",
        (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (int)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    printf("Total free: %6d bytes\n",
        (int)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    printf("Largest DRAM block:  %d\n",
        (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    printf("Largest PSRAM block: %d\n",
        (int)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    printf("=========================\n\n");
#endif
}

// ============================================================================
//  VGA On-Screen Display (OSD)
//  Uses the Canvas pointer passed from main .ino
// ============================================================================
#if DEBUG_VGA_OSD

// OSD state — managed from main code
static bool _dbg_osd_visible = false;

// Max 4 lines of OSD text, 40 chars each
#define DBG_OSD_LINES 4
#define DBG_OSD_CHARS 40
static char _dbg_osd_buf[DBG_OSD_LINES][DBG_OSD_CHARS + 1];
static int  _dbg_osd_line_count = 0;

static inline void dbg_osd_show(void) { _dbg_osd_visible = true; }
static inline void dbg_osd_hide(void) { _dbg_osd_visible = false; }
static inline void dbg_osd_toggle(void) { _dbg_osd_visible = !_dbg_osd_visible; }
static inline bool dbg_osd_is_visible(void) { return _dbg_osd_visible; }

// Set a specific OSD line (0-3)
static inline void dbg_osd_set_line(int line, const char *fmt, ...) {
    if (line < 0 || line >= DBG_OSD_LINES) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(_dbg_osd_buf[line], DBG_OSD_CHARS + 1, fmt, args);
    va_end(args);
    if (line >= _dbg_osd_line_count) _dbg_osd_line_count = line + 1;
}

// Clear all OSD lines
static inline void dbg_osd_clear(void) {
    for (int i = 0; i < DBG_OSD_LINES; i++) _dbg_osd_buf[i][0] = '\0';
    _dbg_osd_line_count = 0;
}

// Draw OSD overlay — call from main render loop with Canvas*
// Uses top-left corner with semi-transparent black background
static inline void dbg_osd_draw(fabgl::Canvas *cv) {
    if (!_dbg_osd_visible || !cv || _dbg_osd_line_count == 0) return;

    // Draw background box
    int boxH = _dbg_osd_line_count * 10 + 4;
    cv->setBrushColor(Color::Black);
    cv->fillRectangle(0, 0, 200, boxH);

    // Draw text lines
    cv->setPenColor(Color::BrightGreen);
    for (int i = 0; i < _dbg_osd_line_count; i++) {
        if (_dbg_osd_buf[i][0] != '\0') {
            cv->drawText(2, 2 + i * 10, _dbg_osd_buf[i]);
        }
    }
}

// Convenience: Update common stats (call every frame or every N frames)
static inline void dbg_osd_update_stats(int fps, const char *emuName) {
    int dramFree = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    int psramFree = (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    dbg_osd_set_line(0, "FPS:%-3d EMU:%s", fps, emuName ? emuName : "---");
    dbg_osd_set_line(1, "DRAM:%dKB PSRAM:%dKB", dramFree, psramFree);
}

#else  // DEBUG_VGA_OSD == 0

// No-op stubs when OSD is disabled
#define dbg_osd_show()           ((void)0)
#define dbg_osd_hide()           ((void)0)
#define dbg_osd_toggle()         ((void)0)
#define dbg_osd_is_visible()     (false)
#define dbg_osd_set_line(...)    ((void)0)
#define dbg_osd_clear()          ((void)0)
#define dbg_osd_draw(cv)         ((void)0)
#define dbg_osd_update_stats(f,n) ((void)0)
static inline void dbg_print_memory(void) {}

#endif // DEBUG_VGA_OSD

#endif // RETRO_GAMER_DEBUG_H
