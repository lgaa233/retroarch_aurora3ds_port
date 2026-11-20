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
#include "ra_nes_apu.h"

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

/* Mono (left) samples, kept while `capture` is set. */
#define CAPTURE_MAX (1u << 17)
static int capture;
static int16_t captured[CAPTURE_MAX];
static size_t captured_n;
static size_t last_batch;

static size_t audio_batch_cb(const int16_t *data, size_t frames) {
  size_t i;
  last_batch = frames;
  if (capture)
    for (i = 0; i < frames && captured_n < CAPTURE_MAX; i++)
      captured[captured_n++] = data[i * 2];
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

/* Plays a 50% pulse at $4002 = 253 (440.4 Hz), full volume, then spins. */
static uint8_t *make_nes_sound_rom(size_t *out_size) {
  static const uint8_t prog[] = {
    0xA9, 0x0F, 0x8D, 0x15, 0x40, /* lda #$0F, sta $4015 */
    0xA9, 0xBF, 0x8D, 0x00, 0x40, /* lda #$BF, sta $4000: duty 2, vol 15 */
    0xA9, 0x08, 0x8D, 0x01, 0x40, /* lda #$08, sta $4001: sweep off */
    0xA9, 0xFD, 0x8D, 0x02, 0x40, /* lda #$FD, sta $4002 */
    0xA9, 0x00, 0x8D, 0x03, 0x40, /* lda #$00, sta $4003 */
    0x4C, 0x19, 0x80,             /* jmp $8019 */
  };
  size_t size;
  uint8_t *rom = make_nes_rom(&size);
  memcpy(rom + 16, prog, sizeof(prog));
  *out_size = size;
  return rom;
}

static void run_frames(const ra_core_t *core, int n) {
  int i;
  for (i = 0; i < n; i++)
    core->run();
}

/* --- sound measurements ------------------------------------------------------ */

typedef struct {
  double hz;   /* from rising crossings of the mean */
  int p2p;     /* peak to peak */
  double mean;
} wave_t;

static wave_t measure(const int16_t *s, size_t n, double rate) {
  wave_t w;
  size_t i, first = 0, last = 0;
  int lo = 32767, hi = -32768, crossings = 0;
  double sum = 0;
  for (i = 0; i < n; i++) {
    sum += s[i];
    if (s[i] < lo) lo = s[i];
    if (s[i] > hi) hi = s[i];
  }
  w.mean = n ? sum / (double)n : 0;
  w.p2p = hi - lo;
  for (i = 1; i < n; i++) {
    if (s[i - 1] < w.mean && s[i] >= w.mean) {
      if (!crossings)
        first = i;
      last = i;
      crossings++;
    }
  }
  w.hz = crossings > 1 ? (double)(crossings - 1) * rate /
                             (double)(last - first)
                       : 0.0;
  return w;
}

static uint8_t dmc_ones(void *ctx, uint16_t addr) {
  (void)ctx;
  (void)addr;
  return 0xFF;
}

/* NTSC frames alternate 29780 and 29781 CPU cycles. */
static size_t apu_frames(int frames, int16_t *mono, size_t max) {
  static int16_t st[RA_NES_APU_MAX_SAMPLES * 2];
  size_t n = 0, got, i;
  int f;
  for (f = 0; f < frames; f++) {
    ra_nes_apu_step(29780u + (uint32_t)(f & 1));
    got = ra_nes_apu_read_samples(st, RA_NES_APU_MAX_SAMPLES);
    for (i = 0; i < got && n < max; i++)
      mono[n++] = st[i * 2];
  }
  return n;
}

static void test_nes_apu(void) {
  static int16_t s[40000];
  size_t n;
  wave_t w;
  char what[160];

  printf("--- ra_nes_apu ---\n");

  ra_nes_apu_reset();
  n = apu_frames(60, s, 40000);
  snprintf(what, sizeof(what), "60 frames give %u samples (32715 +- 2)",
           (unsigned)n);
  CHECK(n >= 32713 && n <= 32717, what);

  /* Triangle at $400A = 126: 1789773 / (32 * 127) = 440.4 Hz. */
  ra_nes_apu_reset();
  ra_nes_apu_write(0x4015, 0x04);
  ra_nes_apu_write(0x4008, 0xFF);
  ra_nes_apu_write(0x400A, 126);
  ra_nes_apu_write(0x400B, 0x00);
  n = apu_frames(30, s, 40000);
  w = measure(s + 2000, n - 2000, RA_NES_APU_RATE);
  snprintf(what, sizeof(what), "triangle plays 440 Hz (%.1f Hz, p2p %d)",
           w.hz, w.p2p);
  CHECK(w.hz > 437.0 && w.hz < 443.0 && w.p2p > 4000, what);

  /* Pulse 2 at $4006 = 126, 25% duty: 1789773 / (16 * 127) = 880.8 Hz. */
  ra_nes_apu_reset();
  ra_nes_apu_write(0x4015, 0x02);
  ra_nes_apu_write(0x4004, 0x7F);
  ra_nes_apu_write(0x4005, 0x08);
  ra_nes_apu_write(0x4006, 126);
  ra_nes_apu_write(0x4007, 0x00);
  n = apu_frames(30, s, 40000);
  w = measure(s + 2000, n - 2000, RA_NES_APU_RATE);
  snprintf(what, sizeof(what), "pulse 2 plays 881 Hz (%.1f Hz, p2p %d)", w.hz,
           w.p2p);
  CHECK(w.hz > 876.0 && w.hz < 886.0 && w.p2p > 3000, what);

  /* The length counter: 10 half frames (~83 ms) when not halted. */
  ra_nes_apu_write(0x4004, 0x5F);
  ra_nes_apu_write(0x4007, 0x00);
  CHECK(ra_nes_apu_read(0x4015) & 2u, "$4015 shows pulse 2 running");
  apu_frames(6, s, 40000);
  CHECK(!(ra_nes_apu_read(0x4015) & 2u), "its length counter runs out");

  /* A sweep that would go past $7FF mutes the channel. */
  ra_nes_apu_reset();
  ra_nes_apu_write(0x4015, 0x01);
  ra_nes_apu_write(0x4000, 0xBF);
  ra_nes_apu_write(0x4001, 0x00); /* shift 0: target = 2 x period */
  ra_nes_apu_write(0x4002, 0xFF);
  ra_nes_apu_write(0x4003, 0x07); /* period $7FF */
  n = apu_frames(5, s, 40000);
  w = measure(s, n, RA_NES_APU_RATE);
  CHECK(w.p2p == 0, "a sweep target past $7FF mutes pulse 1");

  /* Noise. */
  ra_nes_apu_reset();
  ra_nes_apu_write(0x4015, 0x08);
  ra_nes_apu_write(0x400C, 0x3F);
  ra_nes_apu_write(0x400E, 0x04);
  ra_nes_apu_write(0x400F, 0x00);
  n = apu_frames(10, s, 40000);
  w = measure(s, n, RA_NES_APU_RATE);
  snprintf(what, sizeof(what), "noise is audible (p2p %d)", w.p2p);
  CHECK(w.p2p > 2000, what);

  /* DMC: 17 bytes of 1-bits from $C000 at rate 15 raise the level. */
  ra_nes_apu_reset();
  ra_nes_apu_set_reader(dmc_ones, NULL);
  ra_nes_apu_write(0x4010, 0x0F);
  ra_nes_apu_write(0x4011, 0x00);
  ra_nes_apu_write(0x4012, 0x00);
  ra_nes_apu_write(0x4013, 0x01);
  ra_nes_apu_write(0x4015, 0x10);
  CHECK(ra_nes_apu_read(0x4015) & 16u, "$4015 shows the DMC playing");
  n = apu_frames(2, s, 40000);
  snprintf(what, sizeof(what), "DMC output rises (%d -> %d)", s[0],
           s[n - 1]);
  CHECK(s[n - 1] - s[0] > 15000, what);
  CHECK(!(ra_nes_apu_read(0x4015) & 16u), "the DMC sample ends");
  ra_nes_apu_set_reader(NULL, NULL);

  /* The frame IRQ flag, 4-step mode, read once. */
  ra_nes_apu_reset();
  ra_nes_apu_write(0x4017, 0x00);
  apu_frames(1, s, 40000);
  ra_nes_apu_step(100);
  CHECK(ra_nes_apu_read(0x4015) & 64u, "frame IRQ flag set in 4-step mode");
  CHECK(!(ra_nes_apu_read(0x4015) & 64u), "reading $4015 clears it");
  ra_nes_apu_reset();
}

static void test_nes_sound(void) {
  struct retro_game_info game;
  uint8_t *rom;
  size_t rom_size, state_size, i;
  void *state;
  wave_t w;
  uint32_t ha = 2166136261u, hb = 2166136261u;
  char what[160];

  printf("--- agnes + ra_nes_apu ---\n");
  rom = make_nes_sound_rom(&rom_size);
  ra_core_nes.set_callbacks(env_cb, video_cb, audio_batch_cb, input_poll_cb,
                            input_state_cb);
  ra_core_nes.init();
  memset(&game, 0, sizeof(game));
  game.data = rom;
  game.size = rom_size;
  CHECK(ra_core_nes.load_game(&game), "load the sound test ROM");

  captured_n = 0;
  capture = 1;
  run_frames(&ra_core_nes, 60);
  capture = 0;
  snprintf(what, sizeof(what), "a frame gives %u samples", (unsigned)last_batch);
  CHECK(last_batch >= 544 && last_batch <= 547, what);
  w = measure(captured + 4000, captured_n - 4000, RA_NES_APU_RATE);
  snprintf(what, sizeof(what),
           "the ROM's pulse plays 440 Hz (%.1f Hz, p2p %d)", w.hz, w.p2p);
  CHECK(w.hz > 437.0 && w.hz < 443.0 && w.p2p > 5000, what);

  state_size = ra_core_nes.serialize_size();
  state = malloc(state_size);
  CHECK(ra_core_nes.serialize(state, state_size), "serialize with the APU");
  captured_n = 0;
  capture = 1;
  run_frames(&ra_core_nes, 3);
  capture = 0;
  for (i = 0; i < captured_n; i++)
    ha = (ha ^ (uint16_t)captured[i]) * 16777619u;
  CHECK(ra_core_nes.unserialize(state, state_size), "unserialize");
  captured_n = 0;
  capture = 1;
  run_frames(&ra_core_nes, 3);
  capture = 0;
  for (i = 0; i < captured_n; i++)
    hb = (hb ^ (uint16_t)captured[i]) * 16777619u;
  CHECK(ha == hb && captured_n > 1000, "restored state replays the sound");

  /* A version 1 state (no APU) still loads. */
  {
    uint32_t v1 = 1;
    memcpy((uint8_t *)state + 4, &v1, sizeof(v1));
    CHECK(ra_core_nes.unserialize(state, state_size - ra_nes_apu_state_size()),
          "a version 1 state loads");
  }
  free(state);
  ra_core_nes.unload_game();
  ra_core_nes.deinit();
  free(rom);
}

/* --- per-core exercise ------------------------------------------------------- */

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
  if (game.size >= 16 && rom[0] == 'N') {
    struct retro_system_av_info av;
    core->get_system_av_info(&av);
    CHECK(av.timing.sample_rate > 0.0, "NES exposes PCM sample rate");
  }

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

  test_nes_apu();
  test_nes_sound();

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
