/*
 * ============================================================================
 *  RETRO-GAMER — Multi-System Retro Emulator
 *  NES · SNES · GB · GBC · SMS · GG · PCE · Lynx · Game&Watch
 * ============================================================================
 *  Platform : Olimex ESP32-SBC-FabGL Rev B (ESP32-WROVER)
 *  Display  : VGA 320x240 R2G2B2 via FabGL VGA8Controller
 *  Input    : PS/2 Keyboard via FabGL
 *  Storage  : MicroSD Card (SPI mode)
 *  Audio    : Internal DAC (GPIO 25) via FabGL SoundGenerator
 * ============================================================================
 */

#include <fabgl.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>

// Debug system — set DEBUG_LEVEL before include:
//   0=OFF, 1=ERR, 2=WARN, 3=INFO, 4=VERBOSE
#define DEBUG_LEVEL 3
#define DEBUG_VGA_OSD 1
#include "src/debug.h"

// Emulator bridges (C linkage)
extern "C" {
#include "src/nes_bridge.h"
#include "src/gb_bridge.h"
#include "src/sms_bridge.h"
#include "src/pce_bridge.h"
#include "src/snes_bridge.h"
#include "src/lynx_bridge.h"
#include "src/gw_bridge.h"
#include "src/genesis_bridge.h"
#include "src/doom_bridge.h"
#include "src/zip_loader.h"
#include "src/cover_art.h"
#include "src/msx_bridge.h"
}

// WiFi support — compile-time optional
// WiFi+LWIP SDK adds ~31KB static DRAM (cannot use PSRAM in Arduino ESP32)
// To enable: set to 1 AND rename src/wifi_manager.cpp.disabled -> .cpp
#define ENABLE_WIFI 0

#if ENABLE_WIFI
#include "src/wifi_manager.h"
#else
// Macros instead of inline functions to avoid Arduino IDE prototype issues
#define wifi_manager_init()           (false)
#define wifi_manager_connect(n)       (false)
#define wifi_manager_disconnect()     ((void)0)
#define wifi_manager_is_connected()   (false)
#define wifi_manager_get_ip()         "0.0.0.0"
#define wifi_manager_get_ssid(i)      ""
#define wifi_manager_get_network_count() (0)
#define wifi_manager_start_server()   (false)
#define wifi_manager_stop_server()    ((void)0)
#define wifi_manager_process()        ((void)0)
#define wifi_manager_ntp_sync(tz)     (false)
#endif

// ============================================================================
//  HARDWARE PINS — Olimex ESP32-SBC-FabGL Rev B
// ============================================================================
#define VGA_RED0    GPIO_NUM_21
#define VGA_RED1    GPIO_NUM_22
#define VGA_GREEN0  GPIO_NUM_18
#define VGA_GREEN1  GPIO_NUM_19
#define VGA_BLUE0   GPIO_NUM_4
#define VGA_BLUE1   GPIO_NUM_5
#define VGA_HSYNC   GPIO_NUM_23
#define VGA_VSYNC   GPIO_NUM_15
#define SD_CS       13
#define SD_MISO     35
#define SD_MOSI     12
#define SD_CLK      14
#define KBD_CLK     GPIO_NUM_33
#define KBD_DAT     GPIO_NUM_32
#define MOUSE_CLK   GPIO_NUM_26
#define MOUSE_DAT   GPIO_NUM_27
#define AUDIO_DAC   GPIO_NUM_25

// ============================================================================
//  DISPLAY
// ============================================================================
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240

// ============================================================================
//  EMULATOR TYPES
// ============================================================================
typedef enum {
    EMU_NONE = 0,
    EMU_NES,       // 256x240
    EMU_GB,        // 160x144
    EMU_SMS,       // 256x192
    EMU_GG,        // 160x144
    EMU_SG1000,    // 256x192 (TMS9918)
    EMU_COLECO,    // 256x192 (TMS9918)
    EMU_PCE,       // 256x224 (up to 352x242)
    EMU_SNES,      // 256x224
    EMU_GENESIS,   // 320x224 (Mega Drive)
    EMU_LYNX,      // 160x102
    EMU_GW,        // 320x240 (Game & Watch)
    EMU_DOOM,      // 320x200 (experimental)
    EMU_MSX,       // 256x192 (MSX)
} emu_type_t;

static emu_type_t currentEmu = EMU_NONE;

// ============================================================================
//  GLOBAL HARDWARE OBJECTS
// ============================================================================
fabgl::VGA8Controller   DisplayController;
fabgl::PS2Controller    PS2Ctrl;
fabgl::Keyboard *       Kbd = nullptr;

// ============================================================================
//  AUDIO SYSTEM — Ring buffer based DAC output
// ============================================================================
#define AUDIO_RING_SIZE 4096
static int16_t *audioRing = NULL; // Allocated in PSRAM at init
static volatile int audioWritePos = 0;
static volatile int audioReadPos = 0;

// Custom WaveformGenerator that reads from the ring buffer
class EmuAudioGenerator : public fabgl::WaveformGenerator {
public:
    EmuAudioGenerator() {}
    void setFrequency(int value) override { }

    int getSample() override {
        if (audioReadPos == audioWritePos) return 0;
        int16_t s = audioRing[audioReadPos];
        audioReadPos = (audioReadPos + 1) % AUDIO_RING_SIZE;
        // Convert 16-bit signed to 8-bit signed for DAC
        return s >> 8;
    }
};

static fabgl::SoundGenerator  SoundGen(22050, AUDIO_DAC, fabgl::SoundGenMethod::DAC);
static EmuAudioGenerator *    emuAudioGen = nullptr;

void audioFeedSamples(int16_t *samples, int count) {
    for (int i = 0; i < count && ((audioWritePos + 1) % AUDIO_RING_SIZE) != audioReadPos; i++) {
        audioRing[audioWritePos] = samples[i];
        audioWritePos = (audioWritePos + 1) % AUDIO_RING_SIZE;
    }
}

// Feed stereo (L/R interleaved or separate channels)
void audioFeedStereoMixed(int16_t *left, int16_t *right, int count) {
    for (int i = 0; i < count && ((audioWritePos + 1) % AUDIO_RING_SIZE) != audioReadPos; i++) {
        int32_t mixed = ((int32_t)left[i] + (int32_t)right[i]) / 2;
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        audioRing[audioWritePos] = (int16_t)mixed;
        audioWritePos = (audioWritePos + 1) % AUDIO_RING_SIZE;
    }
}

void initAudio() {
    // Allocate audio ring buffer in PSRAM (saves 8KB DRAM)
    if (!audioRing) {
        audioRing = (int16_t *)heap_caps_calloc(AUDIO_RING_SIZE, sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!audioRing) {
            DBG_ERROR("Audio", "PSRAM alloc failed for audioRing!");
            return;
        }
    }
    emuAudioGen = new EmuAudioGenerator();
    emuAudioGen->setSampleRate(22050);
    emuAudioGen->setVolume(126);
    emuAudioGen->enable(true);
    SoundGen.attach(emuAudioGen);
    SoundGen.play(true);
    DBG_INFO("Audio", "DAC initialized at 22050 Hz on GPIO %d", (int)AUDIO_DAC);
}

// ============================================================================
//  NES PALETTE → VGA SBGR2222 LOOKUP TABLE
// ============================================================================
#define NES_PALETTE_COUNT 5
static int nesCurrentPalette = 0;

