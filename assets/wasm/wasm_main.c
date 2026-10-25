#include <emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/html5.h>
#include <errno.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libretro.h"
#include <pico/pico_int.h>


#define SCREEN_W 320
#define SCREEN_H 224
#define AUDIO_RING_FRAMES 8192
#define AUDIO_RING_MASK (AUDIO_RING_FRAMES - 1)
#define SRAM_SYNC_INTERVAL_FRAMES 120

#define SAVE_DIR "/saves"
#define SRAM_HEX_PATH "/saves/game.srm.hex"
#define QUICKSTATE_HEX_PATH "/saves/quick.state.hex"

#ifndef ROM_VFS_PATH
#define ROM_VFS_PATH "/rom/game.32x"
#endif

#ifndef PROJECT_SLUG
#define PROJECT_SLUG "game"
#endif

/* --- Standard wasm-deploy localStorage Persistence Bridges --- */

EM_JS(void, js_sync_saves_to_local_storage, (const char *project_slug, const char *save_dir), {
    var slug = UTF8ToString(project_slug);
    var dir = UTF8ToString(save_dir);
    try {
        var entries = FS.readdir(dir);
        for (var i = 0; i < entries.length; i++) {
            var name = entries[i];
            if (name === "." || name === "..") continue;
            var fullPath = dir + "/" + name;
            var stat = FS.stat(fullPath);
            if (FS.isFile(stat.mode)) {
                var data = FS.readFile(fullPath, { encoding: "utf8" });
                localStorage.setItem(slug + ":" + fullPath, data);
            }
        }
    } catch (e) {
        console.warn("localStorage save sync failed:", e);
    }
});

EM_JS(void, js_restore_saves_from_local_storage, (const char *project_slug), {
    var slug = UTF8ToString(project_slug);
    var prefix = slug + ":";
    try {
        for (var i = 0; i < localStorage.length; i++) {
            var key = localStorage.key(i);
            if (key && key.indexOf(prefix) === 0) {
                var fullPath = key.substring(prefix.length);
                var lastSlash = fullPath.lastIndexOf("/");
                if (lastSlash > 0) {
                    var dir = fullPath.substring(0, lastSlash);
                    try { FS.mkdirTree(dir); } catch (e) {}
                }
                var data = localStorage.getItem(key);
                if (data !== null) {
                    FS.writeFile(fullPath, data);
                }
            }
        }
    } catch (e) {
        console.warn("localStorage restore failed:", e);
    }
});

EM_JS(void, js_present_rgba_frame, (const uint8_t *rgba_ptr, int width, int height), {
    var app = window.SegaApp || window.Game;
    if (app && app.presentFrame) {
        app.presentFrame(rgba_ptr, width, height);
    }
});

EM_JS(void, js_notify_status, (const char *msg), {
    var app = window.SegaApp || window.Game;
    if (app && app.showToast) {
        app.showToast(UTF8ToString(msg));
    }
});

/* --- Frontend State --- */

static uint32_t g_rgba_frame[SCREEN_W * SCREEN_H];
static uint32_t g_rgb565_lut[65536];

/* Stereo int16 ring buffer (interleaved L, R) */
static int16_t g_audio_ring[AUDIO_RING_FRAMES * 2];
static uint32_t g_audio_write_pos = 0;
static uint32_t g_audio_read_pos = 0;

/* 2-player RetroPad bitmasks (indexed by RETRO_DEVICE_ID_JOYPAD_*) */
static uint16_t g_pad_bits[2] = { 0, 0 };

/* SRAM dirty tracking */
static uint32_t g_last_sram_hash = 0;
static uint32_t g_frame_counter = 0;
static bool g_paused = false;

/**
 * @brief Precomputes 65,536-entry RGB565 to 32-bit RGBA lookup table.
 *
 * Maps Sega 32X 16-bit RGB565 high-color framebuffer pixels into little-endian
 * 32-bit RGBA words for hardware zero-copy presentation to HTML5 Canvas.
 */
