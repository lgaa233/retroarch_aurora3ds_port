/* NES / Famicom libretro core: agnes wrapped behind the libretro API.
 *
 * agnes (MIT, Krzysztof Gabis) is a single-file, integer-only NES emulator
 * with built-in state dump/restore. agnes.c is included directly into this
 * translation unit so the PPU's palette-index screen buffer can be converted
 * to RGB565 with one table lookup per pixel instead of 61k function calls
 * per frame, which matters on the 134 MHz ARM9.
 */
#include "core.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "agnes.c" /* also brings agnes.h and the internal types */
#pragma GCC diagnostic pop

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static agnes_t *agn;
static uint8_t *rom;
static size_t rom_size;

static uint16_t fb[AGNES_SCREEN_HEIGHT][AGNES_SCREEN_WIDTH];
static uint16_t color_lut[64];
static bool lut_ready;

static void build_lut(void) {
  int i;
  for (i = 0; i < 64; i++) {
    agnes_color_t c = g_colors[i];
    color_lut[i] = (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) |
                              (c.b >> 3));
  }
  lut_ready = true;
}

/* --- the libretro entry points ------------------------------------------- */

static void nes_set_callbacks(retro_environment_t env,
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

static void nes_init(void) {
  if (!lut_ready)
    build_lut();
}

static void nes_deinit(void) {}

static bool nes_load_game(const struct retro_game_info *game) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

  if (!game || !game->data || game->size < 16)
    return false;

  /* agnes keeps pointers into the iNES data, so take a copy that lives as
   * long as the game does. */
  rom = malloc(game->size);
  if (!rom)
    return false;
  memcpy(rom, game->data, game->size);
  rom_size = game->size;

  agn = agnes_make();
  if (!agn) {
    free(rom);
    rom = NULL;
    return false;
  }
  if (!agnes_load_ines_data(agn, rom, rom_size)) {
    agnes_destroy(agn);
    agn = NULL;
    free(rom);
    rom = NULL;
    return false;
  }

  memset(fb, 0, sizeof(fb));
  if (env_cb)
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  return true;
}

static void nes_unload_game(void) {
  if (agn) {
    agnes_destroy(agn);
    agn = NULL;
  }
  free(rom);
  rom = NULL;
  rom_size = 0;
}

static void nes_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = AGNES_SCREEN_WIDTH;
  info->geometry.base_height = AGNES_SCREEN_HEIGHT;
  info->geometry.max_width = AGNES_SCREEN_WIDTH;
  info->geometry.max_height = AGNES_SCREEN_HEIGHT;
  info->geometry.aspect_ratio =
      (float)AGNES_SCREEN_WIDTH / (float)AGNES_SCREEN_HEIGHT;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 0.0; /* agnes has no APU */
}

static void nes_run(void) {
  agnes_input_t in;
  const uint8_t *src;
  uint16_t *dst;
  int i;

  if (input_poll_cb)
    input_poll_cb();
  memset(&in, 0, sizeof(in));
  if (input_state_cb) {
#define PRESSED(id) (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, (id)) != 0)
    in.a = PRESSED(RETRO_DEVICE_ID_JOYPAD_A);
    in.b = PRESSED(RETRO_DEVICE_ID_JOYPAD_B);
    in.select = PRESSED(RETRO_DEVICE_ID_JOYPAD_SELECT);
    in.start = PRESSED(RETRO_DEVICE_ID_JOYPAD_START);
    in.up = PRESSED(RETRO_DEVICE_ID_JOYPAD_UP);
    in.down = PRESSED(RETRO_DEVICE_ID_JOYPAD_DOWN);
    in.left = PRESSED(RETRO_DEVICE_ID_JOYPAD_LEFT);
    in.right = PRESSED(RETRO_DEVICE_ID_JOYPAD_RIGHT);
#undef PRESSED
  }
  agnes_set_input(agn, &in, NULL);

  agnes_next_frame(agn);

  /* Palette indices to RGB565 through the lookup table. */
  src = agn->ppu.screen_buffer;
  dst = &fb[0][0];
  for (i = 0; i < AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT; i++)
    dst[i] = color_lut[src[i] & 0x3F];

  if (video_cb)
    video_cb(fb, AGNES_SCREEN_WIDTH, AGNES_SCREEN_HEIGHT,
             AGNES_SCREEN_WIDTH * sizeof(uint16_t));
}

static void nes_reset(void) {
  /* agnes has no reset call; reload the same iNES image. */
  if (!agn || !rom)
    return;
  agnes_destroy(agn);
  agn = agnes_make();
  if (agn)
    agnes_load_ines_data(agn, rom, rom_size);
}

typedef struct {
  char magic[4]; /* "AGNS" */
  uint32_t version;
  uint32_t state_size;
  uint32_t rom_size;
} nes_state_header_t;

#define NES_STATE_VERSION 1u

static size_t nes_serialize_size(void) {
  if (!agn)
    return 0;
  return sizeof(nes_state_header_t) + agnes_state_size();
}

static bool nes_serialize(void *data, size_t size) {
  nes_state_header_t h;
  uint8_t *p = data;

  if (!agn || size < nes_serialize_size())
    return false;
  memcpy(h.magic, "AGNS", 4);
  h.version = NES_STATE_VERSION;
  h.state_size = (uint32_t)agnes_state_size();
  h.rom_size = (uint32_t)rom_size;
  memcpy(p, &h, sizeof(h));
  agnes_dump_state(agn, (agnes_state_t *)(p + sizeof(h)));
  return true;
}

static bool nes_unserialize(const void *data, size_t size) {
  nes_state_header_t h;
  const uint8_t *p = data;

  if (!agn || size < sizeof(h))
    return false;
  memcpy(&h, p, sizeof(h));
  if (memcmp(h.magic, "AGNS", 4) != 0 || h.version != NES_STATE_VERSION ||
      h.state_size != agnes_state_size() || h.rom_size != rom_size)
    return false;
  if (size < sizeof(h) + h.state_size)
    return false;
  return agnes_restore_state(agn, (const agnes_state_t *)(p + sizeof(h)));
}

static void *nes_get_memory_data(unsigned id) {
  (void)id;
  return NULL; /* battery RAM is covered by save states only */
}

static size_t nes_get_memory_size(unsigned id) {
  (void)id;
  return 0;
}

const ra_core_t ra_core_nes = {
  .library_name = "agnes",
  .library_version = AGNES_VERSION_STRING,
  .valid_extensions = "nes",
  .set_callbacks = nes_set_callbacks,
  .init = nes_init,
  .deinit = nes_deinit,
  .load_game = nes_load_game,
  .unload_game = nes_unload_game,
  .get_system_av_info = nes_get_system_av_info,
  .run = nes_run,
  .reset = nes_reset,
  .serialize_size = nes_serialize_size,
  .serialize = nes_serialize,
  .unserialize = nes_unserialize,
  .get_memory_data = nes_get_memory_data,
  .get_memory_size = nes_get_memory_size,
};
