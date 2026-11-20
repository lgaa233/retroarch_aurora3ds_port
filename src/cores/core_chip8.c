/* CHIP-8 libretro core, written for this frontend.
 *
 * CHIP-8 is the 1977 virtual machine of the COSMAC VIP; its ROMs (.ch8) are
 * public-domain games people still write today, and a CHIP-8 core is a
 * RetroArch tradition. The whole machine is a few hundred bytes of state,
 * which makes it the cheapest possible platform for a 134 MHz ARM9.
 *
 * The interpreter covers the classic CHIP-8 opcode set with the usual
 * quirk choices (shift puts VY shifted into VX as on the VIP; load/store
 * leave I untouched as most modern ROMs expect).
 *
 * RetroPad mapping for the 4x4 keypad (the usual libretro arrangement for
 * one-player games): D-pad -> 2/4/6/8, A -> 5, B -> 6, X -> 1, Y -> 3,
 * L -> A, R -> B, START -> C, SELECT -> D, and 0/E/F stay unmapped.
 */
#include "core.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define C8_W 64
#define C8_H 32
#define C8_RAM 4096
#define C8_START 0x200
#define C8_CYCLES_PER_FRAME 14 /* about 840 ops/s, a comfortable VIP pace */

typedef struct {
  uint8_t ram[C8_RAM];
  uint8_t v[16];
  uint16_t i;
  uint16_t pc;
  uint16_t stack[16];
  uint8_t sp;
  uint8_t delay;
  uint8_t sound;
  uint8_t screen[C8_H][C8_W]; /* 0 or 1 */
  uint8_t waiting_key;        /* 0x80 | VX while FX0A waits */
  uint16_t keys;              /* bit n = key n held */
  uint32_t rng;
} chip8_t;

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static chip8_t c8;
static uint8_t *rom;
static size_t rom_size;
static bool loaded;
static uint16_t fb[C8_H][C8_W];

static const uint8_t font[80] = {
  0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70,
  0xF0, 0x10, 0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0,
  0x90, 0x90, 0xF0, 0x10, 0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0,
  0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0, 0x10, 0x20, 0x40, 0x40,
  0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0, 0x10, 0xF0,
  0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
  0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0,
  0xF0, 0x80, 0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80,
};

static void c8_reset(void) {
  memset(&c8, 0, sizeof(c8));
  memcpy(c8.ram, font, sizeof(font));
  if (rom)
    memcpy(c8.ram + C8_START, rom,
           rom_size > C8_RAM - C8_START ? C8_RAM - C8_START : rom_size);
  c8.pc = C8_START;
  c8.rng = 0x2A6D365Au;
}

static uint8_t c8_rand(void) {
  /* xorshift32: deterministic, so save states replay exactly. */
  uint32_t x = c8.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  c8.rng = x;
  return (uint8_t)x;
}