// NES palettes (5 options) — each is 64 colors × {R,G,B}
static const uint8_t nes_palettes[NES_PALETTE_COUNT][64][3] = {
    // 0: Default (2C02)
    {
        {101,101,101},{  0, 45,105},{  1, 18,131},{ 37,  7,126},
        { 73,  0, 92},{ 86,  0, 40},{ 78,  2,  0},{ 49, 19,  0},
        { 17, 40,  0},{  0, 55,  0},{  0, 57,  0},{  0, 49, 21},
        {  0, 36, 76},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {174,174,174},{ 14, 97,173},{ 47, 64,216},{ 98, 45,210},
        {148, 34,169},{166, 35,100},{157, 46, 19},{121, 72,  0},
        { 78,100,  0},{ 36,121,  0},{  6,128, 15},{  0,122, 80},
        {  0,104,152},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{ 83,179,253},{116,145,254},{170,121,253},
        {222,109,219},{242,109,145},{234,122, 60},{200,150,  6},
        {155,180,  0},{108,201, 17},{ 76,210, 82},{ 64,205,151},
        { 66,186,226},{ 78, 78, 78},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{178,223,254},{193,208,254},{215,199,254},
        {236,194,240},{244,193,211},{240,199,180},{227,209,161},
        {209,220,154},{190,228,163},{179,232,189},{174,230,218},
        {175,222,241},{180,180,180},{  0,  0,  0},{  0,  0,  0},
    },
    // 1: Smooth (FBX)
    {
        {106,107,105},{  0, 42, 97},{  2, 16,128},{ 38,  5,122},
        { 75,  0, 88},{ 86,  0, 36},{ 79,  4,  0},{ 51, 21,  0},
        { 18, 41,  0},{  0, 55,  0},{  0, 57,  0},{  0, 48, 17},
        {  0, 36, 72},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {180,180,180},{ 14, 94,169},{ 49, 62,214},{100, 42,208},
        {152, 32,167},{168, 32, 96},{160, 44, 16},{124, 70,  0},
        { 82, 98,  0},{ 38,118,  0},{  8,126, 12},{  0,120, 76},
        {  0,102,148},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{ 86,180,254},{118,146,254},{174,122,254},
        {226,110,220},{244,110,146},{236,124, 62},{204,152,  8},
        {158,182,  0},{110,202, 18},{ 78,212, 84},{ 66,206,152},
        { 68,188,228},{ 80, 80, 80},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{180,224,254},{194,210,254},{216,200,254},
        {238,196,240},{246,194,212},{242,200,182},{228,210,162},
        {210,222,156},{192,230,164},{180,234,190},{176,232,220},
        {176,224,242},{182,182,182},{  0,  0,  0},{  0,  0,  0},
    },
    // 2: Nesticle
    {
        {112,112,112},{  0,  0,128},{ 16,  0,176},{ 48,  0,176},
        { 80,  0,144},{100,  0, 64},{ 96,  0,  0},{ 72, 16,  0},
        { 48, 32,  0},{ 16, 48,  0},{  0, 56,  0},{  0, 48,  0},
        {  0, 32, 64},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {176,176,176},{  0, 80,208},{ 32, 48,240},{ 96, 32,240},
        {160, 16,176},{192,  0, 96},{176,  0,  0},{144, 32,  0},
        {112, 64,  0},{ 48, 96,  0},{  0,112,  0},{  0,112,  0},
        {  0, 80,112},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {240,240,240},{ 32,160,255},{ 80,128,255},{160, 96,255},
        {224, 80,224},{240, 80,160},{240, 96, 64},{224,144,  0},
        {176,176,  0},{112,208,  0},{ 48,224, 48},{  0,224,128},
        {  0,192,224},{ 64, 64, 64},{  0,  0,  0},{  0,  0,  0},
        {240,240,240},{160,208,255},{176,192,255},{208,176,255},
        {240,176,240},{240,176,208},{240,192,160},{224,208,128},
        {208,224,128},{176,224,160},{160,224,192},{160,224,224},
        {160,208,240},{160,160,160},{  0,  0,  0},{  0,  0,  0},
    },
    // 3: Wavebeam
    {
        {108,108,108},{  0, 40, 96},{  0, 12,128},{ 40,  0,116},
        { 72,  0, 84},{ 84,  0, 32},{ 76,  0,  0},{ 44, 20,  0},
        { 16, 40,  0},{  0, 52,  0},{  0, 56,  0},{  0, 48, 16},
        {  0, 32, 68},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {172,172,172},{ 12, 88,168},{ 44, 56,212},{ 92, 40,204},
        {140, 28,164},{160, 28, 92},{152, 40, 12},{116, 64,  0},
        { 76, 92,  0},{ 32,112,  0},{  4,120,  8},{  0,116, 72},
        {  0, 96,144},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {248,248,248},{ 76,172,248},{112,140,248},{164,116,248},
        {216,104,216},{236,104,140},{228,116, 56},{196,144,  0},
        {148,172,  0},{104,196, 12},{ 72,204, 76},{ 60,200,148},
        { 60,180,220},{ 72, 72, 72},{  0,  0,  0},{  0,  0,  0},
        {248,248,248},{172,220,248},{188,204,248},{212,196,248},
        {232,192,236},{240,188,208},{236,196,176},{224,204,156},
        {204,216,148},{186,224,156},{174,228,184},{170,226,212},
        {170,218,236},{176,176,176},{  0,  0,  0},{  0,  0,  0},
    },
    // 4: Classic (RP2C02, saturated)
    {
        {102,102,102},{  0, 42,136},{  0, 20,160},{ 44,  2,152},
        { 80,  0,118},{ 90,  0, 54},{ 82,  0,  0},{ 52, 14,  0},
        { 20, 36,  0},{  0, 50,  0},{  0, 52,  0},{  0, 44, 16},
        {  0, 32, 80},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {170,170,170},{  8, 92,176},{ 42, 58,222},{ 96, 38,214},
        {146, 28,172},{164, 28, 98},{156, 40, 14},{120, 66,  0},
        { 76, 96,  0},{ 34,116,  0},{  4,124, 10},{  0,118, 76},
        {  0,100,148},{  0,  0,  0},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{ 80,176,254},{114,142,254},{168,118,254},
        {220,106,220},{240,106,142},{232,118, 58},{198,148,  4},
        {154,178,  0},{106,198, 14},{ 74,208, 80},{ 62,202,148},
        { 64,184,224},{ 76, 76, 76},{  0,  0,  0},{  0,  0,  0},
        {254,254,254},{176,222,254},{192,206,254},{214,198,254},
        {236,192,240},{244,192,210},{240,198,178},{226,208,160},
        {208,218,152},{188,226,162},{178,230,188},{172,228,216},
        {174,220,240},{178,178,178},{  0,  0,  0},{  0,  0,  0},
    },
};

static const char *nes_palette_names[NES_PALETTE_COUNT] = {
    "Default", "Smooth", "Nesticle", "Wavebeam", "Classic"
};

static uint8_t nesLUT[256]; // NES palette index → SBGR2222

void buildNESLUT() {
    const uint8_t (*pal)[3] = nes_palettes[nesCurrentPalette];
    for (int i = 0; i < 64; i++) {
        uint8_t r2 = pal[i][0] >> 6;
        uint8_t g2 = pal[i][1] >> 6;
        uint8_t b2 = pal[i][2] >> 6;
        uint8_t c = r2 | (g2 << 2) | (b2 << 4);
        nesLUT[i] = nesLUT[i+64] = nesLUT[i+128] = nesLUT[i+192] = c;
    }
}

// RGB565 → SBGR2222
static inline uint8_t rgb565toVGA(uint16_t c) {
    return ((c >> 14) & 0x03)        // R: bits 15-14 → bits 0-1
         | (((c >> 9) & 0x03) << 2)  // G: bits 10-9  → bits 2-3
         | (((c >> 3) & 0x03) << 4); // B: bits 4-3   → bits 4-5
}

// ============================================================================
//  APPLICATION STATE
// ============================================================================
typedef enum {
    STATE_BOOT,
    STATE_FILE_SELECT,
    STATE_EMULATING,
    STATE_INGAME_MENU,
    STATE_ERROR,
} app_state_t;

static app_state_t appState = STATE_BOOT;

#define MAX_ROMS 100
static char *romFiles[MAX_ROMS];
static int   romCount = 0;
static int   selectedRom = 0;
static int   scrollOffset = 0;

// In-game menu
typedef enum {
    IGMENU_RESUME = 0,
    IGMENU_SAVE_STATE,
    IGMENU_LOAD_STATE,
    IGMENU_TURBO,
    IGMENU_PALETTE,
    IGMENU_SCALE,
    IGMENU_RESET,
    IGMENU_QUIT,
    IGMENU_COUNT
} igmenu_item_t;
static int igMenuSel = 0;
static int saveSlot = 0;       // Current save slot (0-3)

// Turbo speed
static bool turboMode = false;
static int  turboFrameSkip = 3; // Render every Nth frame in turbo
static int  frameCounter = 0;

// Current ROM path for save/load
static char currentRomPath[256] = {0};

// (NES palette globals defined above with palette tables)

// Favorites & Recently Played
#define MAX_FAVORITES 20
#define MAX_RECENT 10
static char *favoritesList[MAX_FAVORITES];
static int   favCount = 0;
static char *recentList[MAX_RECENT];
static int   recentCount = 0;

// Launcher view filter
typedef enum { VIEW_ALL = 0, VIEW_FAVORITES, VIEW_RECENT, VIEW_COUNT } view_mode_t;
static view_mode_t viewMode = VIEW_ALL;

// Scaling
typedef enum { SCALE_1TO1 = 0, SCALE_FIT, SCALE_STRETCH, SCALE_MODE_COUNT } scale_mode_t;
static scale_mode_t scaleMode = SCALE_1TO1;
static const char *scaleModeNames[] = { "1:1", "Fit", "Stretch" };

// ============================================================================
//  HARDWARE INIT
// ============================================================================
bool initVGA() {
    DisplayController.begin(
        VGA_RED1, VGA_RED0,
        VGA_GREEN1, VGA_GREEN0,
        VGA_BLUE1, VGA_BLUE0,
        VGA_HSYNC, VGA_VSYNC
    );
    DisplayController.setResolution(QVGA_320x240_60Hz);
    Serial.println("[VGA] 320x240 @ 60Hz OK");
    return true;
}

bool initPS2() {
    PS2Ctrl.begin(KBD_CLK, KBD_DAT, MOUSE_CLK, MOUSE_DAT);
    Kbd = PS2Ctrl.keyboard();
    Serial.printf("[PS2] Keyboard: %s\n", Kbd ? "OK" : "N/A");
    return Kbd != nullptr;
}

bool initSD() {
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 20000000)) {
        Serial.println("[SD] Mount FAILED");
        return false;
    }
    Serial.printf("[SD] %llu MB\n", SD.cardSize() / (1024 * 1024));
    return true;
}

// ============================================================================
//  ROM TYPE DETECTION
// ============================================================================
emu_type_t getEmuType(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return EMU_NONE;
    ext++;
    // NES family (retro-go: "nes fc fds nsf zip")
    if (strcasecmp(ext, "nes") == 0) return EMU_NES;
    if (strcasecmp(ext, "fc")  == 0) return EMU_NES;
    if (strcasecmp(ext, "fds") == 0) return EMU_NES;
    if (strcasecmp(ext, "nsf") == 0) return EMU_NES;
    // Gameboy (retro-go: "gb gbc zip")
    if (strcasecmp(ext, "gb") == 0)  return EMU_GB;
    if (strcasecmp(ext, "gbc") == 0) return EMU_GB;
    // SMS (retro-go: "sms sg zip")
    if (strcasecmp(ext, "sms") == 0) return EMU_SMS;
    if (strcasecmp(ext, "sg") == 0)  return EMU_SG1000;
    if (strcasecmp(ext, "sg1") == 0) return EMU_SG1000;
    // Game Gear (retro-go: "gg zip")
    if (strcasecmp(ext, "gg") == 0)  return EMU_GG;
    // PC Engine (retro-go: "pce zip")
    if (strcasecmp(ext, "pce") == 0) return EMU_PCE;
    // SNES (retro-go: "smc sfc zip")
    if (strcasecmp(ext, "sfc") == 0) return EMU_SNES;
    if (strcasecmp(ext, "smc") == 0) return EMU_SNES;
    // Lynx (retro-go: "lnx zip")
    if (strcasecmp(ext, "lnx") == 0) return EMU_LYNX;
    // Game & Watch (retro-go: "gw")
    if (strcasecmp(ext, "gw") == 0)  return EMU_GW;
    // ColecoVision (retro-go: "col rom zip")
    if (strcasecmp(ext, "col") == 0) return EMU_COLECO;
    // Mega Drive (retro-go: "md gen bin zip")
    if (strcasecmp(ext, "md") == 0)  return EMU_GENESIS;
    if (strcasecmp(ext, "gen") == 0) return EMU_GENESIS;
    if (strcasecmp(ext, "bin") == 0) return EMU_GENESIS;
    // DOOM (retro-go: "wad zip")
    if (strcasecmp(ext, "wad") == 0) return EMU_DOOM;
    // MSX (retro-go: "rom mx1 mx2 dsk")
    if (strcasecmp(ext, "rom") == 0) return EMU_MSX;
    if (strcasecmp(ext, "mx1") == 0) return EMU_MSX;
    if (strcasecmp(ext, "mx2") == 0) return EMU_MSX;
    if (strcasecmp(ext, "dsk") == 0) return EMU_MSX;
    // ZIP handled at load time
    if (strcasecmp(ext, "zip") == 0) return EMU_NONE;
    return EMU_NONE;
}

