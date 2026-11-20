/* Sega 8-bit libretro core: SMS Plus GX wrapped behind the libretro API.
 *
 * SMS Plus GX (GPL-2.0+, Charles MacDonald, Eke-Eke, gameblabla) emulates
 * the Master System, the Game Gear, the SG-1000 and the ColecoVision with
 * one Z80 core and one VDP, all in integer C. The save-state layout and the
 * load-time wiring follow the project's own libretro port
 * (source/ports/libretro/smsplus_libretro.c), with a plain cursor instead
 * of libretro-common's memstream.
 *
 * ColecoVision games need the console's BIOS as `BIOS.col` in the system
 * directory (the frontend points it at SD:/Aurora/ROMs).
 */
#include "core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared.h"

t_config option; /* the core reads these settings (config.h) */

extern uint32_t load_rom_mem(const char *data, size_t size);

/* The core calls this around resets to flush battery RAM; the frontend
 * persists cart.sram through the libretro memory API instead. */
void system_manage_sram(uint8_t *sram, uint8_t slot_n, uint8_t mode) {
  (void)sram;
  (void)slot_n;
  (void)mode;
}

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static uint16_t *sms_bitmap;
static bool loaded;
static unsigned out_width = VIDEO_WIDTH_SMS;
static unsigned out_height = VIDEO_HEIGHT_SMS;

/* --- save states (the libretro port's layout, cursor-based) ---------------- */

typedef struct {
  uint8_t *p;
  size_t pos, size;
  bool ok;
} cursor_t;

static void cur_write(cursor_t *c, const void *data, size_t n) {
  if (c->pos + n > c->size) {
    c->ok = false;
    return;
  }
  memcpy(c->p + c->pos, data, n);
  c->pos += n;
}

static void cur_read(cursor_t *c, void *data, size_t n) {
  if (c->pos + n > c->size) {
    c->ok = false;
    return;
  }
  memcpy(data, c->p + c->pos, n);
  c->pos += n;
}

static size_t sms_state_size(void) {
  return sizeof(vdp_t) + sizeof(sms_t) + 4 + 0x8000 + sizeof(z80_t) +
         YM2413_GetContextSize() + FM_GetContextSize() +
         SN76489_GetContextSize();
}

static bool sms_state_save(void *data, size_t size) {
  cursor_t c = {data, 0, size, true};

  cur_write(&c, &vdp, sizeof(vdp_t));
  cur_write(&c, &sms, sizeof(sms_t));
  cur_write(&c, cart.fcr, 4);
  cur_write(&c, cart.sram, 0x8000);
  cur_write(&c, Z80_Context, sizeof(z80_t));
  cur_write(&c, YM2413_GetContextPtr(), YM2413_GetContextSize());
  cur_write(&c, FM_GetContextPtr(), FM_GetContextSize());
  cur_write(&c, SN76489_GetContextPtr(0), SN76489_GetContextSize());
  return c.ok;
}

static bool sms_state_load(const void *data, size_t size) {
  cursor_t c = {(uint8_t *)data, 0, size, true};
  uint8_t *buf;
  unsigned i;

  system_reset();

  cur_read(&c, &vdp, sizeof(vdp_t));
  cur_read(&c, &sms, sizeof(sms_t));

  /* Restore video and audio settings (needed if the timing changed). */
  vdp_init();
  SMSPLUS_sound_init();

  cur_read(&c, cart.fcr, 4);
  cur_read(&c, cart.sram, 0x8000);
  cur_read(&c, Z80_Context, sizeof(z80_t));
  Z80.irq_callback = sms_irq_callback;

  cur_read(&c, YM2413_GetContextPtr(), YM2413_GetContextSize());

  buf = malloc(FM_GetContextSize());
  cur_read(&c, buf, FM_GetContextSize());
  FM_SetContext(buf);
  free(buf);

  buf = malloc(SN76489_GetContextSize());
  cur_read(&c, buf, SN76489_GetContextSize());
  SN76489_SetContext(0, buf);
  free(buf);

  if (!c.ok)
    return false;

  if (sms.console != CONSOLE_COLECO && sms.console != CONSOLE_SG1000) {
    /* Cartridge in the slot by default; restore the mapping. */
    slot.rom = cart.rom;
    slot.pages = cart.pages;
    slot.mapper = cart.mapper;
    slot.fcr = &cart.fcr[0];

    mapper_reset();
    cpu_readmap[0] = &slot.rom[0];
    if (slot.mapper != MAPPER_KOREA_MSX) {
      mapper_16k_w(0, slot.fcr[0]);
      mapper_16k_w(1, slot.fcr[1]);
      mapper_16k_w(2, slot.fcr[2]);
      mapper_16k_w(3, slot.fcr[3]);
    } else {
      mapper_8k_w(0, slot.fcr[0]);
      mapper_8k_w(1, slot.fcr[1]);
      mapper_8k_w(2, slot.fcr[2]);
      mapper_8k_w(3, slot.fcr[3]);
    }
  }

  /* Force a full pattern cache update. */
  bg_list_index = 0x200;
  for (i = 0; i < 0x200; i++) {
    bg_name_list[i] = i;
    bg_name_dirty[i] = 255;
  }
  for (i = 0; i < PALETTE_SIZE; i++)
    palette_sync(i);
  return true;
}

