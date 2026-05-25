/*
 * Cover Art Loader Implementation
 * Loads .art (raw RGB565 LE) files from /romart/{emu}/{romname}.art
 * Converts RGB565 -> VGA SBGR2222 for display
 */
#include "cover_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_heap_caps.h>

// Convert RGB565 pixel to VGA SBGR2222
static inline uint8_t rgb565_to_sbgr2222(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F; // 5 bits
    uint8_t g = (c >> 5)  & 0x3F; // 6 bits
    uint8_t b = c & 0x1F;         // 5 bits
    // Map to 2-bit per channel
    uint8_t r2 = r >> 3; // 5->2
    uint8_t g2 = g >> 4; // 6->2
    uint8_t b2 = b >> 3; // 5->2
    // FabGL VGA8: S1 B1 G1 R1 S0 B0 G0 R0 (SBGR2222)
    return (b2 << 4) | (g2 << 2) | r2;
}

void cover_init(void) {
    // Nothing to init for now
}

bool cover_load(const char *rom_path, const char *emu_short, uint8_t *out_buf) {
    if (!rom_path || !emu_short || !out_buf) return false;

    // Extract ROM filename
    const char *fn = strrchr(rom_path, '/');
    fn = fn ? fn + 1 : rom_path;

    char path[260];

    // Strategy 1: filename-based .art (retro-go-covers format)
    // /romart/{emu}/{romname_without_ext}.art
    {
        char base[128];
        strncpy(base, fn, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';

        snprintf(path, sizeof(path), "/romart/%s/%s.art", emu_short, base);
        
        FILE *f = fopen(path, "rb");
        if (f) {
            // .art files are raw RGB565 LE pixel data
            // Read header: first 4 bytes = width(u16) + height(u16)
            uint16_t w = 0, h = 0;
            fread(&w, 2, 1, f);
            fread(&h, 2, 1, f);

            if (w > 0 && w <= 320 && h > 0 && h <= 240) {
                // Read and convert pixel data
                uint16_t *line = (uint16_t *)malloc(w * 2);
                if (line) {
                    memset(out_buf, 0, COVER_WIDTH * COVER_HEIGHT);
                    
                    int scaleX = (w << 8) / COVER_WIDTH;
                    int scaleY = (h << 8) / COVER_HEIGHT;

                    for (int y = 0; y < COVER_HEIGHT && y * scaleY / 256 < h; y++) {
                        int srcY = (y * scaleY) >> 8;
                        fseek(f, 4 + srcY * w * 2, SEEK_SET);
                        fread(line, 2, w, f);

                        for (int x = 0; x < COVER_WIDTH; x++) {
                            int srcX = (x * scaleX) >> 8;
                            if (srcX < w) {
                                out_buf[y * COVER_WIDTH + x] = rgb565_to_sbgr2222(line[srcX]);
                            }
                        }
                    }
                    free(line);
                    fclose(f);
                    printf("[Cover] Loaded: %s (%dx%d)\n", path, w, h);
                    return true;
                }
            }
            fclose(f);
        }
    }

    // Strategy 2: Try headerless raw art (some covers are just w*h*2 bytes)
    // Assume 160x120 if file size matches
    {
        snprintf(path, sizeof(path), "/romart/%s/%s.art", emu_short, fn);
        // Remove double extension: if fn already has .nes, try without
        char *dot = strrchr(path, '.');
        if (dot) {
            strcpy(dot, ".art");
        }

        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);

            // Common sizes: 160*120*2=38400, 86*120*2=20640
            int w = 0, h = 0;
            if (sz == 160 * 120 * 2) { w = 160; h = 120; }
            else if (sz == 86 * 120 * 2) { w = 86; h = 120; }
            else if (sz >= 4) {
                // Try reading as header format
                uint16_t tw, th;
                fread(&tw, 2, 1, f);
                fread(&th, 2, 1, f);
                if (tw > 0 && tw <= 320 && th > 0 && th <= 240 &&
                    (long)(4 + tw * th * 2) == sz) {
                    w = tw; h = th;
                }
            }

            if (w > 0 && h > 0) {
                uint16_t *line = (uint16_t *)malloc(w * 2);
                if (line) {
                    memset(out_buf, 0, COVER_WIDTH * COVER_HEIGHT);
                    int offset = (sz == (long)(4 + w * h * 2)) ? 4 : 0;
                    int scaleX = (w << 8) / COVER_WIDTH;
                    int scaleY = (h << 8) / COVER_HEIGHT;

                    for (int y = 0; y < COVER_HEIGHT; y++) {
                        int srcY = (y * scaleY) >> 8;
                        if (srcY >= h) break;
                        fseek(f, offset + srcY * w * 2, SEEK_SET);
                        fread(line, 2, w, f);
                        for (int x = 0; x < COVER_WIDTH; x++) {
                            int srcX = (x * scaleX) >> 8;
                            if (srcX < w)
                                out_buf[y * COVER_WIDTH + x] = rgb565_to_sbgr2222(line[srcX]);
                        }
                    }
                    free(line);
                    fclose(f);
                    printf("[Cover] Loaded raw: %s (%dx%d)\n", path, w, h);
                    return true;
                }
            }
            fclose(f);
        }
    }

    return false; // No cover found
}

void cover_free(void) {
    // Nothing to free
}