// Display name (for UI)
const char* emuName(emu_type_t t) {
    switch(t) {
        case EMU_NES:     return "NES";
        case EMU_GB:      return "GB";
        case EMU_SMS:     return "SMS";
        case EMU_GG:      return "GG";
        case EMU_PCE:     return "PCE";
        case EMU_SNES:    return "SNES";
        case EMU_LYNX:    return "LYNX";
        case EMU_GW:      return "G&W";
        case EMU_SG1000:  return "SG";
        case EMU_COLECO:  return "COL";
        case EMU_GENESIS: return "MD";
        case EMU_DOOM:    return "DOOM";
        case EMU_MSX:     return "MSX";
        default:          return "?";
    }
}

// Retro-go compatible short name (for save/cover paths — lowercase)
const char* emuShortName(emu_type_t t) {
    switch(t) {
        case EMU_NES:     return "nes";
        case EMU_GB:      return "gb";
        case EMU_SMS:     return "sms";
        case EMU_GG:      return "gg";
        case EMU_PCE:     return "pce";
        case EMU_SNES:    return "snes";
        case EMU_LYNX:    return "lnx";
        case EMU_GW:      return "gw";
        case EMU_SG1000:  return "sms";  // retro-go groups SG-1000 with SMS
        case EMU_COLECO:  return "col";
        case EMU_GENESIS: return "md";
        case EMU_DOOM:    return "doom";
        case EMU_MSX:     return "msx";
        default:          return "misc";
    }
}

// ============================================================================
//  ROM SCANNER — Recursive, multi-directory
// ============================================================================
void scanDir(File dir, const char *base) {
    File entry;
    while ((entry = dir.openNextFile()) && romCount < MAX_ROMS) {
        if (entry.isDirectory()) {
            String sub = String(base) + "/" + entry.name();
            scanDir(entry, sub.c_str());
        } else {
            if (getEmuType(entry.name()) != EMU_NONE || zip_is_zip_file(entry.name())) {
                char fp[256];
                snprintf(fp, sizeof(fp), "%s/%s", base, entry.name());
                romFiles[romCount++] = strdup(fp);
            }
        }
        entry.close();
    }
}

void scanAllRoms() {
    for (int i = 0; i < romCount; i++) { free(romFiles[i]); romFiles[i]=NULL; }
    romCount = 0; selectedRom = 0; scrollOffset = 0;
    // Retro-go compatible: /roms/{platform}/ (primary)
    const char *dirs[] = {
        "/roms/nes", "/roms/gb", "/roms/gbc", "/roms/sms", "/roms/gg",
        "/roms/pce", "/roms/snes", "/roms/lnx", "/roms/gw", "/roms/col",
        "/roms/md", "/roms/doom", "/roms/msx",
        "/roms",  // fallback: scan all of /roms/
    };
    int dirCount = sizeof(dirs) / sizeof(dirs[0]);
    for (int i = 0; i < dirCount; i++) {
        File d = SD.open(dirs[i]);
        if (d && d.isDirectory()) { scanDir(d, dirs[i]); d.close(); }
    }
    Serial.printf("[ROM] Found %d file(s)\n", romCount);
}

// ============================================================================
//  FAVORITES & RECENTLY PLAYED
// ============================================================================
bool isFavorite(const char *path) {
    for (int i = 0; i < favCount; i++)
        if (strcmp(favoritesList[i], path) == 0) return true;
    return false;
}

void loadFavorites() {
    for (int i = 0; i < favCount; i++) { free(favoritesList[i]); favoritesList[i]=NULL; }
    favCount = 0;
    File f = SD.open("/retro-go/config/favorites.txt", FILE_READ);
    if (!f) return;
    char line[256];
    while (f.available() && favCount < MAX_FAVORITES) {
        int len = 0;
        while (f.available() && len < 255) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len > 0) favoritesList[favCount++] = strdup(line);
    }
    f.close();
    Serial.printf("[FAV] Loaded %d favorites\n", favCount);
}

void saveFavorites() {
    if (!SD.exists("/retro-go")) SD.mkdir("/retro-go");
    if (!SD.exists("/retro-go/config")) SD.mkdir("/retro-go/config");
    File f = SD.open("/retro-go/config/favorites.txt", FILE_WRITE);
    if (!f) return;
    for (int i = 0; i < favCount; i++) {
        f.println(favoritesList[i]);
    }
    f.close();
}

void toggleFavorite(const char *path) {
    // Check if already favorite
    for (int i = 0; i < favCount; i++) {
        if (strcmp(favoritesList[i], path) == 0) {
            // Remove
            free(favoritesList[i]);
            for (int j = i; j < favCount-1; j++) favoritesList[j] = favoritesList[j+1];
            favCount--;
            saveFavorites();
            Serial.printf("[FAV] Removed: %s\n", path);
            return;
        }
    }
    // Add
    if (favCount < MAX_FAVORITES) {
        favoritesList[favCount++] = strdup(path);
        saveFavorites();
        Serial.printf("[FAV] Added: %s\n", path);
    }
}

void loadRecent() {
    for (int i = 0; i < recentCount; i++) { free(recentList[i]); recentList[i]=NULL; }
    recentCount = 0;
    File f = SD.open("/retro-go/config/recent.txt", FILE_READ);
    if (!f) return;
    char line[256];
    while (f.available() && recentCount < MAX_RECENT) {
        int len = 0;
        while (f.available() && len < 255) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len > 0) recentList[recentCount++] = strdup(line);
    }
    f.close();
}

void saveRecent() {
    if (!SD.exists("/retro-go")) SD.mkdir("/retro-go");
    if (!SD.exists("/retro-go/config")) SD.mkdir("/retro-go/config");
    File f = SD.open("/retro-go/config/recent.txt", FILE_WRITE);
    if (!f) return;
    for (int i = 0; i < recentCount; i++) f.println(recentList[i]);
    f.close();
}

void addToRecent(const char *path) {
    // Remove if already in list
    for (int i = 0; i < recentCount; i++) {
        if (strcmp(recentList[i], path) == 0) {
            free(recentList[i]);
            for (int j = i; j < recentCount-1; j++) recentList[j] = recentList[j+1];
            recentCount--;
            break;
        }
    }
    // Push to front (most recent first)
    if (recentCount >= MAX_RECENT) {
        free(recentList[recentCount-1]);
        recentCount--;
    }
    for (int i = recentCount; i > 0; i--) recentList[i] = recentList[i-1];
    recentList[0] = strdup(path);
    recentCount++;
    saveRecent();
}