static void init_rgb565_lut(void)
{
    for (uint32_t c = 0; c < 65536; c++) {
        uint32_t r5 = (c >> 11) & 0x1F;
        uint32_t g6 = (c >> 5) & 0x3F;
        uint32_t b5 = c & 0x1F;
        uint32_t r8 = (r5 << 3) | (r5 >> 2);
        uint32_t g8 = (g6 << 2) | (g6 >> 4);
        uint32_t b8 = (b5 << 3) | (b5 >> 2);
        /* Little-endian ABGR -> RGBA byte order in Uint8ClampedArray */
        g_rgb565_lut[c] = 0xFF000000u | (b8 << 16) | (g8 << 8) | r8;
    }
}

/* --- Hex Serialization Helpers for UTF-8 Safe localStorage Persistence --- */

static const char k_hex_chars[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

/**
 * @brief Converts single hexadecimal character into numerical nibble (0..15).
 *
 * @param c ASCII character.
 * @return 0..15 on valid hex character, -1 on invalid.
 */
static int hex_nibble(char c)
{
    /* Decimal digits 0..9 */
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    /* Lowercase a..f */
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    /* Uppercase A..F */
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

/**
 * @brief Writes raw binary byte buffer to disk as ASCII hexadecimal string.
 *
 * Ensures binary save data and savestates can safely round-trip through
 * browser localStorage without UTF-8 corruption or NULL byte truncation.
 *
 * @param path Destination filesystem path.
 * @param data Source binary data.
 * @param len Byte count.
 * @return true on success, false on allocation or I/O failure.
 */
static bool write_binary_as_hex_file(const char *path, const uint8_t *data, size_t len)
{
    char *hex_buf = (char *)malloc(len * 2 + 1);
    /* Allocation guard */
    if (!hex_buf) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        hex_buf[i * 2] = k_hex_chars[b >> 4];
        hex_buf[i * 2 + 1] = k_hex_chars[b & 0x0F];
    }
    hex_buf[len * 2] = '\0';

    FILE *f = fopen(path, "wb");
    /* File creation guard */
    if (!f) {
        free(hex_buf);
        return false;
    }
    size_t written = fwrite(hex_buf, 1, len * 2, f);
    fclose(f);
    free(hex_buf);
    return written == (len * 2);
}

/**
 * @brief Reads an ASCII hexadecimal file and decodes it back into raw binary bytes.
 *
 * @param path Source filesystem path.
 * @param out_buf Destination binary buffer.
 * @param max_len Maximum capacity of destination buffer.
 * @return Number of binary bytes decoded.
 */
static size_t read_hex_file_to_binary(const char *path, uint8_t *out_buf, size_t max_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long fsize = ftell(f);
    if (fsize <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    char *hex_buf = (char *)malloc((size_t)fsize);
    if (!hex_buf) {
        fclose(f);
        return 0;
    }
    size_t nread = fread(hex_buf, 1, (size_t)fsize, f);
    fclose(f);

    size_t bin_len = nread / 2;
    /* Clamp decoded bytes to destination buffer capacity */
    if (bin_len > max_len) {
        bin_len = max_len;
    }
    for (size_t i = 0; i < bin_len; i++) {
        int hi = hex_nibble(hex_buf[i * 2]);
        int lo = hex_nibble(hex_buf[i * 2 + 1]);
        /* Abort on corrupted hex formatting */
        if (hi < 0 || lo < 0) {
            free(hex_buf);
            return 0;
        }
        out_buf[i] = (uint8_t)((hi << 4) | lo);
    }
    free(hex_buf);
    return bin_len;
}

/**
 * @brief Computes 32-bit FNV-1a hash over memory block.
 *
 * Used for fast dirty-detection of battery-backed SRAM.
 *
 * @param data Memory buffer.
 * @param len Buffer length in bytes.
 * @return 32-bit FNV-1a hash.
 */
static uint32_t fnv1a_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* --- Libretro Callbacks --- */

/**
 * @brief Environment callback handling Libretro core configuration negotiation.
 *
 * Configures RGB565 pixel format, save directories, and default 6-button gamepad mode.
 *
 * @param cmd Libretro environment command ID.
 * @param data Pointer to command-specific payload.
 * @return true if handled/supported, false otherwise.
 */
static bool cb_environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        /* Enforce 16-bit RGB565 pixel format */
        const enum retro_pixel_format *fmt = (const enum retro_pixel_format *)data;
        return fmt && (*fmt == RETRO_PIXEL_FORMAT_RGB565);
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
        /* Direct save files and SRAM to /saves VFS directory */
        const char **dir = (const char **)data;
        *dir = SAVE_DIR;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        /* Configure PicoDrive joypad variables for authentic 6-button Genesis controllers */
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var || !var->key) {
            return false;
        }
        if (strcmp(var->key, "picodrive_input1") == 0 ||
            strcmp(var->key, "picodrive_input2") == 0) {
            var->value = "6 button pad";
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
        bool *updated = (bool *)data;
        if (updated) {
            *updated = false;
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    default:
        return false;
    }
}

/**
 * @brief Video refresh callback receiving emulator video frames from Libretro.
 *
 * Converts 16-bit RGB565 lines to 32-bit RGBA via LUT and presents to JavaScript Canvas.
 *
 * @param data Pointer to frame pixel buffer.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param pitch Byte pitch per scanline.
 */
static void cb_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
    /* Ignore empty frame */
    if (!data) {
        return;
    }
    unsigned copy_w = width < SCREEN_W ? width : SCREEN_W;
    unsigned copy_h = height < SCREEN_H ? height : SCREEN_H;
    const uint8_t *src_row = (const uint8_t *)data;

    for (unsigned y = 0; y < copy_h; y++) {
        const uint16_t *src16 = (const uint16_t *)(src_row + y * pitch);
        uint32_t *dst32 = &g_rgba_frame[y * SCREEN_W];
        for (unsigned x = 0; x < copy_w; x++) {
            dst32[x] = g_rgb565_lut[src16[x]];
        }
    }
    js_present_rgba_frame((const uint8_t *)g_rgba_frame, (int)copy_w, (int)copy_h);
}

