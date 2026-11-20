/* Host-side smoke tests for the statically linked libretro cores.
 *
 * No ROMs ship with this repository, so the tests synthesise minimal ones:
 * a 32 KB Game Boy cartridge (valid header checksum, a jr-to-self loop) and
 * a 16 KB+8 KB iNES image (reset vector into a jmp-to-self loop). Each core
 * must load them, run frames, and restore a save state deterministically.
 *
 * Build and run with `make test` (plain host gcc; the cores do not touch the
 * Aurora SDK).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include "cores/core.h"

static int failures;

/* The device build decides this in video.c. Host tests always draw. */
bool ra_video_will_skip(void) { return false; }

#define CHECK(cond, what)                                                     \
  do {                                                                        \
    if (cond) {                                                               \
      printf("ok   %s\n", what);                                              \
    } else {                                                                  \
      printf("FAIL %s\n", what);                                              \
      failures++;                                                             \
    }                                                                         \
  } while (0)

/* --- frontend stubs -------------------------------------------------------- */

static enum retro_pixel_format seen_fmt = RETRO_PIXEL_FORMAT_UNKNOWN;
static unsigned seen_w, seen_h;
static size_t seen_pitch;
static int video_calls;
static uint32_t frame_hash;

static bool env_cb(unsigned cmd, void *data) {
  if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
    seen_fmt = *(const enum retro_pixel_format *)data;
    return true;
  }
  return false;
}

static void video_cb(const void *data, unsigned width, unsigned height,
                     size_t pitch) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  unsigned y;
  size_t x;

  video_calls++;
  seen_w = width;
  seen_h = height;
  seen_pitch = pitch;
  if (!data)
    return;
  for (y = 0; y < height; y++)
    for (x = 0; x < width * 2; x++) { /* RGB565: 2 bytes per pixel */
      h ^= p[y * pitch + x];
      h *= 16777619u;
    }
  frame_hash = h;
}

static size_t audio_batch_cb(const int16_t *data, size_t frames) {
  (void)data;
  return frames;
}

static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index,
                              unsigned id) {
  (void)port;
  (void)device;
  (void)index;
  (void)id;
  return 0;
}

/* --- synthetic ROMs --------------------------------------------------------- */

static uint8_t *make_gb_rom(size_t *out_size) {
  size_t size = 32 * 1024;
  uint8_t *rom = calloc(1, size);
  uint8_t checksum = 0;
  int i;

  /* Entry point: nop; jp $0150. */
  rom[0x100] = 0x00;
  rom[0x101] = 0xC3;
  rom[0x102] = 0x50;
  rom[0x103] = 0x01;
  memcpy(&rom[0x134], "RETROARCH TEST", 14);
  rom[0x147] = 0x03; /* MBC1 + RAM + battery */
  rom[0x148] = 0x00; /* 32 KB ROM */
  rom[0x149] = 0x02; /* 8 KB cart RAM */
  /* jr -2: spin until the frame ends. */
  rom[0x150] = 0x18;
  rom[0x151] = 0xFE;
  for (i = 0x134; i <= 0x14C; i++)
    checksum = checksum - rom[i] - 1;
  rom[0x14D] = checksum;

  *out_size = size;
  return rom;
}

static uint8_t *make_sms_rom(size_t *out_size) {
  /* 32 KB, no "TMR SEGA" header: auto-detects as an export SMS2. The Z80
   * spins on jp $0000. */
  size_t size = 32 * 1024;
  uint8_t *rom = calloc(1, size);

  rom[0] = 0xC3;
  rom[1] = 0x00;
  rom[2] = 0x00;

  *out_size = size;
  return rom;
}

static uint8_t *make_chip8_rom(size_t *out_size) {
  /* Draw the "0" glyph at (0, 0), then spin: jp $0206. */
  static const uint8_t prog[] = {
    0x60, 0x00,       /* 6000: V0 = 0 */
    0xF0, 0x29,       /* F029: I = glyph for V0 */
    0xD0, 0x05,       /* D005: draw 8x5 at (V0, V0) */
    0x12, 0x06,       /* 1206: jp self */
  };
  uint8_t *rom = malloc(sizeof(prog));
  memcpy(rom, prog, sizeof(prog));
  *out_size = sizeof(prog);
  return rom;
}

