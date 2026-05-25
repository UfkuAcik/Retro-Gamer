/*
 * Genesis Bridge — Sega Mega Drive / Genesis (gwenesis)
 * Globals required by gwenesis + bridge API
 */

#include "genesis_bridge.h"
#include "gwenesis/gwenesis.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

// Globals required by gwenesis core
unsigned char *VRAM = NULL;
int system_clock = 0;
int scan_line = 0;

#define GENESIS_AUDIO_BUF_LEN 888 // ~53267/60
int16_t gwenesis_sn76489_buffer[GENESIS_AUDIO_BUF_LEN];
int sn76489_index = 0;
int sn76489_clock = 0;
int16_t gwenesis_ym2612_buffer[GENESIS_AUDIO_BUF_LEN];
int ym2612_index = 0;
int ym2612_clock = 0;

static uint8_t *genesis_fb = NULL;
static uint16_t genesis_palette[256];
static uint32_t genesis_buttons = 0;
static unsigned int gen_screen_w = 320, gen_screen_h = 224;
extern int zclk;

// Savestate stubs (required by gwenesis)
SaveState* saveGwenesisStateOpenForRead(const char* f) { return (void*)1; }
SaveState* saveGwenesisStateOpenForWrite(const char* f) { return (void*)1; }
int saveGwenesisStateGet(SaveState* s, const char* tag) { return 0; }
void saveGwenesisStateSet(SaveState* s, const char* tag, int val) {}
void saveGwenesisStateGetBuffer(SaveState* s, const char* tag, void* buf, int len) {}
void saveGwenesisStateSetBuffer(SaveState* s, const char* tag, void* buf, int len) {}

// IO stub — gwenesis calls this to poll input
void gwenesis_io_get_buttons(void)
{
    // buttons already set via bridge_set_input → gwenesis_io_pad_press/release
}

bool genesis_bridge_init(int sample_rate)
{
    // 320 * 240 + some overdraw
    genesis_fb = (uint8_t *)heap_caps_calloc(320 * 241 + 320, 1, MALLOC_CAP_SPIRAM);
    if (!genesis_fb) {
        printf("[GEN] ERROR: Failed to allocate framebuffer!\n");
        return false;
    }

    // VRAM (64KB for VDP)
    #ifndef VRAM_MAX_SIZE
    #define VRAM_MAX_SIZE 0x10000
    #endif
    VRAM = (unsigned char *)heap_caps_calloc(VRAM_MAX_SIZE, 1, MALLOC_CAP_SPIRAM);
    if (!VRAM) {
        printf("[GEN] ERROR: Failed to allocate VRAM!\n");
        return false;
    }

    // M68K RAM (64KB) in PSRAM
    extern unsigned char *M68K_RAM;
    M68K_RAM = (unsigned char *)heap_caps_calloc(0x10000, 1, MALLOC_CAP_SPIRAM);
    if (!M68K_RAM) {
        printf("[GEN] ERROR: Failed to allocate M68K_RAM!\n");
        return false;
    }

    // Z80 RAM (8KB) in PSRAM
    extern unsigned char *ZRAM;
    ZRAM = (unsigned char *)heap_caps_calloc(8192, 1, MALLOC_CAP_SPIRAM);
    if (!ZRAM) {
        printf("[GEN] ERROR: Failed to allocate ZRAM!\n");
        return false;
    }

    printf("[GEN] Initialized. Sample rate: %d\n", sample_rate);
    return true;
}

int genesis_bridge_load_rom(const char *path)
{
    printf("[GEN] Loading ROM: %s\n", path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[GEN] Failed to open: %s\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Align to 64KB boundary
    size_t alloc_size = (rom_size + 0xFFFF) & ~0xFFFF;
    void *rom_data = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);
    if (!rom_data) {
        printf("[GEN] Failed to allocate ROM buffer! (%d bytes)\n", (int)rom_size);
        fclose(f);
        return -1;
    }
    memset(rom_data, 0xFF, alloc_size);
    fread(rom_data, 1, rom_size, f);
    fclose(f);

    load_cartridge(rom_data, rom_size);
    power_on();
    reset_emulation();

    printf("[GEN] ROM loaded. Size: %d bytes\n", (int)rom_size);
    return 0;
}