/* --- loading ----------------------------------------------------------------- */

static const char *path_ext(const char *path) {
  const char *dot = path ? strrchr(path, '.') : NULL;
  return dot ? dot + 1 : "";
}

static int ext_is(const char *ext, const char *want) {
  while (*ext && *want) {
    char a = *ext++, b = *want++;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (a != b)
      return 0;
  }
  return !*ext && !*want;
}

static bool coleco_bios_load(void) {
  const char *dir = NULL;
  char path[600];
  FILE *fd;

  if (!env_cb || !env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) || !dir)
    return false;
  snprintf(path, sizeof(path), "%s/BIOS.col", dir);
  fd = fopen(path, "rb");
  if (!fd)
    return false;
  if (fread(coleco.rom, 1, sizeof(coleco.rom), fd) != sizeof(coleco.rom)) {
    fclose(fd);
    return false;
  }
  fclose(fd);
  return true;
}

static void sms_set_callbacks(retro_environment_t env,
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

static void sms_core_init(void) {}

static void sms_core_deinit(void) {}

static bool sms_load_game(const struct retro_game_info *game) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
  const char *ext;

  if (!game || !game->data || game->size < 0x400)
    return false;
  ext = path_ext(game->path);

  memset(&option, 0, sizeof(option));
  option.fm = 0; /* the FM unit costs ARM9 cycles and nothing plays it yet */
  option.spritelimit = 1;
  option.tms_pal = 2;
  option.country = 0;
  option.console = 0; /* auto, from the header and the game database */
  if (ext_is(ext, "sg"))
    option.console = 5; /* SG-1000 */
  else if (ext_is(ext, "col"))
    option.console = 6; /* ColecoVision */

  if (option.console == 6 && !coleco_bios_load())
    return false; /* the ColecoVision BIOS is required */

  if (!load_rom_mem(game->data, game->size))
    return false;

  /* A headerless Game Gear dump is not caught by the "TMR SEGA" check; go
   * by the file extension (the game database has already had its say). */
  if (ext_is(ext, "gg") && sms.console != CONSOLE_GG &&
      sms.console != CONSOLE_GGMS)
    sms.console = CONSOLE_GG;

  if (!sms_bitmap)
    sms_bitmap = malloc(VIDEO_WIDTH_SMS * 240 * sizeof(uint16_t));
  if (!sms_bitmap) {
    free_rom();
    return false;
  }
  memset(sms_bitmap, 0, VIDEO_WIDTH_SMS * 240 * sizeof(uint16_t));

  memset(&bitmap, 0, sizeof(bitmap));
  bitmap.width = VIDEO_WIDTH_SMS;
  bitmap.height = 240;
  bitmap.depth = 16;
  bitmap.data = (uint8_t *)sms_bitmap;
  bitmap.pitch = VIDEO_WIDTH_SMS * sizeof(uint16_t);
  bitmap.viewport.w = VIDEO_WIDTH_SMS;
  bitmap.viewport.h = VIDEO_HEIGHT_SMS;
  bitmap.viewport.x = 0;
  bitmap.viewport.y = 0;

  system_poweron();

  out_width = bitmap.viewport.w;
  out_height = bitmap.viewport.h;

  if (env_cb)
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  loaded = true;
  return true;
}

static void sms_unload_game(void) {
  if (!loaded)
    return;
  system_poweroff();
  free_rom();
  free(sms_bitmap);
  sms_bitmap = NULL;
  loaded = false;
}

static void sms_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = out_width;
  info->geometry.base_height = out_height;
  info->geometry.max_width = VIDEO_WIDTH_SMS;
  info->geometry.max_height = 240;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = (sms.display == DISPLAY_PAL) ? FPS_PAL : FPS_NTSC;
  info->timing.sample_rate = (double)SOUND_FREQUENCY;
}