/**
 * @brief Audio callback receiving individual stereo PCM samples.
 *
 * Pushes sample pair into ring buffer if space is available.
 *
 * @param left Left channel int16 sample.
 * @param right Right channel int16 sample.
 */
static void cb_audio_sample(int16_t left, int16_t right)
{
    uint32_t used = g_audio_write_pos - g_audio_read_pos;
    /* Drop samples on buffer overrun */
    if (used < AUDIO_RING_FRAMES) {
        uint32_t idx = (g_audio_write_pos & AUDIO_RING_MASK) * 2;
        g_audio_ring[idx] = left;
        g_audio_ring[idx + 1] = right;
        g_audio_write_pos++;
    }
}

/**
 * @brief Batch audio callback receiving interleaved stereo PCM frames.
 *
 * @param data Interleaved int16 stereo samples [L0, R0, L1, R1, ...].
 * @param frames Number of stereo frames.
 * @return Number of frames consumed.
 */
static size_t cb_audio_sample_batch(const int16_t *data, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        uint32_t used = g_audio_write_pos - g_audio_read_pos;
        /* Ring buffer full check */
        if (used >= AUDIO_RING_FRAMES) {
            break;
        }
        uint32_t idx = (g_audio_write_pos & AUDIO_RING_MASK) * 2;
        g_audio_ring[idx] = data[i * 2];
        g_audio_ring[idx + 1] = data[i * 2 + 1];
        g_audio_write_pos++;
    }
    return frames;
}

/**
 * @brief Input poll callback invoked prior to querying controller state.
 */
static void cb_input_poll(void)
{
}