void genesis_bridge_run_frame(bool draw)
{
    extern unsigned char gwenesis_vdp_regs[0x20];
    extern unsigned int gwenesis_vdp_status;
    extern unsigned short CRAM565[256];
    extern unsigned int screen_width, screen_height;
    extern int hint_pending;

    int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
    int hint_counter = gwenesis_vdp_regs[10];

    screen_width = REG12_MODE_H40 ? 320 : 256;
    screen_height = REG1_PAL ? 240 : 224;
    gen_screen_w = screen_width;
    gen_screen_h = screen_height;

    gwenesis_vdp_set_buffer(genesis_fb);
    gwenesis_vdp_render_config();

    system_clock = 0;
    zclk = 0;
    ym2612_clock = 0; ym2612_index = 0;
    sn76489_clock = 0; sn76489_index = 0;
    scan_line = 0;

    while (scan_line < lines_per_frame)
    {
        m68k_run(system_clock + VDP_CYCLES_PER_LINE);
        z80_run(system_clock + VDP_CYCLES_PER_LINE);

        if (GWENESIS_AUDIO_ACCURATE == 0) {
            gwenesis_SN76489_run(system_clock + VDP_CYCLES_PER_LINE);
            ym2612_run(system_clock + VDP_CYCLES_PER_LINE);
        }

        if (draw && scan_line < (int)screen_height)
            gwenesis_vdp_render_line(scan_line);

        if ((scan_line == 0) || (scan_line > (int)screen_height))
            hint_counter = REG10_LINE_COUNTER;

        if (--hint_counter < 0) {
            if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= (int)screen_height)) {
                hint_pending = 1;
                if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0)
                    m68k_update_irq(4);
            }
            hint_counter = REG10_LINE_COUNTER;
        }

        scan_line++;

        if (scan_line == (int)screen_height) {
            if (REG1_VBLANK_INTERRUPT != 0) {
                gwenesis_vdp_status |= STATUS_VIRQPENDING;
                m68k_set_irq(6);
            }
            z80_irq_line(1);
        }
        if (scan_line == (int)screen_height + 1) {
            z80_irq_line(0);
        }

        system_clock += VDP_CYCLES_PER_LINE;
    }

    if (GWENESIS_AUDIO_ACCURATE == 1) {
        gwenesis_SN76489_run(system_clock);
        ym2612_run(system_clock);
    }

    m68k.cycles -= system_clock;

    // Build palette
    if (draw) {
        for (int i = 0; i < 256; i++)
            genesis_palette[i] = (CRAM565[i] << 8) | (CRAM565[i] >> 8);
    }
}

uint8_t* genesis_bridge_get_framebuffer(int *width, int *height)
{
    *width = gen_screen_w;
    *height = gen_screen_h;
    return genesis_fb;
}

uint16_t* genesis_bridge_get_palette(void)
{
    return genesis_palette;
}

int16_t* genesis_bridge_get_audio(int *num_samples)
{
    *num_samples = ym2612_index;
    return gwenesis_ym2612_buffer;
}

void genesis_bridge_set_input(uint32_t buttons)
{
    // gwenesis uses button indices 0-7: UP DOWN LEFT RIGHT A B C START
    static uint32_t prev = 0;
    if (buttons == prev) return;

    uint32_t masks[] = {GEN_BTN_UP, GEN_BTN_DOWN, GEN_BTN_LEFT, GEN_BTN_RIGHT,
                        GEN_BTN_A, GEN_BTN_B, GEN_BTN_C, GEN_BTN_START};
    for (int i = 0; i < 8; i++) {
        if (buttons & masks[i])
            gwenesis_io_pad_press_button(0, i);
        else
            gwenesis_io_pad_release_button(0, i);
    }
    prev = buttons;
}

void genesis_bridge_shutdown(void)
{
    if (genesis_fb) { heap_caps_free(genesis_fb); genesis_fb = NULL; }
    if (VRAM) { heap_caps_free(VRAM); VRAM = NULL; }
}

bool genesis_bridge_save_state(const char *path) {
    // TODO: Implement save state
    (void)path;
    return false;
}

bool genesis_bridge_load_state(const char *path) {
    // TODO: Implement load state
    (void)path;
    return false;
}