static void sms_core_run(void) {
  int32_t xoffset;

  if (input_poll_cb)
    input_poll_cb();

  input.pad[0] = 0;
  input.pad[1] = 0;
  coleco.keypad[0] = (uint8_t)~0;
  coleco.keypad[1] = (uint8_t)~0;
  input.system &= (sms.console == CONSOLE_GG) ? (uint8_t)~INPUT_START
                                              : (uint8_t)~INPUT_PAUSE;

  if (input_state_cb) {
#define PRESSED(id) input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, (id))
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_UP)) input.pad[0] |= INPUT_UP;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_DOWN)) input.pad[0] |= INPUT_DOWN;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_LEFT)) input.pad[0] |= INPUT_LEFT;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_RIGHT)) input.pad[0] |= INPUT_RIGHT;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_B)) input.pad[0] |= INPUT_BUTTON1;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_A)) input.pad[0] |= INPUT_BUTTON2;

    if (sms.console == CONSOLE_COLECO) {
      /* The keypad keys that fit on the 3DS: enough for the usual
       * "press 1/2 to start" screens. */
      if (PRESSED(RETRO_DEVICE_ID_JOYPAD_X))
        coleco.keypad[0] = 1;
      else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_Y))
        coleco.keypad[0] = 2;
      else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_R))
        coleco.keypad[0] = 3;
      else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_L))
        coleco.keypad[0] = 4;
      else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_START))
        coleco.keypad[0] = 10; /* # */
      else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_SELECT))
        coleco.keypad[0] = 11; /* * */
      input.system = 0;
    } else if (PRESSED(RETRO_DEVICE_ID_JOYPAD_START)) {
      input.system |=
          (sms.console == CONSOLE_GG) ? INPUT_START : INPUT_PAUSE;
    }
#undef PRESSED
  }

  system_frame(0);

  xoffset = bitmap.viewport.x;
  out_width = bitmap.viewport.w;
  out_height = bitmap.viewport.h;

  if (video_cb)
    video_cb((const uint16_t *)sms_bitmap + xoffset, out_width, out_height,
             bitmap.pitch);
  if (audio_batch_cb && snd.enabled)
    audio_batch_cb((const int16_t *)snd.output, (size_t)snd.sample_count);
}

static void sms_core_reset(void) {
  if (loaded)
    system_reset();
}

typedef struct {
  char magic[4]; /* "SMSS" */
  uint32_t version;
  uint32_t state_size;
  uint32_t crc;
} sms_state_header_t;

#define SMS_STATE_VERSION 1u

static size_t sms_serialize_size(void) {
  if (!loaded)
    return 0;
  return sizeof(sms_state_header_t) + sms_state_size();
}

static bool sms_serialize(void *data, size_t size) {
  sms_state_header_t h;
  uint8_t *p = data;

  if (!loaded || size < sms_serialize_size())
    return false;
  memcpy(h.magic, "SMSS", 4);
  h.version = SMS_STATE_VERSION;
  h.state_size = (uint32_t)sms_state_size();
  h.crc = cart.crc;
  memcpy(p, &h, sizeof(h));
  return sms_state_save(p + sizeof(h), size - sizeof(h));
}

static bool sms_unserialize(const void *data, size_t size) {
  sms_state_header_t h;
  const uint8_t *p = data;

  if (!loaded || size < sizeof(h))
    return false;
  memcpy(&h, p, sizeof(h));
  if (memcmp(h.magic, "SMSS", 4) != 0 || h.version != SMS_STATE_VERSION ||
      h.state_size != sms_state_size() || h.crc != cart.crc)
    return false;
  if (size < sizeof(h) + h.state_size)
    return false;
  return sms_state_load(p + sizeof(h), size - sizeof(h));
}

static void *sms_get_memory_data(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM && loaded)
    return cart.sram;
  return NULL;
}

static size_t sms_get_memory_size(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM && loaded)
    return 0x8000;
  return 0;
}

const ra_core_t ra_core_sms = {
  .library_name = "SMS Plus GX",
  .library_version = "head",
  .valid_extensions = "sms|gg|sg|col",
  .set_callbacks = sms_set_callbacks,
  .init = sms_core_init,
  .deinit = sms_core_deinit,
  .load_game = sms_load_game,
  .unload_game = sms_unload_game,
  .get_system_av_info = sms_get_system_av_info,
  .run = sms_core_run,
  .reset = sms_core_reset,
  .serialize_size = sms_serialize_size,
  .serialize = sms_serialize,
  .unserialize = sms_unserialize,
  .get_memory_data = sms_get_memory_data,
  .get_memory_size = sms_get_memory_size,
};