// ============================================================================
//  DRAW FUNCTIONS
// ============================================================================
void drawMenu() {
    auto cv = new fabgl::Canvas(&DisplayController);
    cv->setBrushColor(fabgl::Color::Black);
    cv->clear();

    // Title
    cv->setPenColor(fabgl::Color::BrightCyan);
    cv->setGlyphOptions(fabgl::GlyphOptions().DoubleWidth(1));
    cv->drawText(50, 4, "RETRO-GAMER");
    cv->setGlyphOptions(fabgl::GlyphOptions());

    // View mode tabs
    const char *tabs[] = { "All", "Favs", "Recent" };
    int tx = 8;
    for (int i = 0; i < VIEW_COUNT; i++) {
        if (i == viewMode) {
            cv->setBrushColor(fabgl::Color::Blue);
            cv->fillRectangle(tx-2, 20, tx+38, 33);
            cv->setPenColor(fabgl::Color::BrightWhite);
        } else {
            cv->setPenColor(fabgl::Color::White);
        }
        cv->drawText(tx, 22, tabs[i]);
        cv->setBrushColor(fabgl::Color::Black);
        tx += 44;
    }

    // Scaling info
    cv->setPenColor(fabgl::Color::BrightGreen);
    cv->drawTextFmt(220, 22, "Scale:%s", scaleModeNames[scaleMode]);

    cv->setPenColor(fabgl::Color::BrightYellow);
    cv->drawLine(4, 36, 316, 36);

    if (romCount == 0 && viewMode == VIEW_ALL) {
        cv->setPenColor(fabgl::Color::BrightRed);
        cv->drawText(30, 100, "No ROM files found!");
        cv->setPenColor(fabgl::Color::White);
        cv->drawText(8, 118, ".nes .fc .fds .gb .gbc .sms .gg .pce .lnx");
        cv->drawText(8, 134, ".sfc .smc .gw .col .rom .md .gen .bin .wad");
        cv->drawText(15, 150, "Put ROMs in /roms/{nes,gb,...}/");
    } else {
        // Build filtered index list
        int filtered[MAX_ROMS];
        int fCount = 0;

        if (viewMode == VIEW_FAVORITES) {
            for (int i = 0; i < romCount && fCount < MAX_ROMS; i++)
                if (isFavorite(romFiles[i])) filtered[fCount++] = i;
        } else if (viewMode == VIEW_RECENT) {
            for (int r = 0; r < recentCount && fCount < MAX_ROMS; r++) {
                for (int i = 0; i < romCount; i++) {
                    if (strcmp(romFiles[i], recentList[r]) == 0) {
                        filtered[fCount++] = i;
                        break;
                    }
                }
            }
        } else {
            for (int i = 0; i < romCount && fCount < MAX_ROMS; i++)
                filtered[fCount++] = i;
        }

        if (fCount == 0) {
            cv->setPenColor(fabgl::Color::White);
            cv->drawText(60, 100, viewMode == VIEW_FAVORITES ? "No favorites yet (F=add)" : "No recent games");
        } else {
            // Clamp selection
            if (selectedRom >= fCount) selectedRom = fCount - 1;
            if (selectedRom < 0) selectedRom = 0;

            int vis = 12, y = 40, lh = 15;
            for (int fi = scrollOffset; fi < fCount && fi < scrollOffset + vis; fi++) {
                int i = filtered[fi];
                const char *fn = strrchr(romFiles[i], '/');
                fn = fn ? fn + 1 : romFiles[i];
                emu_type_t et = getEmuType(fn);

                if (fi == selectedRom) {
                    cv->setBrushColor(fabgl::Color::Blue);
                    cv->fillRectangle(4, y-1, 222, y+lh-2);
                    cv->setPenColor(fabgl::Color::BrightWhite);
                } else {
                    cv->setPenColor(
                        et==EMU_NES  ? fabgl::Color::BrightRed :
                        et==EMU_SNES ? fabgl::Color::BrightMagenta :
                        et==EMU_GB   ? fabgl::Color::BrightGreen :
                        et==EMU_SMS  ? fabgl::Color::BrightCyan :
                        et==EMU_GG   ? fabgl::Color::BrightYellow :
                        et==EMU_PCE  ? fabgl::Color::BrightWhite :
                        et==EMU_LYNX ? fabgl::Color::Yellow :
                        et==EMU_GW   ? fabgl::Color::Green :
                        et==EMU_SG1000 ? fabgl::Color::BrightCyan :
                        et==EMU_COLECO ? fabgl::Color::BrightMagenta :
                        et==EMU_GENESIS ? fabgl::Color::BrightRed :
                        et==EMU_DOOM ? fabgl::Color::BrightYellow :
                        et==EMU_MSX  ? fabgl::Color::BrightMagenta :
                        fabgl::Color::White);
                }
                char line[44]; char sn[30];
                strncpy(sn, fn, 29); sn[29]='\0';
                bool fav = isFavorite(romFiles[i]);
                snprintf(line, sizeof(line), "%s[%-3s] %s", fav?"*":"", emuName(et), sn);
                cv->drawText(8, y, line);
                y += lh;
            }
            cv->setBrushColor(fabgl::Color::Black);
            cv->setPenColor(fabgl::Color::White);
            cv->drawTextFmt(240, 228, "%d/%d", selectedRom+1, fCount);

            // Cover art preview (right side, ~86x120 pixels)
            if (selectedRom >= 0 && selectedRom < fCount) {
                static uint8_t *coverBuf = NULL;
                static int lastCoverRom = -1;
                static bool hasCover = false;

                if (!coverBuf) {
                    coverBuf = (uint8_t *)heap_caps_calloc(COVER_WIDTH * COVER_HEIGHT, 1, MALLOC_CAP_SPIRAM);
                }

                int ri = filtered[selectedRom];
                if (coverBuf && ri != lastCoverRom) {
                    lastCoverRom = ri;
                    emu_type_t et = getEmuType(romFiles[ri]);
                    hasCover = cover_load(romFiles[ri], emuShortName(et), coverBuf);
                }

                if (hasCover) {
                    // Draw cover at right side (x=228, y=40, 86x120)
                    int cx = 228, cy = 40;
                    // Use Canvas setPixel for VGA8 compatibility
                    for (int y = 0; y < COVER_HEIGHT && (cy+y) < 224; y++) {
                        for (int x = 0; x < COVER_WIDTH && (cx+x) < 320; x++) {
                            uint8_t c = coverBuf[y * COVER_WIDTH + x];
                            // Map SBGR2222 to nearest FabGL Color
                            uint8_t r2 = c & 0x03;
                            uint8_t g2 = (c >> 2) & 0x03;
                            uint8_t b2 = (c >> 4) & 0x03;
                            fabgl::RGB888 rgb = {(uint8_t)(r2 * 85), (uint8_t)(g2 * 85), (uint8_t)(b2 * 85)};
                            cv->setPixel(cx+x, cy+y, rgb);
                        }
                    }
                    // Draw border
                    cv->setPenColor(fabgl::Color::White);
                    cv->drawRectangle(cx-1, cy-1, cx+COVER_WIDTH, cy+COVER_HEIGHT);
                }
            }
        }
    }

    cv->setPenColor(fabgl::Color::BrightYellow);
    cv->drawLine(4, 224, 316, 224);
    cv->setPenColor(fabgl::Color::White);
    cv->drawText(8, 228, "Enter=Play F=Fav Tab=View S=Scl W=WiFi");
    // WiFi status indicator
    if (wifi_manager_is_connected()) {
        cv->setPenColor(fabgl::Color::BrightGreen);
        cv->drawTextFmt(260, 4, "WiFi:%s", wifi_manager_get_ip());
    }
    delete cv;
}

void drawBoot() {
    auto cv = new fabgl::Canvas(&DisplayController);
    cv->setBrushColor(fabgl::Color::Black);
    cv->clear();
    cv->setPenColor(fabgl::Color::BrightCyan);
    cv->setGlyphOptions(fabgl::GlyphOptions().DoubleWidth(1));
    cv->drawText(40, 15, "RETRO-GAMER");
    cv->setGlyphOptions(fabgl::GlyphOptions());
    cv->setPenColor(fabgl::Color::White);
    cv->drawText(20, 40, "Multi-System Retro Emulator");
    cv->drawText(20, 55, "Olimex ESP32-SBC-FabGL Rev B");
    cv->setPenColor(fabgl::Color::BrightYellow);
    cv->drawLine(10, 72, 310, 72);
    int y = 82;
    cv->setPenColor(fabgl::Color::BrightGreen);
    cv->drawTextFmt(10, y, "CPU: ESP32 @ %dMHz", ESP.getCpuFreqMHz()); y+=14;
    cv->drawTextFmt(10, y, "PSRAM: %d KB", ESP.getPsramSize()/1024); y+=14;
    cv->drawTextFmt(10, y, "Free Heap: %d KB", ESP.getFreeHeap()/1024); y+=14;
    cv->drawTextFmt(10, y, "SD: %s", SD.cardSize()>0?"OK":"N/A"); y+=14;
    cv->drawTextFmt(10, y, "Keyboard: %s", Kbd?"OK":"N/A"); y+=20;
    cv->setPenColor(fabgl::Color::BrightRed);    cv->drawText(10,y,"NES ");
    cv->setPenColor(fabgl::Color::BrightMagenta); cv->drawText(44,y,"SNES ");
    cv->setPenColor(fabgl::Color::BrightGreen);   cv->drawText(84,y,"GB ");
    cv->setPenColor(fabgl::Color::BrightCyan);    cv->drawText(110,y,"SMS ");
    cv->setPenColor(fabgl::Color::BrightWhite);   cv->drawText(144,y,"PCE ");
    cv->setPenColor(fabgl::Color::Yellow);        cv->drawText(178,y,"Lynx ");
    cv->setPenColor(fabgl::Color::Green);         cv->drawText(218,y,"G&W"); y+=18;
    cv->setPenColor(fabgl::Color::BrightYellow);
    cv->drawLine(10,y,310,y); y+=8;
    cv->setPenColor(fabgl::Color::White);
    cv->drawText(10,y,"Arrows/WASD=Move  Z=A  X=B"); y+=14;
    cv->drawText(10,y,"Enter=Start  RShift=Select  Esc=Menu");
    delete cv;
    delay(2500);
}

// ============================================================================
//  SAVE STATE HELPERS
// ============================================================================
void getSaveStatePath(char *out, size_t sz, int slot) {
    // Extract ROM filename with extension (retro-go compatible: game.nes.sav0)
    const char *fn = strrchr(currentRomPath, '/');
    fn = fn ? fn + 1 : currentRomPath;

    // Build save path: /retro-go/saves/{short_name}/{romfile}.sav{slot}
    const char *emuDir = emuShortName(currentEmu);
    snprintf(out, sz, "/retro-go/saves/%s/%s.sav%d", emuDir, fn, slot);
}

bool ensureSaveDir() {
    char dir[128];
    const char *emuDir = emuShortName(currentEmu);
    snprintf(dir, sizeof(dir), "/retro-go/saves/%s", emuDir);

    // Create dirs recursively
    if (!SD.exists("/retro-go")) SD.mkdir("/retro-go");
    if (!SD.exists("/retro-go/saves")) SD.mkdir("/retro-go/saves");
    if (!SD.exists(dir)) SD.mkdir(dir);
    return SD.exists(dir);
}

bool saveStateToSD(int slot) {
    if (!ensureSaveDir()) return false;

    // Each bridge has its own save format — for now we call bridge-specific save
    char path[256];
    getSaveStatePath(path, sizeof(path), slot);

    bool ok = false;
    switch(currentEmu) {
        case EMU_NES:     ok = nes_bridge_save_state(path);     break;
        case EMU_GB:      ok = gb_bridge_save_state(path);      break;
        case EMU_SMS:
        case EMU_GG:
        case EMU_SG1000:
        case EMU_COLECO:  ok = sms_bridge_save_state(path);     break;
        case EMU_PCE:     ok = pce_bridge_save_state(path);     break;
        case EMU_SNES:    ok = snes_bridge_save_state(path);    break;
        case EMU_LYNX:    ok = lynx_bridge_save_state(path);    break;
        case EMU_GW:      ok = gw_bridge_save_state(path);      break;
        case EMU_GENESIS: ok = genesis_bridge_save_state(path); break;
        case EMU_DOOM:    ok = doom_bridge_save_state(path);    break;
        case EMU_MSX:     ok = msx_bridge_save_state(path);     break;
        default: break;
    }
    Serial.printf("[SAVE] Slot %d → %s: %s\n", slot, path, ok ? "OK" : "FAIL");
    return ok;
}

bool loadStateFromSD(int slot) {
    char path[256];
    getSaveStatePath(path, sizeof(path), slot);

    if (!SD.exists(path)) {
        Serial.printf("[LOAD] Slot %d: file not found\n", slot);
        return false;
    }

    bool ok = false;
    switch(currentEmu) {
        case EMU_NES:     ok = nes_bridge_load_state(path);     break;
        case EMU_GB:      ok = gb_bridge_load_state(path);      break;
        case EMU_SMS:
        case EMU_GG:
        case EMU_SG1000:
        case EMU_COLECO:  ok = sms_bridge_load_state(path);     break;
        case EMU_PCE:     ok = pce_bridge_load_state(path);     break;
        case EMU_SNES:    ok = snes_bridge_load_state(path);    break;
        case EMU_LYNX:    ok = lynx_bridge_load_state(path);    break;
        case EMU_GW:      ok = gw_bridge_load_state(path);      break;
        case EMU_GENESIS: ok = genesis_bridge_load_state(path); break;
        case EMU_DOOM:    ok = doom_bridge_load_state(path);    break;
        case EMU_MSX:     ok = msx_bridge_load_state(path);     break;
        default: break;
    }
    Serial.printf("[LOAD] Slot %d ← %s: %s\n", slot, path, ok ? "OK" : "FAIL");
    return ok;
}