static void c8_step(void) {
  uint16_t op, nnn;
  uint8_t x, y, n, kk;

  if (c8.waiting_key) {
    int k;
    for (k = 0; k < 16; k++) {
      if (c8.keys & (1 << k)) {
        c8.v[c8.waiting_key & 0x0F] = (uint8_t)k;
        c8.waiting_key = 0;
        break;
      }
    }
    return;
  }

  op = (uint16_t)((c8.ram[c8.pc & (C8_RAM - 1)] << 8) |
                  c8.ram[(c8.pc + 1) & (C8_RAM - 1)]);
  c8.pc = (uint16_t)(c8.pc + 2);
  nnn = op & 0x0FFF;
  x = (op >> 8) & 0x0F;
  y = (op >> 4) & 0x0F;
  n = op & 0x0F;
  kk = op & 0xFF;

  switch (op >> 12) {
  case 0x0:
    if (op == 0x00E0)
      memset(c8.screen, 0, sizeof(c8.screen));
    else if (op == 0x00EE && c8.sp)
      c8.pc = c8.stack[--c8.sp];
    break;
  case 0x1:
    c8.pc = nnn;
    break;
  case 0x2:
    if (c8.sp < 16)
      c8.stack[c8.sp++] = c8.pc;
    c8.pc = nnn;
    break;
  case 0x3:
    if (c8.v[x] == kk) c8.pc += 2;
    break;
  case 0x4:
    if (c8.v[x] != kk) c8.pc += 2;
    break;
  case 0x5:
    if (c8.v[x] == c8.v[y]) c8.pc += 2;
    break;
  case 0x6:
    c8.v[x] = kk;
    break;
  case 0x7:
    c8.v[x] = (uint8_t)(c8.v[x] + kk);
    break;
  case 0x8: {
    uint8_t vf;
    switch (n) {
    case 0x0: c8.v[x] = c8.v[y]; break;
    case 0x1: c8.v[x] |= c8.v[y]; break;
    case 0x2: c8.v[x] &= c8.v[y]; break;
    case 0x3: c8.v[x] ^= c8.v[y]; break;
    case 0x4: {
      uint16_t sum = (uint16_t)(c8.v[x] + c8.v[y]);
      vf = sum > 0xFF;
      c8.v[x] = (uint8_t)sum;
      c8.v[0xF] = vf;
      break;
    }
    case 0x5:
      vf = c8.v[x] >= c8.v[y];
      c8.v[x] = (uint8_t)(c8.v[x] - c8.v[y]);
      c8.v[0xF] = vf;
      break;
    case 0x6:
      vf = c8.v[y] & 1;
      c8.v[x] = c8.v[y] >> 1;
      c8.v[0xF] = vf;
      break;
    case 0x7:
      vf = c8.v[y] >= c8.v[x];
      c8.v[x] = (uint8_t)(c8.v[y] - c8.v[x]);
      c8.v[0xF] = vf;
      break;
    case 0xE:
      vf = c8.v[y] >> 7;
      c8.v[x] = (uint8_t)(c8.v[y] << 1);
      c8.v[0xF] = vf;
      break;
    }
    break;
  }
  case 0x9:
    if (c8.v[x] != c8.v[y]) c8.pc += 2;
    break;
  case 0xA:
    c8.i = nnn;
    break;
  case 0xB:
    c8.pc = (uint16_t)(nnn + c8.v[0]);
    break;
  case 0xC:
    c8.v[x] = c8_rand() & kk;
    break;
  case 0xD: {
    /* Draw an 8xN sprite at (VX, VY), XOR, VF = collision, wrap. */
    uint8_t row;
    c8.v[0xF] = 0;
    for (row = 0; row < n; row++) {
      uint8_t bits = c8.ram[(c8.i + row) & (C8_RAM - 1)];
      uint8_t py = (uint8_t)((c8.v[y] + row) & (C8_H - 1));
      uint8_t col;
      for (col = 0; col < 8; col++) {
        uint8_t px = (uint8_t)((c8.v[x] + col) & (C8_W - 1));
        if (bits & (0x80 >> col)) {
          if (c8.screen[py][px])
            c8.v[0xF] = 1;
          c8.screen[py][px] ^= 1;
        }
      }
    }
    break;
  }
  case 0xE:
    if (kk == 0x9E && (c8.keys & (1 << (c8.v[x] & 0x0F))))
      c8.pc += 2;
    else if (kk == 0xA1 && !(c8.keys & (1 << (c8.v[x] & 0x0F))))
      c8.pc += 2;
    break;
  case 0xF:
    switch (kk) {
    case 0x07: c8.v[x] = c8.delay; break;
    case 0x0A: c8.waiting_key = 0x80 | x; break;
    case 0x15: c8.delay = c8.v[x]; break;
    case 0x18: c8.sound = c8.v[x]; break;
    case 0x1E: c8.i = (uint16_t)((c8.i + c8.v[x]) & 0x0FFF); break;
    case 0x29: c8.i = (uint16_t)((c8.v[x] & 0x0F) * 5); break;
    case 0x33: {
      uint8_t v = c8.v[x];
      c8.ram[c8.i & (C8_RAM - 1)] = v / 100;
      c8.ram[(c8.i + 1) & (C8_RAM - 1)] = (v / 10) % 10;
      c8.ram[(c8.i + 2) & (C8_RAM - 1)] = v % 10;
      break;
    }
    case 0x55: {
      uint8_t r;
      for (r = 0; r <= x; r++)
        c8.ram[(c8.i + r) & (C8_RAM - 1)] = c8.v[r];
      break;
    }
    case 0x65: {
      uint8_t r;
      for (r = 0; r <= x; r++)
        c8.v[r] = c8.ram[(c8.i + r) & (C8_RAM - 1)];
      break;
    }
    }
    break;
  }
}

/* --- the libretro entry points ------------------------------------------- */

static void ch8_set_callbacks(retro_environment_t env,
                              retro_video_refresh_t video,
                              retro_audio_sample_batch_t audio_batch,
                              retro_input_poll_t input_poll,
                              retro_input_state_t input_state) {
  env_cb = env;
  video_cb = video;
  audio_batch_cb = audio_batch;
  input_poll_cb = input_poll;
  input_state_cb = input_state;
}

static void ch8_init(void) {}

static void ch8_deinit(void) {}

static bool ch8_load_game(const struct retro_game_info *game) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

  if (!game || !game->data || !game->size ||
      game->size > C8_RAM - C8_START)
    return false;
  rom = malloc(game->size);
  if (!rom)
    return false;
  memcpy(rom, game->data, game->size);
  rom_size = game->size;
  c8_reset();
  if (env_cb)
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  loaded = true;
  return true;
}

