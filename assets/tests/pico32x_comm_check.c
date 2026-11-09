/*
 * pico32x_comm_check.c - headless PicoDrive regression for the dual SH-2 IPC demo.
 *
 * Boots a 32X ROM in picodrive_libretro.so with no display, runs a fixed number
 * of frames, then reads the 32X COMM registers through the core's own 68000 bus
 * (m68k_read16) and asserts:
 *   COMM6 == 0x1F   all five IPC tests passed (see sh2_test.h TEST_MASK_ALL)
 *   COMM7 advances  the Master SH-2 render loop is still alive between samples
 *
 * Build: cc -O2 -o pico32x_comm_check pico32x_comm_check.c -ldl
 * Usage: pico32x_comm_check picodrive_libretro.so rom.32x [frames]
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RETRO_ENV_SET_PIXEL_FORMAT 10
#define COMM6_ADDR        0xA1512C
#define COMM7_ADDR        0xA1512E
#define TEST_MASK_ALL     0x1F
#define DEFAULT_FRAMES    120
#define EXTRA_FRAMES      10

struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };

static bool cb_env(unsigned cmd, void *data) { (void)data; return cmd == RETRO_ENV_SET_PIXEL_FORMAT; }
static void cb_video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void cb_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t cb_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void cb_poll(void) {}
static int16_t cb_input(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }

static void *sym(void *h, const char *name)
{
    void *p = dlsym(h, name);
    if (!p) { fprintf(stderr, "FAIL: missing symbol %s\n", name); exit(2); }
    return p;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s core.so rom.32x [frames]\n", argv[0]); return 2; }
    int frames = argc > 3 ? atoi(argv[3]) : DEFAULT_FRAMES;

    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "FAIL: dlopen: %s\n", dlerror()); return 2; }

    void (*set_env)(bool (*)(unsigned, void *))        = sym(h, "retro_set_environment");
    void (*set_video)(void (*)(const void *, unsigned, unsigned, size_t)) = sym(h, "retro_set_video_refresh");
    void (*set_audio)(void (*)(int16_t, int16_t))      = sym(h, "retro_set_audio_sample");
    void (*set_audio_b)(size_t (*)(const int16_t *, size_t)) = sym(h, "retro_set_audio_sample_batch");
    void (*set_poll)(void (*)(void))                   = sym(h, "retro_set_input_poll");
    void (*set_input)(int16_t (*)(unsigned, unsigned, unsigned, unsigned)) = sym(h, "retro_set_input_state");
    void (*init)(void)                                 = sym(h, "retro_init");
    bool (*load)(const struct retro_game_info *)       = sym(h, "retro_load_game");
    void (*run)(void)                                  = sym(h, "retro_run");
    uint16_t (*read16)(uint32_t)                       = sym(h, "m68k_read16");

    set_env(cb_env); set_video(cb_video); set_audio(cb_audio);
    set_audio_b(cb_audio_batch); set_poll(cb_poll); set_input(cb_input);
    init();

    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *rom = malloc((size_t)size);
    if (fread(rom, 1, (size_t)size, f) != (size_t)size) { perror("read rom"); return 2; }
    fclose(f);

    struct retro_game_info gi = { argv[2], rom, (size_t)size, NULL };
    if (!load(&gi)) { fprintf(stderr, "FAIL: core rejected ROM\n"); return 1; }

    for (int i = 0; i < frames; i++) { run(); }
    uint16_t pass_mask = read16(COMM6_ADDR);
    uint16_t beat_a = read16(COMM7_ADDR);
    for (int i = 0; i < EXTRA_FRAMES; i++) { run(); }
    uint16_t beat_b = read16(COMM7_ADDR);

    printf("COMM6 pass mask = 0x%04X, COMM7 heartbeat %u -> %u\n", pass_mask, beat_a, beat_b);
    printf("diag: COMM0=0x%04X COMM2=0x%04X COMM3=0x%04X COMM4=0x%04X A15100=0x%04X\n",
           read16(0xA15120), read16(0xA15124), read16(0xA15126), read16(0xA15128), read16(0xA15100));
    if (pass_mask != TEST_MASK_ALL) {
        fprintf(stderr, "FAIL: COMM6 is 0x%04X, expected 0x%02X (not every IPC test passed)\n", pass_mask, TEST_MASK_ALL);
        return 1;
    }
    if (beat_a == beat_b) {
        fprintf(stderr, "FAIL: COMM7 stuck at %u; Master SH-2 render loop stopped\n", beat_a);
        return 1;
    }
    printf("PASS: dual SH-2 IPC suite green and Master SH-2 alive\n");
    return 0;
}