static uint8_t *make_nes_rom(size_t *out_size) {
  size_t size = 16 + 16384 + 8192;
  uint8_t *rom = calloc(1, size);
  uint8_t *prg = rom + 16;

  rom[0] = 'N';
  rom[1] = 'E';
  rom[2] = 'S';
  rom[3] = 0x1A;
  rom[4] = 1; /* 16 KB PRG */
  rom[5] = 1; /* 8 KB CHR */

  /* $8000: jmp $8000. NROM mirrors the single bank at $C000 too. */
  prg[0] = 0x4C;
  prg[1] = 0x00;
  prg[2] = 0x80;
  /* Reset vector $FFFC -> $8000. */
  prg[0x3FFC] = 0x00;
  prg[0x3FFD] = 0x80;

  *out_size = size;
  return rom;
}

/* --- per-core exercise ------------------------------------------------------- */

static void run_frames(const ra_core_t *core, int n) {
  int i;
  for (i = 0; i < n; i++)
    core->run();
}

static void exercise(const ra_core_t *core, uint8_t *rom, size_t rom_size,
                     unsigned want_w, unsigned want_h, size_t want_sram) {
  struct retro_game_info game;
  size_t state_size;
  void *state;
  uint32_t hash_a, hash_b;
  char what[128];

  printf("--- %s %s ---\n", core->library_name, core->library_version);

  core->set_callbacks(env_cb, video_cb, audio_batch_cb, input_poll_cb,
                      input_state_cb);
  core->init();

  memset(&game, 0, sizeof(game));
  game.path = "test.rom";
  game.data = rom;
  game.size = rom_size;
  seen_fmt = RETRO_PIXEL_FORMAT_UNKNOWN;
  video_calls = 0;

  CHECK(core->load_game(&game), "load_game");
  CHECK(seen_fmt == RETRO_PIXEL_FORMAT_RGB565, "pixel format is RGB565");

  run_frames(core, 10);
  snprintf(what, sizeof(what), "ran 10 frames (%d video calls)", video_calls);
  CHECK(video_calls == 10, what);
  snprintf(what, sizeof(what), "frame is %ux%u", want_w, want_h);
  CHECK(seen_w == want_w && seen_h == want_h, what);
  CHECK(seen_pitch == want_w * 2, "pitch matches RGB565");

  if (want_sram) {
    void *mem = core->get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t mem_size = core->get_memory_size(RETRO_MEMORY_SAVE_RAM);
    snprintf(what, sizeof(what), "%u KB of cart RAM exposed",
             (unsigned)(want_sram / 1024));
    CHECK(mem != NULL && mem_size == want_sram, what);
  }

  state_size = core->serialize_size();
  CHECK(state_size > 0, "serialize_size > 0");
  state = malloc(state_size);
  CHECK(core->serialize(state, state_size), "serialize");

  run_frames(core, 5);
  hash_a = frame_hash;

  CHECK(core->unserialize(state, state_size), "unserialize");
  run_frames(core, 5);
  hash_b = frame_hash;
  CHECK(hash_a == hash_b, "restored state replays deterministically");

  CHECK(!core->unserialize(state, sizeof(uint32_t)),
        "a truncated state is refused");

  free(state);
  core->unload_game();
  core->deinit();
}

int main(void) {
  uint8_t *rom;
  size_t rom_size;

  CHECK(ra_core_for_path("game.gb") == &ra_core_gb, "routes .gb");
  CHECK(ra_core_for_path("game.DMG") == &ra_core_gb, "routes .DMG");
  CHECK(ra_core_for_path("game.nes") == &ra_core_nes, "routes .nes");
  CHECK(ra_core_for_path("game.sms") == &ra_core_sms, "routes .sms");
  CHECK(ra_core_for_path("game.gg") == &ra_core_sms, "routes .gg");
  CHECK(ra_core_for_path("game.sg") == &ra_core_sms, "routes .sg");
  CHECK(ra_core_for_path("game.col") == &ra_core_sms, "routes .col");
  CHECK(ra_core_for_path("game.ch8") == &ra_core_chip8, "routes .ch8");
  CHECK(ra_core_for_path("game.gba") == NULL, "refuses .gba");
  CHECK(ra_core_for_path("game") == NULL, "refuses no extension");

  rom = make_gb_rom(&rom_size);
  exercise(&ra_core_gb, rom, rom_size, 160, 144, 0x2000);
  free(rom);

  rom = make_nes_rom(&rom_size);
  exercise(&ra_core_nes, rom, rom_size, 256, 240, 0);
  free(rom);

  rom = make_sms_rom(&rom_size);
  exercise(&ra_core_sms, rom, rom_size, 256, 192, 0x8000);
  free(rom);

  rom = make_chip8_rom(&rom_size);
  exercise(&ra_core_chip8, rom, rom_size, 64, 32, 0);
  free(rom);

  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nall tests passed\n");
  return 0;
}