// ============================================================================
//  IN-GAME MENU
// ============================================================================
void drawInGameMenu() {
    auto cv = new fabgl::Canvas(&DisplayController);

    // Semi-transparent overlay box
    int bx = 70, by = 30, bw = 180, bh = 190;
    cv->setBrushColor(fabgl::Color::Black);
    cv->fillRectangle(bx, by, bx+bw, by+bh);
    cv->setPenColor(fabgl::Color::BrightCyan);
    cv->drawRectangle(bx, by, bx+bw, by+bh);
    cv->drawRectangle(bx+1, by+1, bx+bw-1, by+bh-1);

    // Title
    cv->setPenColor(fabgl::Color::BrightYellow);
    cv->drawText(bx+50, by+8, "== PAUSE ==");

    // Menu items
    const char *items[IGMENU_COUNT];
    char turboBuf[24], saveBuf[24], loadBuf[24], palBuf[32], scaleBuf[24];
    snprintf(turboBuf, sizeof(turboBuf), "Turbo: %s", turboMode ? "ON" : "OFF");
    snprintf(saveBuf,  sizeof(saveBuf),  "Save State [%d]", saveSlot);
    snprintf(loadBuf,  sizeof(loadBuf),  "Load State [%d]", saveSlot);

    // Build palette label based on current emulator
    if (currentEmu == EMU_GB) {
        snprintf(palBuf, sizeof(palBuf), "Palette: %s", gb_bridge_get_palette_name(gb_bridge_get_palette()));
    } else if (currentEmu == EMU_NES) {
        snprintf(palBuf, sizeof(palBuf), "Palette: %s", nes_palette_names[nesCurrentPalette]);
    } else {
        snprintf(palBuf, sizeof(palBuf), "Palette: N/A");
    }
    snprintf(scaleBuf, sizeof(scaleBuf), "Scale: %s", scaleModeNames[scaleMode]);

    items[IGMENU_RESUME]     = "Resume";
    items[IGMENU_SAVE_STATE] = saveBuf;
    items[IGMENU_LOAD_STATE] = loadBuf;
    items[IGMENU_TURBO]      = turboBuf;
    items[IGMENU_PALETTE]    = palBuf;
    items[IGMENU_SCALE]      = scaleBuf;
    items[IGMENU_RESET]      = "Reset Game";
    items[IGMENU_QUIT]       = "Quit to Menu";

    int iy = by + 30;
    for (int i = 0; i < IGMENU_COUNT; i++) {
        if (i == igMenuSel) {
            cv->setBrushColor(fabgl::Color::Blue);
            cv->fillRectangle(bx+6, iy-1, bx+bw-6, iy+14);
            cv->setPenColor(fabgl::Color::BrightWhite);
        } else {
            cv->setPenColor(fabgl::Color::White);
        }
        cv->drawText(bx+16, iy, items[i]);
        cv->setBrushColor(fabgl::Color::Black);
        iy += 18;
    }

    // Controls hint
    cv->setPenColor(fabgl::Color::BrightGreen);
    cv->drawText(bx+10, by+bh-16, "^v:Sel <> :Slot Enter:OK");
    delete cv;
}


// ============================================================================
//  BLIT FUNCTIONS — Emulator framebuffer → VGA
// ============================================================================

// NES: 256x240, 8-bit palette → center in 320x240
void blitNES(uint8_t *fb) {
    if (!fb) return;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        uint8_t *nes = fb + y * 272 + 8; // pitch=272, overdraw=8
        for (int x = 0; x < 32; x++) VGA_PIXELINROW(vga, x) = 0;
        for (int x = 0; x < 256; x++) VGA_PIXELINROW(vga, x+32) = nesLUT[nes[x]];
        for (int x = 288; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
    }
}

// GB/GBC: 160x144, RGB565 → center in 320x240
void blitGB(uint16_t *fb) {
    if (!fb) return;
    int xOff = 80, yOff = 48; // (320-160)/2, (240-144)/2
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        if (y < yOff || y >= yOff + 144) {
            for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        } else {
            uint16_t *gb = fb + (y - yOff) * 160;
            for (int x = 0; x < xOff; x++) VGA_PIXELINROW(vga, x) = 0;
            for (int x = 0; x < 160; x++) VGA_PIXELINROW(vga, x+xOff) = rgb565toVGA(gb[x]);
            for (int x = xOff+160; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        }
    }
}

// SMS: 256x192 paletted, center in 320x240
void blitSMS(uint8_t *fb, uint16_t *pal, int w, int h) {
    if (!fb || !pal) return;
    // Pre-convert palette (32 entries max)
    uint8_t vgaPal[32];
    for (int i = 0; i < 32; i++) vgaPal[i] = rgb565toVGA(pal[i]);

    int xOff = (320 - w) / 2;
    int yOff = (240 - h) / 2;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        if (y < yOff || y >= yOff + h) {
            for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        } else {
            uint8_t *src = fb + (y - yOff) * 256; // SMS pitch = 256
            for (int x = 0; x < xOff; x++) VGA_PIXELINROW(vga, x) = 0;
            for (int x = 0; x < w; x++) VGA_PIXELINROW(vga, x+xOff) = vgaPal[src[x] & 0x1F];
            for (int x = xOff+w; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        }
    }
}

// PCE: 256x224 paletted → center in 320x240
void blitPCE(uint8_t *fb, uint16_t *pal, int w, int h) {
    if (!fb || !pal) return;
    uint8_t vgaPal[256];
    for (int i = 0; i < 256; i++) vgaPal[i] = rgb565toVGA(pal[i]);
    int xOff = (320 - w) / 2; if (xOff < 0) xOff = 0;
    int yOff = (240 - h) / 2; if (yOff < 0) yOff = 0;
    int drawW = (w > 320) ? 320 : w;
    int drawH = (h > 240) ? 240 : h;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        if (y < yOff || y >= yOff + drawH) {
            for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        } else {
            uint8_t *src = fb + (y - yOff) * (352 + 16); // XBUF_WIDTH
            for (int x = 0; x < xOff; x++) VGA_PIXELINROW(vga, x) = 0;
            for (int x = 0; x < drawW; x++) VGA_PIXELINROW(vga, x+xOff) = vgaPal[src[x]];
            for (int x = xOff+drawW; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        }
    }
}

// SNES: 256x224, RGB565 LE → center in 320x240
void blitSNES(uint16_t *fb, int w, int h) {
    if (!fb) return;
    int xOff = (320 - w) / 2, yOff = (240 - h) / 2;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        if (y < yOff || y >= yOff + h) {
            for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        } else {
            uint16_t *src = fb + (y - yOff) * w;
            for (int x = 0; x < xOff; x++) VGA_PIXELINROW(vga, x) = 0;
            for (int x = 0; x < w; x++) VGA_PIXELINROW(vga, x+xOff) = rgb565toVGA(src[x]);
            for (int x = xOff+w; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        }
    }
}

// LYNX: 160x102, RGB565 BE → center in 320x240
void blitLynx(uint16_t *fb) {
    if (!fb) return;
    int xOff = 80, yOff = 69;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        if (y < yOff || y >= yOff + 102) {
            for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        } else {
            uint16_t *src = fb + (y - yOff) * 160;
            for (int x = 0; x < xOff; x++) VGA_PIXELINROW(vga, x) = 0;
            for (int x = 0; x < 160; x++) VGA_PIXELINROW(vga, x+xOff) = rgb565toVGA(src[x]);
            for (int x = xOff+160; x < 320; x++) VGA_PIXELINROW(vga, x) = 0;
        }
    }
}

// GW: 320x240, RGB565 → full screen
void blitGW(uint16_t *fb) {
    if (!fb) return;
    for (int y = 0; y < 240; y++) {
        auto vga = (uint8_t*)DisplayController.getScanline(y);
        uint16_t *src = fb + y * 320;
        for (int x = 0; x < 320; x++) VGA_PIXELINROW(vga, x) = rgb565toVGA(src[x]);
    }
}

// ============================================================================
//  KEYBOARD → GENERIC BUTTONS
// ============================================================================
#define BTN_UP    0x01
#define BTN_DOWN  0x02
#define BTN_LEFT  0x04
#define BTN_RIGHT 0x08
#define BTN_A     0x10
#define BTN_B     0x20
#define BTN_START 0x40
#define BTN_SEL   0x80

uint32_t readButtons() {
    uint32_t b = 0;
    if (!Kbd) return b;
    if (Kbd->isVKDown(fabgl::VK_UP)    || Kbd->isVKDown(fabgl::VK_w))    b|=BTN_UP;
    if (Kbd->isVKDown(fabgl::VK_DOWN)  || Kbd->isVKDown(fabgl::VK_s))    b|=BTN_DOWN;
    if (Kbd->isVKDown(fabgl::VK_LEFT)  || Kbd->isVKDown(fabgl::VK_a))    b|=BTN_LEFT;
    if (Kbd->isVKDown(fabgl::VK_RIGHT) || Kbd->isVKDown(fabgl::VK_d))    b|=BTN_RIGHT;
    if (Kbd->isVKDown(fabgl::VK_z)     || Kbd->isVKDown(fabgl::VK_SPACE))b|=BTN_A;
    if (Kbd->isVKDown(fabgl::VK_x)     || Kbd->isVKDown(fabgl::VK_LCTRL))b|=BTN_B;
    if (Kbd->isVKDown(fabgl::VK_RETURN))  b|=BTN_START;
    if (Kbd->isVKDown(fabgl::VK_RSHIFT))  b|=BTN_SEL;
    return b;
}