/**
 * @brief Input state callback querying current button press status.
 *
 * @param port Controller port (0 = player 1, 1 = player 2).
 * @param device Device class (RETRO_DEVICE_JOYPAD).
 * @param index Multitap sub-index.
 * @param id Button ID (RETRO_DEVICE_ID_JOYPAD_*).
 * @return 1 if pressed, 0 otherwise.
 */
static int16_t cb_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    /* Validate controller port and joypad button bounds */
    if (port >= 2 || device != RETRO_DEVICE_JOYPAD || id >= 16) {
        return 0;
    }
    return (g_pad_bits[port] & (1u << id)) ? 1 : 0;
}

/* --- SRAM Persistence --- */

static size_t g_sram_capacity = 0;

/**
 * @brief Restores cartridge battery-backed save RAM from persistent browser storage.
 */
/**
 * @brief Restores cartridge battery-backed save RAM from persistent browser storage.
 *
 * Checks if the emulator core has exposed a valid save RAM pointer and non-zero
 * capacity. If so, reads the hex-encoded save file from virtual filesystem into
 * cartridge SRAM and calculates an initial FNV-1a checksum to avoid redundant writes.
 */
static void restore_sram_if_present(void)
{
    uint8_t *sram_ptr = (uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    /* Abort if core does not expose battery RAM or reported capacity is uninitialized */
    if (!sram_ptr || g_sram_capacity == 0) {
        return;
    }
    size_t loaded = read_hex_file_to_binary(SRAM_HEX_PATH, sram_ptr, g_sram_capacity);
    /* When existing persistent save data was loaded into SRAM, update hash to prevent immediate re-sync */
    if (loaded > 0) {
        g_last_sram_hash = fnv1a_hash(sram_ptr, g_sram_capacity);
        js_notify_status("Restored Cartridge SRAM from browser storage");
    }
}

/**
 * @brief Checks if SRAM has been modified and flushes changes to browser localStorage.
 *
 * Computes an FNV-1a hash across the active save RAM region. If modified or forced,
 * writes the data out to virtual filesystem and invokes JavaScript localStorage persistence.
 *
 * @param force If true, synchronizes immediately regardless of dirty hash match.
 */
static void check_and_sync_sram(bool force)
{
    const uint8_t *sram_ptr = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t active_size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    /* Abort if no save RAM buffer is mapped by the active emulation core */
    if (!sram_ptr || active_size == 0) {
        return;
    }
    uint32_t h = fnv1a_hash(sram_ptr, active_size);
    /* Skip expensive disk and localStorage serialization if SRAM contents remain unmodified */
    if (!force && h == g_last_sram_hash) {
        return;
    }
    g_last_sram_hash = h;
    /* Write serialized hex representation to virtual filesystem and trigger JS local storage sync */
    if (write_binary_as_hex_file(SRAM_HEX_PATH, sram_ptr, active_size)) {
        js_sync_saves_to_local_storage(PROJECT_SLUG, SAVE_DIR);
        /* Notify user in HUD only on explicit user-triggered sync requests to avoid spam */
        if (force) {
            js_notify_status("Cartridge SRAM synced to browser storage");
        }
    }
}

/* --- Exported Controls for HTML Shell --- */

/**
 * @brief Sets the bitmask of pressed buttons for a controller port.
 *
 * Invoked from JavaScript gamepad/keyboard input handlers to update controller state in one call.
 *
 * @param port Controller port index (0 for Player 1, 1 for Player 2).
 * @param mask Bitfield representing Libretro joypad button states.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_set_pad_mask(int port, int mask)
{
    /* Enforce bounds check on controller port to prevent out-of-bounds array access */
    if (port >= 0 && port < 2) {
        g_pad_bits[port] = (uint16_t)mask;
    }
}