static void ch8_unload_game(void) {
  free(rom);
  rom = NULL;
  rom_size = 0;
  loaded = false;
}

static void ch8_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = C8_W;
  info->geometry.base_height = C8_H;
  info->geometry.max_width = C8_W;
  info->geometry.max_height = C8_H;
  info->geometry.aspect_ratio = (float)C8_W / (float)C8_H;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 0.0;
}

static void ch8_run(void) {
  int i, x, y;

  if (input_poll_cb)
    input_poll_cb();
  c8.keys = 0;
  if (input_state_cb) {
    static const uint8_t map[][2] = {
      {RETRO_DEVICE_ID_JOYPAD_UP, 0x2},    {RETRO_DEVICE_ID_JOYPAD_LEFT, 0x4},
      {RETRO_DEVICE_ID_JOYPAD_RIGHT, 0x6}, {RETRO_DEVICE_ID_JOYPAD_DOWN, 0x8},
      {RETRO_DEVICE_ID_JOYPAD_A, 0x5},     {RETRO_DEVICE_ID_JOYPAD_B, 0x6},
      {RETRO_DEVICE_ID_JOYPAD_X, 0x1},     {RETRO_DEVICE_ID_JOYPAD_Y, 0x3},
      {RETRO_DEVICE_ID_JOYPAD_L, 0xA},     {RETRO_DEVICE_ID_JOYPAD_R, 0xB},
      {RETRO_DEVICE_ID_JOYPAD_START, 0xC}, {RETRO_DEVICE_ID_JOYPAD_SELECT, 0xD},
    };
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
      if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i][0]))
        c8.keys |= (uint16_t)(1 << map[i][1]);
  }

  for (i = 0; i < C8_CYCLES_PER_FRAME; i++)
    c8_step();
  if (c8.delay)
    c8.delay--;
  if (c8.sound)
    c8.sound--;

  for (y = 0; y < C8_H; y++)
    for (x = 0; x < C8_W; x++)
      fb[y][x] = c8.screen[y][x] ? 0xFFFF : 0x0000;

  if (video_cb)
    video_cb(fb, C8_W, C8_H, C8_W * sizeof(uint16_t));
}

static void ch8_core_reset(void) {
  if (loaded)
    c8_reset();
}

typedef struct {
  char magic[4]; /* "CHP8" */
  uint32_t version;
  uint32_t rom_size;
} ch8_state_header_t;

#define CH8_STATE_VERSION 1u

static size_t ch8_serialize_size(void) {
  if (!loaded)
    return 0;
  return sizeof(ch8_state_header_t) + sizeof(c8);
}

static bool ch8_serialize(void *data, size_t size) {
  ch8_state_header_t h;
  uint8_t *p = data;

  if (!loaded || size < ch8_serialize_size())
    return false;
  memcpy(h.magic, "CHP8", 4);
  h.version = CH8_STATE_VERSION;
  h.rom_size = (uint32_t)rom_size;
  memcpy(p, &h, sizeof(h));
  memcpy(p + sizeof(h), &c8, sizeof(c8));
  return true;
}

static bool ch8_unserialize(const void *data, size_t size) {
  ch8_state_header_t h;
  const uint8_t *p = data;

  if (!loaded || size < sizeof(h))
    return false;
  memcpy(&h, p, sizeof(h));
  if (memcmp(h.magic, "CHP8", 4) != 0 || h.version != CH8_STATE_VERSION ||
      h.rom_size != rom_size)
    return false;
  if (size < sizeof(h) + sizeof(c8))
    return false;
  memcpy(&c8, p + sizeof(h), sizeof(c8));
  return true;
}

static void *ch8_get_memory_data(unsigned id) {
  (void)id;
  return NULL;
}

static size_t ch8_get_memory_size(unsigned id) {
  (void)id;
  return 0;
}

const ra_core_t ra_core_chip8 = {
  .library_name = "chip8",
  .library_version = "1.0",
  .valid_extensions = "ch8",
  .set_callbacks = ch8_set_callbacks,
  .init = ch8_init,
  .deinit = ch8_deinit,
  .load_game = ch8_load_game,
  .unload_game = ch8_unload_game,
  .get_system_av_info = ch8_get_system_av_info,
  .run = ch8_run,
  .reset = ch8_core_reset,
  .serialize_size = ch8_serialize_size,
  .serialize = ch8_serialize,
  .unserialize = ch8_unserialize,
  .get_memory_data = ch8_get_memory_data,
  .get_memory_size = ch8_get_memory_size,
};