// Generic → NES
uint32_t toNES(uint32_t b) {
    uint32_t n=0;
    if(b&BTN_UP)n|=NES_BTN_UP; if(b&BTN_DOWN)n|=NES_BTN_DOWN;
    if(b&BTN_LEFT)n|=NES_BTN_LEFT; if(b&BTN_RIGHT)n|=NES_BTN_RIGHT;
    if(b&BTN_A)n|=NES_BTN_A; if(b&BTN_B)n|=NES_BTN_B;
    if(b&BTN_START)n|=NES_BTN_START; if(b&BTN_SEL)n|=NES_BTN_SELECT;
    return n;
}
// Generic → GB
uint32_t toGB(uint32_t b) {
    uint32_t g=0;
    if(b&BTN_UP)g|=GB_BTN_UP; if(b&BTN_DOWN)g|=GB_BTN_DOWN;
    if(b&BTN_LEFT)g|=GB_BTN_LEFT; if(b&BTN_RIGHT)g|=GB_BTN_RIGHT;
    if(b&BTN_A)g|=GB_BTN_A; if(b&BTN_B)g|=GB_BTN_B;
    if(b&BTN_START)g|=GB_BTN_START; if(b&BTN_SEL)g|=GB_BTN_SELECT;
    return g;
}
// Generic → SMS
uint32_t toSMS(uint32_t b) {
    uint32_t s=0;
    if(b&BTN_UP)s|=SMS_BTN_UP; if(b&BTN_DOWN)s|=SMS_BTN_DOWN;
    if(b&BTN_LEFT)s|=SMS_BTN_LEFT; if(b&BTN_RIGHT)s|=SMS_BTN_RIGHT;
    if(b&BTN_A)s|=SMS_BTN_A; if(b&BTN_B)s|=SMS_BTN_B;
    if(b&BTN_START)s|=SMS_BTN_START;
    return s;
}
// Generic → PCE
uint32_t toPCE(uint32_t b) {
    uint32_t p=0;
    if(b&BTN_UP)p|=PCE_BTN_UP; if(b&BTN_DOWN)p|=PCE_BTN_DOWN;
    if(b&BTN_LEFT)p|=PCE_BTN_LEFT; if(b&BTN_RIGHT)p|=PCE_BTN_RIGHT;
    if(b&BTN_A)p|=PCE_BTN_A; if(b&BTN_B)p|=PCE_BTN_B;
    if(b&BTN_START)p|=PCE_BTN_RUN; if(b&BTN_SEL)p|=PCE_BTN_SELECT;
    return p;
}
// Generic → SNES (direct mask mapping)
uint32_t toSNES(uint32_t b) {
    uint32_t s=0;
    if(b&BTN_UP)s|=SNES_BTN_UP; if(b&BTN_DOWN)s|=SNES_BTN_DOWN;
    if(b&BTN_LEFT)s|=SNES_BTN_LEFT; if(b&BTN_RIGHT)s|=SNES_BTN_RIGHT;
    if(b&BTN_A)s|=SNES_BTN_A; if(b&BTN_B)s|=SNES_BTN_B;
    if(b&BTN_START)s|=SNES_BTN_START; if(b&BTN_SEL)s|=SNES_BTN_SELECT;
    return s;
}
// Generic → LYNX
uint32_t toLynx(uint32_t b) {
    uint32_t l=0;
    if(b&BTN_UP)l|=LYNX_BTN_UP; if(b&BTN_DOWN)l|=LYNX_BTN_DOWN;
    if(b&BTN_LEFT)l|=LYNX_BTN_LEFT; if(b&BTN_RIGHT)l|=LYNX_BTN_RIGHT;
    if(b&BTN_A)l|=LYNX_BTN_A; if(b&BTN_B)l|=LYNX_BTN_B;
    if(b&BTN_START)l|=LYNX_BTN_OPT2; if(b&BTN_SEL)l|=LYNX_BTN_OPT1;
    return l;
}
// Generic → GW
uint32_t toGW(uint32_t b) {
    uint32_t g=0;
    if(b&BTN_UP)g|=GW_BTN_UP; if(b&BTN_DOWN)g|=GW_BTN_DOWN;
    if(b&BTN_LEFT)g|=GW_BTN_LEFT; if(b&BTN_RIGHT)g|=GW_BTN_RIGHT;
    if(b&BTN_A)g|=GW_BTN_A; if(b&BTN_B)g|=GW_BTN_B;
    if(b&BTN_START)g|=GW_BTN_GAME; if(b&BTN_SEL)g|=GW_BTN_TIME;
    return g;
}
// Generic → Genesis
uint32_t toGenesis(uint32_t b) {
    uint32_t g=0;
    if(b&BTN_UP)g|=GEN_BTN_UP; if(b&BTN_DOWN)g|=GEN_BTN_DOWN;
    if(b&BTN_LEFT)g|=GEN_BTN_LEFT; if(b&BTN_RIGHT)g|=GEN_BTN_RIGHT;
    if(b&BTN_A)g|=GEN_BTN_A; if(b&BTN_B)g|=GEN_BTN_B;
    if(b&BTN_START)g|=GEN_BTN_START;
    return g;
}

// ============================================================================
//  LOAD & RUN ROM
// ============================================================================
bool loadROM(const char *path) {
    const char *actualPath = path;
    char extractedPath[260] = {0};

    // Handle ZIP files: extract first file to temp location
    if (zip_is_zip_file(path)) {
        char innerName[128];
        size_t extractedSize = 0;
        uint8_t *data = zip_extract_first(path, &extractedSize, innerName, sizeof(innerName));
        if (!data || extractedSize == 0) {
            DBG_ERROR("ZIP", "Failed to extract from %s", path);
            if (data) free(data);
            return false;
        }
        // Write extracted data to temp file
        snprintf(extractedPath, sizeof(extractedPath), "/retro-go/temp/%s", innerName);
        if (!SD.exists("/retro-go/temp")) SD.mkdir("/retro-go/temp");
        File tf = SD.open(extractedPath, FILE_WRITE);
        if (!tf) {
            free(data);
            return false;
        }
        tf.write(data, extractedSize);
        tf.close();
        free(data);
        actualPath = extractedPath;
        DBG_INFO("ZIP", "Extracted: %s (%u bytes)", innerName, (unsigned)extractedSize);
    }

    emu_type_t emu = getEmuType(actualPath);
    DBG_INFO("ROM", "Loading: %s (type: %s)", actualPath, emuName(emu));
    int ret = -1;

    // Shutdown previous
    switch(currentEmu) {
        case EMU_NES:  nes_bridge_shutdown(); break;
        case EMU_GB:   gb_bridge_shutdown(); break;
        case EMU_SMS: case EMU_GG: sms_bridge_shutdown(); break;
        case EMU_PCE:  pce_bridge_shutdown(); break;
        case EMU_SNES: snes_bridge_shutdown(); break;
        case EMU_LYNX: lynx_bridge_shutdown(); break;
        case EMU_GW:   gw_bridge_shutdown(); break;
        case EMU_SG1000: case EMU_COLECO: sms_bridge_shutdown(); break;
        case EMU_GENESIS: genesis_bridge_shutdown(); break;
        case EMU_DOOM: doom_bridge_shutdown(); break;
        case EMU_MSX:  msx_bridge_shutdown(); break;
        default: break;
    }
    currentEmu = EMU_NONE;
    audioWritePos = audioReadPos = 0; // Clear audio ring

    switch(emu) {
        case EMU_NES:
            if(!nes_bridge_init(NES_AUDIO_SAMPLE_RATE)) return false;
            ret = nes_bridge_load_rom(actualPath);
            break;
        case EMU_GB: {
            if(!gb_bridge_init(GB_AUDIO_SAMPLE_RATE)) return false;
            ret = gb_bridge_load_rom(actualPath);
            if (ret) {
                // Auto-load SRAM (.sav file)
                char savPath[260];
                strncpy(savPath, actualPath, sizeof(savPath)-5);
                char *dot = strrchr(savPath, '.');
                if (dot) strcpy(dot, ".sav");
                else strcat(savPath, ".sav");
                gb_bridge_load_sram(savPath);
            }
            break;
        }
        case EMU_SMS:
        case EMU_GG:
            if(!sms_bridge_init(SMS_AUDIO_SAMPLE_RATE)) return false;
            ret = sms_bridge_load_rom(actualPath);
            break;
        case EMU_PCE:
            if(!pce_bridge_init(PCE_AUDIO_SAMPLE_RATE)) return false;
            ret = pce_bridge_load_rom(actualPath);
            break;
        case EMU_SNES:
            if(!snes_bridge_init(SNES_AUDIO_SAMPLE_RATE)) return false;
            ret = snes_bridge_load_rom(actualPath);
            break;
        case EMU_LYNX:
            if(!lynx_bridge_init(LYNX_AUDIO_SAMPLE_RATE)) return false;
            ret = lynx_bridge_load_rom(actualPath);
            break;
        case EMU_GW:
            if(!gw_bridge_init()) return false;
            ret = gw_bridge_load_rom(actualPath);
            break;
        case EMU_SG1000:
        case EMU_COLECO:
            if(!sms_bridge_init(SMS_AUDIO_SAMPLE_RATE)) return false;
            ret = sms_bridge_load_rom(actualPath);
            break;
        case EMU_GENESIS:
            if(!genesis_bridge_init(GENESIS_AUDIO_SAMPLE_RATE)) return false;
            ret = genesis_bridge_load_rom(actualPath);
            break;
        case EMU_DOOM:
            if(!doom_bridge_init()) return false;
            ret = doom_bridge_load_wad(path);
            break;
        case EMU_MSX:
            if(!msx_bridge_init(MSX_AUDIO_SAMPLE_RATE)) return false;
            ret = msx_bridge_load_rom(actualPath) ? 0 : -1;
            break;
        default:
            return false;
    }

    if (ret != 0) {
        DBG_ERROR("ROM", "Load failed (ret=%d): %s", ret, actualPath);
        return false;
    }
    currentEmu = emu;
    strncpy(currentRomPath, path, sizeof(currentRomPath)-1);
    turboMode = false;
    frameCounter = 0;
    DBG_INFO("EMU", "Running: %s (%s)", path, emuName(emu));
    dbg_print_memory();
    return true;
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(300);
    DBG_INFO("Boot", "=== RETRO-GAMER v1.0 ===");
    DBG_INFO("Boot", "PSRAM: %d KB", (int)(ESP.getPsramSize()/1024));
    DBG_INFO("Boot", "CPU freq: %d MHz", (int)ESP.getCpuFreqMHz());
    DBG_INFO("Boot", "Flash: %d KB", (int)(ESP.getFlashChipSize()/1024));
    dbg_print_memory();

    if (!ESP.getPsramSize()) {
        DBG_ERROR("Boot", "PSRAM not found! Halting.");
        while(1) delay(1000);
    }

    DBG_INFO("Boot", "Initializing VGA...");
    initVGA();
    DBG_INFO("Boot", "Initializing PS/2...");
    initPS2();
    DBG_INFO("Boot", "Initializing SD card...");
    bool sdOk = initSD();
    DBG_INFO("Boot", "SD card: %s", sdOk ? "OK" : "FAILED");

    buildNESLUT();
    initAudio();

    drawBoot();

    if (sdOk) {
        DBG_INFO("Boot", "Scanning ROMs...");
        scanAllRoms();
        DBG_INFO("Boot", "Found %d ROM(s)", romCount);
        loadFavorites();
        loadRecent();
        wifi_manager_init(); // Load wifi.json config (won't connect yet)
    }

    dbg_print_memory();
    DBG_INFO("Boot", "Initialization complete. Entering launcher.");

    appState = STATE_FILE_SELECT;
    drawMenu();
}