/**
 * @brief Updates the press/release state of a single joypad button.
 *
 * Modifies the bitfield for the specified port and button ID.
 *
 * @param port Controller port index (0 or 1).
 * @param retro_id Libretro joypad button constant (RETRO_DEVICE_ID_JOYPAD_*).
 * @param pressed 1 if the button is pressed, 0 if released.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_set_button(int port, int retro_id, int pressed)
{
    /* Validate port index and button identifier within standard 16-bit pad mask bounds */
    if (port < 0 || port >= 2 || retro_id < 0 || retro_id >= 16) {
        return;
    }
    /* Set bit on button press; clear bit on release */
    if (pressed) {
        g_pad_bits[port] |= (uint16_t)(1u << retro_id);
    } else {
        g_pad_bits[port] &= (uint16_t)~(1u << retro_id);
    }
}

/**
 * @brief Pulls decoded 32-bit floating point audio samples from the internal ring buffer.
 *
 * Invoked by the Web Audio API ScriptProcessorNode or AudioWorklet to drain available PCM audio.
 *
 * @param out_left Destination buffer for left channel float samples [-1.0, 1.0].
 * @param out_right Destination buffer for right channel float samples [-1.0, 1.0].
 * @param max_frames Maximum number of stereo frames to extract.
 * @return Number of stereo frames actually extracted.
 */
EMSCRIPTEN_KEEPALIVE
int wasm_pull_audio_f32(float *out_left, float *out_right, int max_frames)
{
    int count = 0;
    const float inv_scale = 1.0f / 32768.0f;
    /* Drain ring buffer up to the requested maximum frame limit while samples remain */
    while (count < max_frames && g_audio_read_pos != g_audio_write_pos) {
        uint32_t idx = (g_audio_read_pos & AUDIO_RING_MASK) * 2;
        out_left[count] = (float)g_audio_ring[idx] * inv_scale;
        out_right[count] = (float)g_audio_ring[idx + 1] * inv_scale;
        g_audio_read_pos++;
        count++;
    }
    return count;
}

/**
 * @brief Sets the emulation pause state.
 *
 * When paused, the main animation loop skips core stepping and resets clock delta timers.
 *
 * @param paused Non-zero to pause emulation, zero to resume.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_set_paused(int paused)
{
    g_paused = (paused != 0);
}

/**
 * @brief Triggers a hardware reset of the simulated 32X / Genesis console.
 *
 * Flushes dirty SRAM to prevent data loss before signaling the core reset.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_reset_cartridge(void)
{
    check_and_sync_sram(false);
    retro_reset();
    js_notify_status("32X Hardware Reset");
}

/**
 * @brief Serializes the complete 32X emulator state and stores it in browser localStorage.
 *
 * Queries state size, serializes to binary buffer, encodes as hex file, and synchronizes.
 *
 * @return 1 on success, 0 on failure.
 */