// ============================================================================
//  MAIN LOOP
// ============================================================================
void loop() {
    // Process WiFi HTTP requests if server is running
    wifi_manager_process();

    switch (appState) {

    // ---- FILE SELECTION ----
    case STATE_FILE_SELECT: {
        static unsigned long lastInput = 0;
        unsigned long now = millis();
        if (!Kbd || now - lastInput < 150) { delay(10); break; }

        bool redraw = false;

        if (Kbd->isVKDown(fabgl::VK_UP) && selectedRom > 0) {
            selectedRom--;
            if (selectedRom < scrollOffset) scrollOffset = selectedRom;
            redraw = true; lastInput = now;
        }
        if (Kbd->isVKDown(fabgl::VK_DOWN) && selectedRom < romCount-1) {
            selectedRom++;
            if (selectedRom >= scrollOffset+12) scrollOffset = selectedRom-11;
            redraw = true; lastInput = now;
        }
        if (Kbd->isVKDown(fabgl::VK_RETURN) && romCount > 0) {
            // Loading screen
            auto cv = new fabgl::Canvas(&DisplayController);
            cv->setBrushColor(fabgl::Color::Black); cv->clear();
            cv->setPenColor(fabgl::Color::BrightYellow);
            const char *en = emuName(getEmuType(romFiles[selectedRom]));
            cv->drawTextFmt(60, 110, "Loading %s...", en);
            delete cv;

            if (loadROM(romFiles[selectedRom])) {
                addToRecent(romFiles[selectedRom]);
                appState = STATE_EMULATING;
            } else {
                auto c = new fabgl::Canvas(&DisplayController);
                c->setPenColor(fabgl::Color::BrightRed);
                c->drawText(80, 130, "Load failed!");
                delete c;
                delay(2000);
                redraw = true;
            }
            lastInput = now;
        }
        if (Kbd->isVKDown(fabgl::VK_ESCAPE)) {
            scanAllRoms(); redraw = true; lastInput = now;
        }
        // F = toggle favorite
        if (Kbd->isVKDown(fabgl::VK_f) && romCount > 0 && selectedRom < romCount) {
            toggleFavorite(romFiles[selectedRom]);
            redraw = true; lastInput = now;
        }
        // Tab = cycle view mode
        if (Kbd->isVKDown(fabgl::VK_TAB)) {
            viewMode = (view_mode_t)((viewMode + 1) % VIEW_COUNT);
            selectedRom = 0; scrollOffset = 0;
            redraw = true; lastInput = now;
        }
        // S = cycle scaling
        if (Kbd->isVKDown(fabgl::VK_s) && !Kbd->isVKDown(fabgl::VK_DOWN)) {
            scaleMode = (scale_mode_t)((scaleMode + 1) % SCALE_MODE_COUNT);
            redraw = true; lastInput = now;
        }
        // W = WiFi toggle
        if (Kbd->isVKDown(fabgl::VK_w)) {
            lastInput = now;
            auto cv = new fabgl::Canvas(&DisplayController);
            cv->setBrushColor(fabgl::Color::Black);
            cv->fillRectangle(60, 100, 260, 140);
            cv->setPenColor(fabgl::Color::BrightYellow);

            if (wifi_manager_is_connected()) {
                cv->drawText(70, 110, "Disconnecting WiFi...");
                delete cv;
                wifi_manager_disconnect();
            } else if (wifi_manager_get_network_count() > 0) {
                cv->drawTextFmt(70, 110, "Connecting to %s...", wifi_manager_get_ssid(0));
                delete cv;
                if (wifi_manager_connect(0)) {
                    wifi_manager_start_server();
                    wifi_manager_ntp_sync("CET-1CEST,M3.5.0,M10.5.0/3"); // Turkey: TRT
                    auto c2 = new fabgl::Canvas(&DisplayController);
                    c2->setBrushColor(fabgl::Color::Black);
                    c2->fillRectangle(60, 100, 260, 140);
                    c2->setPenColor(fabgl::Color::BrightGreen);
                    c2->drawTextFmt(70, 110, "WiFi OK! %s", wifi_manager_get_ip());
                    c2->drawText(70, 125, "http:// above IP");
                    delete c2;
                    delay(2000);
                } else {
                    auto c2 = new fabgl::Canvas(&DisplayController);
                    c2->setPenColor(fabgl::Color::BrightRed);
                    c2->drawText(70, 110, "WiFi connection failed!");
                    delete c2;
                    delay(1500);
                }
            } else {
                cv->drawText(70, 110, "No wifi.json config!");
                cv->drawText(70, 125, "Put in /retro-go/config/");
                delete cv;
                delay(1500);
            }
            redraw = true;
        }
        if (redraw) drawMenu();
        delay(16);
        break;
    }

    // ---- EMULATION ----
    case STATE_EMULATING: {
        uint32_t btns = readButtons();
        bool shouldRender = !turboMode || (frameCounter % turboFrameSkip == 0);

        switch (currentEmu) {
            case EMU_NES: {
                nes_bridge_set_input(toNES(btns));
                uint8_t *fb = nes_bridge_run_frame(shouldRender);
                if (shouldRender) blitNES(fb);
                // Audio
                int nSamples = 0;
                int16_t *aBuf = nes_bridge_get_audio(&nSamples);
                if (shouldRender && aBuf && nSamples > 0) audioFeedSamples(aBuf, nSamples);
                break;
            }
            case EMU_GB: {
                gb_bridge_set_input(toGB(btns));
                gb_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    uint16_t *fb = gb_bridge_get_framebuffer();
                    blitGB(fb);
                    // Audio
                    int nSamples = 0;
                    int16_t *aBuf = gb_bridge_get_audio(&nSamples);
                    if (aBuf && nSamples > 0) {
                        for (int i = 0; i < nSamples && ((audioWritePos+1)%AUDIO_RING_SIZE)!=audioReadPos; i++) {
                            audioRing[audioWritePos] = aBuf[i * 2];
                            audioWritePos = (audioWritePos + 1) % AUDIO_RING_SIZE;
                        }
                    }
                }
                // SRAM auto-save: every ~300 frames (~5 seconds)
                static int gbSramTimer = 0;
                if (++gbSramTimer >= 300) {
                    gbSramTimer = 0;
                    if (gb_bridge_sram_dirty()) {
                        char savPath[260];
                        strncpy(savPath, currentRomPath, sizeof(savPath)-5);
                        char *dot = strrchr(savPath, '.');
                        if (dot) strcpy(dot, ".sav");
                        else strcat(savPath, ".sav");
                        gb_bridge_save_sram(savPath);
                    }
                }
                break;
            }
            case EMU_SMS:
            case EMU_GG: {
                sms_bridge_set_input(toSMS(btns));
                sms_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    int w, h;
                    uint8_t *fb = sms_bridge_get_framebuffer(&w, &h);
                    uint16_t *pal = sms_bridge_get_palette();
                    blitSMS(fb, pal, w, h);
                    int16_t *aL, *aR; int nSamples;
                    sms_bridge_get_audio(&aL, &aR, &nSamples);
                    if (aL && aR && nSamples > 0) audioFeedStereoMixed(aL, aR, nSamples);
                }
                break;
            }
            case EMU_PCE: {
                pce_bridge_set_input(toPCE(btns));
                pce_bridge_run_frame();
                if (shouldRender) {
                    int w, h;
                    uint8_t *fb = pce_bridge_get_framebuffer(&w, &h);
                    uint16_t *pal = pce_bridge_get_palette();
                    blitPCE(fb, pal, w, h);
                }
                break;
            }
            case EMU_SNES: {
                snes_bridge_set_input(toSNES(btns));
                snes_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    int w, h;
                    uint16_t *fb = snes_bridge_get_framebuffer(&w, &h);
                    blitSNES(fb, w, h);
                    int nSamples = 0;
                    int16_t *aBuf = snes_bridge_get_audio(&nSamples);
                    if (aBuf && nSamples > 0) audioFeedSamples(aBuf, nSamples);
                }
                break;
            }
            case EMU_LYNX: {
                lynx_bridge_set_input(toLynx(btns));
                lynx_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    uint16_t *fb = lynx_bridge_get_framebuffer();
                    blitLynx(fb);
                    int nSamples = 0;
                    int16_t *aBuf = lynx_bridge_get_audio(&nSamples);
                    if (aBuf && nSamples > 0) audioFeedSamples(aBuf, nSamples);
                }
                break;
            }
            case EMU_GW: {
                gw_bridge_set_input(toGW(btns));
                gw_bridge_run_frame();
                if (shouldRender) {
                    uint16_t *fb = gw_bridge_get_framebuffer();
                    blitGW(fb);
                }
                break;
            }
            case EMU_SG1000:
            case EMU_COLECO: {
                sms_bridge_set_input(toSMS(btns));
                sms_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    int w, h;
                    uint8_t *fb = sms_bridge_get_framebuffer(&w, &h);
                    uint16_t *pal = sms_bridge_get_palette();
                    blitSMS(fb, pal, w, h);
                    int16_t *aL, *aR; int nSamples;
                    sms_bridge_get_audio(&aL, &aR, &nSamples);
                    if (aL && aR && nSamples > 0) audioFeedStereoMixed(aL, aR, nSamples);
                }
                break;
            }
            case EMU_GENESIS: {
                genesis_bridge_set_input(toGenesis(btns));
                genesis_bridge_run_frame(shouldRender);
                if (shouldRender) {
                    int w, h;
                    uint8_t *fb = genesis_bridge_get_framebuffer(&w, &h);
                    uint16_t *pal = genesis_bridge_get_palette();
                    blitPCE(fb, pal, w, h);
                    int nSamples = 0;
                    int16_t *aBuf = genesis_bridge_get_audio(&nSamples);
                    if (aBuf && nSamples > 0) audioFeedSamples(aBuf, nSamples);
                }
                break;
            }
            default: break;
        }

        // Turbo: skip rendering every N frames
        frameCounter++;

        // FPS counter for debug OSD
#if DEBUG_VGA_OSD
        static unsigned long _dbgLastFpsTick = 0;
        static int _dbgFrameCount = 0;
        static int _dbgFps = 0;
        _dbgFrameCount++;
        unsigned long _dbgNow = millis();
        if (_dbgNow - _dbgLastFpsTick >= 1000) {
            _dbgFps = _dbgFrameCount;
            _dbgFrameCount = 0;
            _dbgLastFpsTick = _dbgNow;
        }
        // Draw OSD overlay if visible
        if (dbg_osd_is_visible() && shouldRender) {
            dbg_osd_update_stats(_dbgFps, emuName(currentEmu));
            dbg_osd_set_line(2, "Frame:%-8lu Turbo:%s", frameCounter, turboMode?"ON":"OFF");
            fabgl::Canvas cv(&DisplayController);
            dbg_osd_draw(&cv);
        }
#endif

        // Tab → toggle turbo
        if (Kbd && Kbd->isVKDown(fabgl::VK_TAB)) {
            turboMode = !turboMode;
            DBG_INFO("Input", "Turbo: %s", turboMode ? "ON" : "OFF");
            delay(250);
        }

        // F12 → toggle debug OSD overlay
        if (Kbd && Kbd->isVKDown(fabgl::VK_F12)) {
            dbg_osd_toggle();
            delay(250);
        }

        // ESC → open in-game menu (not quit directly)
        if (Kbd && Kbd->isVKDown(fabgl::VK_ESCAPE)) {
            delay(200);
            igMenuSel = 0;
            appState = STATE_INGAME_MENU;
            drawInGameMenu();
        }
        break;
    }

    // ---- IN-GAME MENU ----
    case STATE_INGAME_MENU: {
        static unsigned long lastMenuInput = 0;
        unsigned long now = millis();
        if (!Kbd || now - lastMenuInput < 180) { delay(10); break; }

        bool redrawIG = false;

        if (Kbd->isVKDown(fabgl::VK_UP)) {
            igMenuSel = (igMenuSel - 1 + IGMENU_COUNT) % IGMENU_COUNT;
            redrawIG = true; lastMenuInput = now;
        }
        if (Kbd->isVKDown(fabgl::VK_DOWN)) {
            igMenuSel = (igMenuSel + 1) % IGMENU_COUNT;
            redrawIG = true; lastMenuInput = now;
        }
        // Left/Right — context-dependent
        if (Kbd->isVKDown(fabgl::VK_LEFT)) {
            if (igMenuSel == IGMENU_SAVE_STATE || igMenuSel == IGMENU_LOAD_STATE) {
                saveSlot = (saveSlot - 1 + 4) % 4;
            } else if (igMenuSel == IGMENU_PALETTE) {
                if (currentEmu == EMU_GB) {
                    int p = (gb_bridge_get_palette() - 1 + gb_bridge_get_palette_count()) % gb_bridge_get_palette_count();
                    gb_bridge_set_palette(p);
                } else if (currentEmu == EMU_NES) {
                    nesCurrentPalette = (nesCurrentPalette - 1 + NES_PALETTE_COUNT) % NES_PALETTE_COUNT;
                    buildNESLUT();
                }
            } else if (igMenuSel == IGMENU_SCALE) {
                scaleMode = (scale_mode_t)((scaleMode - 1 + SCALE_MODE_COUNT) % SCALE_MODE_COUNT);
            }
            redrawIG = true; lastMenuInput = now;
        }
        if (Kbd->isVKDown(fabgl::VK_RIGHT)) {
            if (igMenuSel == IGMENU_SAVE_STATE || igMenuSel == IGMENU_LOAD_STATE) {
                saveSlot = (saveSlot + 1) % 4;
            } else if (igMenuSel == IGMENU_PALETTE) {
                if (currentEmu == EMU_GB) {
                    int p = (gb_bridge_get_palette() + 1) % gb_bridge_get_palette_count();
                    gb_bridge_set_palette(p);
                } else if (currentEmu == EMU_NES) {
                    nesCurrentPalette = (nesCurrentPalette + 1) % NES_PALETTE_COUNT;
                    buildNESLUT();
                }
            } else if (igMenuSel == IGMENU_SCALE) {
                scaleMode = (scale_mode_t)((scaleMode + 1) % SCALE_MODE_COUNT);
            }
            redrawIG = true; lastMenuInput = now;
        }

        // ESC → resume directly
        if (Kbd->isVKDown(fabgl::VK_ESCAPE)) {
            appState = STATE_EMULATING;
            lastMenuInput = now;
            break;
        }

        // Enter → execute selected item
        if (Kbd->isVKDown(fabgl::VK_RETURN)) {
            lastMenuInput = now;

            switch((igmenu_item_t)igMenuSel) {
                case IGMENU_RESUME:
                    appState = STATE_EMULATING;
                    break;

                case IGMENU_SAVE_STATE: {
                    auto cv = new fabgl::Canvas(&DisplayController);
                    cv->setBrushColor(fabgl::Color::Black);
                    cv->fillRectangle(90, 110, 250, 130);
                    cv->setPenColor(fabgl::Color::BrightYellow);
                    cv->drawText(100, 115, "Saving...");
                    delete cv;

                    bool ok = saveStateToSD(saveSlot);

                    cv = new fabgl::Canvas(&DisplayController);
                    cv->setBrushColor(fabgl::Color::Black);
                    cv->fillRectangle(90, 110, 250, 130);
                    cv->setPenColor(ok ? fabgl::Color::BrightGreen : fabgl::Color::BrightRed);
                    cv->drawText(100, 115, ok ? "Saved OK!" : "Save FAILED!");
                    delete cv;
                    delay(1000);
                    redrawIG = true;
                    break;
                }

                case IGMENU_LOAD_STATE: {
                    auto cv = new fabgl::Canvas(&DisplayController);
                    cv->setBrushColor(fabgl::Color::Black);
                    cv->fillRectangle(90, 110, 250, 130);
                    cv->setPenColor(fabgl::Color::BrightYellow);
                    cv->drawText(100, 115, "Loading...");
                    delete cv;

                    bool ok = loadStateFromSD(saveSlot);

                    cv = new fabgl::Canvas(&DisplayController);
                    cv->setBrushColor(fabgl::Color::Black);
                    cv->fillRectangle(90, 110, 250, 130);
                    cv->setPenColor(ok ? fabgl::Color::BrightGreen : fabgl::Color::BrightRed);
                    cv->drawText(100, 115, ok ? "Loaded OK!" : "No save found!");
                    delete cv;
                    delay(1000);

                    if (ok) {
                        appState = STATE_EMULATING;
                    } else {
                        redrawIG = true;
                    }
                    break;
                }

                case IGMENU_TURBO:
                    turboMode = !turboMode;
                    redrawIG = true;
                    break;

                case IGMENU_PALETTE:
                    // Cycle to next palette on Enter
                    if (currentEmu == EMU_GB) {
                        int p = (gb_bridge_get_palette() + 1) % gb_bridge_get_palette_count();
                        gb_bridge_set_palette(p);
                    } else if (currentEmu == EMU_NES) {
                        nesCurrentPalette = (nesCurrentPalette + 1) % NES_PALETTE_COUNT;
                        buildNESLUT();
                    }
                    redrawIG = true;
                    break;

                case IGMENU_SCALE:
                    scaleMode = (scale_mode_t)((scaleMode + 1) % SCALE_MODE_COUNT);
                    redrawIG = true;
                    break;

                case IGMENU_RESET:
                    // Reload the same ROM
                    appState = STATE_EMULATING;
                    // Simple reset: shutdown + reload
                    switch(currentEmu) {
                        case EMU_NES:  nes_bridge_shutdown(); break;
                        case EMU_GB:   gb_bridge_shutdown(); break;
                        case EMU_SMS: case EMU_GG: sms_bridge_shutdown(); break;
                        case EMU_PCE:  pce_bridge_shutdown(); break;
                        case EMU_SNES: snes_bridge_shutdown(); break;
                        case EMU_LYNX: lynx_bridge_shutdown(); break;
                        case EMU_GW:   gw_bridge_shutdown(); break;
                        case EMU_SG1000: case EMU_COLECO: sms_bridge_shutdown(); break;
                        case EMU_GENESIS: genesis_bridge_shutdown(); break;
                        case EMU_DOOM: doom_bridge_shutdown(); break;
        case EMU_MSX:  msx_bridge_shutdown(); break;
                        default: break;
                    }
                    currentEmu = EMU_NONE;
                    loadROM(currentRomPath);
                    break;

                case IGMENU_QUIT: {
                    switch(currentEmu) {
                        case EMU_NES:  nes_bridge_shutdown(); break;
                        case EMU_GB:   gb_bridge_shutdown(); break;
                        case EMU_SMS: case EMU_GG: sms_bridge_shutdown(); break;
                        case EMU_PCE:  pce_bridge_shutdown(); break;
                        case EMU_SNES: snes_bridge_shutdown(); break;
                        case EMU_LYNX: lynx_bridge_shutdown(); break;
                        case EMU_GW:   gw_bridge_shutdown(); break;
                        case EMU_SG1000: case EMU_COLECO: sms_bridge_shutdown(); break;
                        case EMU_GENESIS: genesis_bridge_shutdown(); break;
                        case EMU_DOOM: doom_bridge_shutdown(); break;
        case EMU_MSX:  msx_bridge_shutdown(); break;
                        default: break;
                    }
                    currentEmu = EMU_NONE;
                    appState = STATE_FILE_SELECT;
                    drawMenu();
                    break;
                }

                default: break;
            }
        }

        if (redrawIG) drawInGameMenu();
        delay(16);
        break;
    }

    default: delay(1000); break;
    }
}