EMSCRIPTEN_KEEPALIVE
int wasm_save_quick_state(void)
{
    size_t sz = retro_serialize_size();
    /* Abort if core does not support serialization / savestates */
    if (sz == 0) {
        js_notify_status("Savestates unsupported by core");
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc(sz);
    /* Fail cleanly if memory allocation for savestate buffer fails */
    if (!buf) {
        return 0;
    }
    bool ok = retro_serialize(buf, sz);
    /* If serialization succeeded, write hex file to virtual storage */
    if (ok) {
        ok = write_binary_as_hex_file(QUICKSTATE_HEX_PATH, buf, sz);
    }
    free(buf);
    /* Commit persistent storage and inform user if quickstate write was successful */
    if (ok) {
        check_and_sync_sram(false);
        js_sync_saves_to_local_storage(PROJECT_SLUG, SAVE_DIR);
        js_notify_status("Saved State to localStorage");
        return 1;
    }
    js_notify_status("Failed to save 32X state");
    return 0;
}

/**
 * @brief Unserializes emulator state from quickstate file stored in browser localStorage.
 *
 * Restores 68000, dual SH-2, VDP, and PWM audio states to the saved snapshot.
 *
 * @return 1 on success, 0 on failure.
 */
EMSCRIPTEN_KEEPALIVE
int wasm_load_quick_state(void)
{
    size_t sz = retro_serialize_size();
    /* Abort if core does not support deserialization */
    if (sz == 0) {
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc(sz);
    /* Check memory allocation for state load buffer */
    if (!buf) {
        return 0;
    }
    size_t loaded = read_hex_file_to_binary(QUICKSTATE_HEX_PATH, buf, sz);
    bool ok = false;
    /* If state file was present and read successfully, unserialize into running core */
    if (loaded > 0) {
        ok = retro_unserialize(buf, loaded);
    }
    free(buf);
    /* Update status HUD based on whether state restore succeeded */
    if (ok) {
        js_notify_status("Loaded 32X State from localStorage");
        return 1;
    }
    js_notify_status("No saved state found in localStorage");
    return 0;
}

/**
 * @brief Forces immediate synchronization of cartridge SRAM to browser localStorage.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_sync_sram_now(void)
{
    check_and_sync_sram(true);
}

/* --- Hardware Telemetry & Emulator Diagnostics Inspector (Phase 23) --- */

#define TELEMETRY_BUFFER_SIZE 36
static uint32_t g_telemetry_buf[TELEMETRY_BUFFER_SIZE];
static uint32_t g_core_exec_time_us = 0;

/**
 * @brief Updates internal diagnostic counters and returns direct pointer to telemetry buffer.
 *
 * Populates real-time memory allocation metrics, dual SH-2 and 68000 CPU states,
 * inter-CPU bus communication registers (COMM0..COMM14), and emulation performance metrics.
 * Preconditions: Core and memory structures are initialized after retro_init() and retro_load_game().
 *
 * @return Direct pointer to static 32-bit uint32 array accessible via Module.HEAPU32.
 */
EMSCRIPTEN_KEEPALIVE
uint32_t *wasm_get_telemetry_ptr(void)
{
    /* [0] Total WebAssembly virtual memory capacity allocated by runtime in bytes */
    g_telemetry_buf[0] = (uint32_t)emscripten_get_heap_size();

    /* [1] Current break watermark indicating heap memory in active use */
    void *cur_brk = sbrk(0);
    g_telemetry_buf[1] = (uint32_t)((uintptr_t)cur_brk);

    /* [2] Sega 32X SDRAM capacity (256 KiB fixed hardware memory) */
    g_telemetry_buf[2] = 0x40000;

    /* [3] Sega 32X Framebuffer DRAM total capacity (2 x 128 KiB display/draw banks) */
    g_telemetry_buf[3] = 0x40000;

    /* [4] Active 32X Framebuffer state: pending_fb flag and VDP display mode */
    g_telemetry_buf[4] = (uint32_t)Pico32x.pending_fb;

    /* [5] Motorola 68000 Main Work RAM capacity (64 KiB fixed memory) */
    g_telemetry_buf[5] = 0x10000;

    /* [6] Genesis VDP Video RAM capacity (64 KiB) */
    g_telemetry_buf[6] = 0x10000;

    /* [7] Z80 Audio Coprocessor RAM capacity (8 KiB) */
    g_telemetry_buf[7] = 0x2000;

    /* [8] Cartridge Battery SRAM capacity */
    g_telemetry_buf[8] = (uint32_t)g_sram_capacity;

    /* [9] 32-bit FNV-1a hash of active SRAM (detects dirty modifications) */
    g_telemetry_buf[9] = g_last_sram_hash;

    /* [10] Cartridge ROM total capacity in bytes */
    g_telemetry_buf[10] = (uint32_t)Pico.romsize;

    /* [11] Master Hitachi SH-2 Program Counter */
    g_telemetry_buf[11] = msh2.pc;

    /* [12] Master Hitachi SH-2 Execution State (RUN=1, SLEEP=2, CPOLL=4, VPOLL=8, RPOLL=16) */
    g_telemetry_buf[12] = msh2.state;

    /* [13] Master Hitachi SH-2 Polling Cycles Accumulator */
    g_telemetry_buf[13] = msh2.poll_cycles;

    /* [14] Master Hitachi SH-2 Polling Count */
    g_telemetry_buf[14] = (uint32_t)msh2.poll_cnt;

    /* [15] Slave Hitachi SH-2 Program Counter */
    g_telemetry_buf[15] = ssh2.pc;

    /* [16] Slave Hitachi SH-2 Execution State */
    g_telemetry_buf[16] = ssh2.state;

    /* [17] Slave Hitachi SH-2 Polling Cycles Accumulator */
    g_telemetry_buf[17] = ssh2.poll_cycles;

    /* [18] Slave Hitachi SH-2 Polling Count */
    g_telemetry_buf[18] = (uint32_t)ssh2.poll_cnt;

    /* [19] VDP Scanline (0..261) and Z80 running status flag */
    g_telemetry_buf[19] = ((uint32_t)Pico.m.z80Run << 16) | (uint32_t)Pico.m.scanline;


    /* [20..27] 32X Inter-CPU Communication Registers COMM0..COMM14 (Pico32x.regs[0x10..0x17]) */
    for (int i = 0; i < 8; i++) {
        g_telemetry_buf[20 + i] = (uint32_t)Pico32x.regs[0x10 + i];
    }

    /* [28] Rolling average core emulation frame time in microseconds */
    g_telemetry_buf[28] = g_core_exec_time_us;

    /* [29] Audio ring buffer current occupancy in stereo frames */
    uint32_t queued_audio = (g_audio_write_pos - g_audio_read_pos) & AUDIO_RING_MASK;
    g_telemetry_buf[29] = queued_audio;

    /* [30] Audio ring buffer maximum capacity in stereo frames */
    g_telemetry_buf[30] = AUDIO_RING_FRAMES;

    /* [31] Total simulated frames stepped since cartridge boot */
    g_telemetry_buf[31] = g_frame_counter;

    /* [32] 32X VDP Control Register (display mode & resolution flags) */
    g_telemetry_buf[32] = (uint32_t)Pico32x.vdp_regs[0];

    /* [33] Master SH-2 Status Register (SR) */
    g_telemetry_buf[33] = msh2.sr;

    /* [34] Slave SH-2 Status Register (SR) */
    g_telemetry_buf[34] = ssh2.sr;

    /* [35] Emulation paused flag */
    if (g_paused) {
        /* Record paused state when user or HUD halts emulation */
        g_telemetry_buf[35] = 1;
    } else {
        /* Record active running state */
        g_telemetry_buf[35] = 0;
    }

    return g_telemetry_buf;
}

/**
 * @brief Copies diagnostic telemetry metrics into caller-allocated destination buffer.
 *
 * Preconditions: out_buf must point to valid memory with capacity of at least max_entries uint32_t.
 *
 * @param out_buf Destination 32-bit integer array.
 * @param max_entries Maximum entries to copy.
 * @return Number of entries actually copied.
 */
EMSCRIPTEN_KEEPALIVE
int wasm_get_telemetry(uint32_t *out_buf, int max_entries)
{
    /* Validate caller buffer pointer and entry bounds */
    if (!out_buf || max_entries <= 0) {
        /* Return zero on invalid buffer argument to prevent memory faults */
        return 0;
    }
    wasm_get_telemetry_ptr();
    int count = max_entries < TELEMETRY_BUFFER_SIZE ? max_entries : TELEMETRY_BUFFER_SIZE;
    for (int i = 0; i < count; i++) {
        out_buf[i] = g_telemetry_buf[i];
    }
    return count;
}

/* --- Main Loop with 60 Hz NTSC Fixed-Timestep Clock Timing --- */

static double g_last_time_ms = 0.0;
static double g_time_accumulator_ms = 0.0;

/**
 * @brief Periodic main loop callback driven by requestAnimationFrame.
 *
 * Implements a fixed-timestep 60.0 Hz NTSC clock accumulator to step PicoDrive
 * deterministically regardless of the host display's refresh rate (60Hz, 120Hz, 144Hz).
 * Clamps maximum simulated frames per tick to prevent the "spiral of death" during lag spikes.
 */
static void main_tick(void)
{
    /* If emulator is explicitly paused via HUD, freeze accumulator and skip core stepping */
    if (g_paused) {
        g_last_time_ms = 0.0;
        return;
    }

    double now = emscripten_get_now();
    /* First frame initialization: capture current timestamp and wait for next tick */
    if (g_last_time_ms == 0.0) {
        g_last_time_ms = now;
        return;
    }

    double elapsed = now - g_last_time_ms;
    g_last_time_ms = now;

    /* Clamp elapsed time: upper bound prevents catchup spiral on background tab, lower bound guards backward clock */
    if (elapsed > 100.0) {
        elapsed = 100.0;
    } else if (elapsed < 0.0) {
        elapsed = 0.0;
    }

    g_time_accumulator_ms += elapsed;

    const double FRAME_DURATION_MS = 1000.0 / 60.0; /* 60.0 Hz Sega NTSC rate */
    int max_frames_per_tick = 3;

    /* Step the Libretro core for each 16.66ms chunk accumulated, capping at 3 frames per RAF */
    while (g_time_accumulator_ms >= FRAME_DURATION_MS && max_frames_per_tick-- > 0) {
        double t0 = emscripten_get_now();
        retro_run();
        double dt_us = (emscripten_get_now() - t0) * 1000.0;

        /* Track rolling exponential moving average of core frame execution time */
        if (g_core_exec_time_us == 0) {
            /* Seed initial average on first frame */
            g_core_exec_time_us = (uint32_t)dt_us;
        } else {
            /* Smooth with 90/10 weight factor to filter out scheduling spikes */
            g_core_exec_time_us = (uint32_t)(g_core_exec_time_us * 0.9 + dt_us * 0.1);
        }

        g_frame_counter++;
        /* Periodically flush battery SRAM to browser storage every 120 frames (~2 seconds) */
        if ((g_frame_counter % SRAM_SYNC_INTERVAL_FRAMES) == 0) {
            check_and_sync_sram(false);
        }
        g_time_accumulator_ms -= FRAME_DURATION_MS;
    }

    /* Discard excess accumulated time beyond 2 frames to keep latency low */
    if (g_time_accumulator_ms > FRAME_DURATION_MS * 2.0) {
        g_time_accumulator_ms = FRAME_DURATION_MS * 2.0;
    }
}

/**
 * @brief Main entry point for the WebAssembly runtime environment.
 *
 * Prepares the Emscripten virtual filesystem, restores SRAM saves from browser
 * localStorage, initializes video and audio callbacks, loads the embedded 32X ROM,
 * and attaches the 60 Hz animation loop.
 *
 * @return 0 on successful setup, 1 on cartridge load failure.
 */
int main(void)
{
    mkdir(SAVE_DIR, 0777);
    js_restore_saves_from_local_storage(PROJECT_SLUG);
    init_rgb565_lut();

    retro_set_environment(cb_environment);
    retro_set_video_refresh(cb_video_refresh);
    retro_set_audio_sample(cb_audio_sample);
    retro_set_audio_sample_batch(cb_audio_sample_batch);
    retro_set_input_poll(cb_input_poll);
    retro_set_input_state(cb_input_state);

    retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = ROM_VFS_PATH;

    /* Abort execution if PicoDrive fails to mount and parse the 32X cartridge ROM */
    if (!retro_load_game(&info)) {
        js_notify_status("ERROR: Failed to load /rom/game.32x");
        return 1;
    }

    /* Query initial SRAM capacity on frame 0 before PicoDrive hides uninitialized SRAM */
    g_sram_capacity = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    /* Fall back to standard 16 KiB battery SRAM if core reports zero capacity prior to cartridge write */
    if (g_sram_capacity == 0) {
        g_sram_capacity = 0x4000;
    }
    restore_sram_if_present();

    emscripten_set_main_loop(main_tick, 0, 1);
    return 0;
}
