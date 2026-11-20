/* Game Boy (DMG) libretro core: Peanut-GB wrapped behind the libretro API.
 *
 * Peanut-GB (MIT, Mahyar Koshkouei) keeps all machine state inside one
 * struct gb_s and reads the ROM and cart RAM through callbacks, which makes
 * it a good fit for a 134 MHz ARM9 with no FPU: it is integer-only and was
 * written for microcontrollers.
 *
 * Save states are a raw dump of struct gb_s plus the cart RAM; the function
 * pointers inside are re-pointed at this build's callbacks on restore, so a
 * state is only valid for the same ROM (the header records its size).
 */
#include "retroarch.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "minigb_apu.h"

uint8_t audio_read(uint16_t addr);
void audio_write(uint16_t addr, uint8_t val);

#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#include "peanut_gb.h"

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static struct gb_s gb;
static struct minigb_apu_ctx apu;

uint8_t audio_read(uint16_t addr) {
  return minigb_apu_audio_read(&apu, addr);
}

void audio_write(uint16_t addr, uint8_t val) {
  minigb_apu_audio_write(&apu, addr, val);
}
static uint8_t *rom;
static size_t rom_size;
static uint8_t *cart_ram;
static size_t cart_ram_size;
static bool loaded;
static enum gb_error_e last_error;

/* 160x144 RGB565, written line by line from the LCD callback. */
static uint16_t fb[LCD_HEIGHT][LCD_WIDTH];

#define C565(r, g, b) \
  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* The classic DMG green ramp, one ramp for BG, OBJ0 and OBJ1. */
static const uint16_t palette[3][4] = {
  {C565(0xE0, 0xF8, 0xD0), C565(0x88, 0xC0, 0x70),
   C565(0x34, 0x68, 0x56), C565(0x08, 0x18, 0x20)}, /* OBJ0 */
  {C565(0xE0, 0xF8, 0xD0), C565(0x88, 0xC0, 0x70),
   C565(0x34, 0x68, 0x56), C565(0x08, 0x18, 0x20)}, /* OBJ1 */
  {C565(0xE0, 0xF8, 0xD0), C565(0x88, 0xC0, 0x70),
   C565(0x34, 0x68, 0x56), C565(0x08, 0x18, 0x20)}, /* BG */
};

static uint8_t rom_read(struct gb_s *g, const uint_fast32_t addr) {
  (void)g;
  return rom[addr];
}

static uint8_t ram_read(struct gb_s *g, const uint_fast32_t addr) {
  (void)g;
  return cart_ram[addr];
}

static void ram_write(struct gb_s *g, const uint_fast32_t addr,
                      const uint8_t val) {
  (void)g;
  cart_ram[addr] = val;
}

static void on_error(struct gb_s *g, const enum gb_error_e err,
                     const uint16_t addr) {
  (void)g;
  (void)addr;
  /* Keep running: an invalid opcode in a bad dump is not worth a crash
   * screen. The frontend can show last_error if it wants to. */
  last_error = err;
}

static void lcd_draw_line(struct gb_s *g, const uint8_t *pixels,
                          const uint_fast8_t line) {
  uint16_t *dst = fb[line];
  int x;
  (void)g;
  if (ra_video_will_skip())
    return;
  for (x = 0; x < LCD_WIDTH; x++) {
    uint8_t p = pixels[x];
    dst[x] = palette[(p & LCD_PALETTE_ALL) >> 4][p & LCD_COLOUR];
  }
}

static void fix_pointers(void) {
  gb.gb_rom_read = rom_read;
  gb.gb_cart_ram_read = ram_read;
  gb.gb_cart_ram_write = ram_write;
  gb.gb_error = on_error;
  gb.gb_serial_tx = NULL;
  gb.gb_serial_rx = NULL;
  gb.gb_bootrom_read = NULL;
  gb.display.lcd_draw_line = lcd_draw_line;
  gb.direct.priv = NULL;
}

/* --- the libretro entry points ------------------------------------------- */

static void gbc_set_callbacks(retro_environment_t env,
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

static void gbc_init(void) {}

static void gbc_deinit(void) {}

static bool gbc_load_game(const struct retro_game_info *game) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

  if (!game || !game->data || game->size < 0x150)
    return false;

  rom = malloc(game->size);
  if (!rom)
    return false;
  memcpy(rom, game->data, game->size);
  rom_size = game->size;
  last_error = GB_UNKNOWN_ERROR;

  if (gb_init(&gb, rom_read, ram_read, ram_write, on_error, NULL) !=
      GB_INIT_NO_ERROR) {
    free(rom);
    rom = NULL;
    return false;
  }

  if (gb_get_save_size_s(&gb, &cart_ram_size) < 0)
    cart_ram_size = 0;
  if (cart_ram_size) {
    cart_ram = calloc(1, cart_ram_size);
    if (!cart_ram) {
      free(rom);
      rom = NULL;
      return false;
    }
  }

  gb_init_lcd(&gb, lcd_draw_line);
  minigb_apu_audio_init(&apu);
  memset(fb, 0, sizeof(fb));

  if (env_cb)
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

  loaded = true;
  return true;
}

static void gbc_unload_game(void) {
  free(rom);
  rom = NULL;
  rom_size = 0;
  free(cart_ram);
  cart_ram = NULL;
  cart_ram_size = 0;
  loaded = false;
}

static void gbc_get_system_av_info(struct retro_system_av_info *info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = LCD_WIDTH;
  info->geometry.base_height = LCD_HEIGHT;
  info->geometry.max_width = LCD_WIDTH;
  info->geometry.max_height = LCD_HEIGHT;
  info->geometry.aspect_ratio = (float)LCD_WIDTH / (float)LCD_HEIGHT;
  info->timing.fps = 4194304.0 / 70224.0; /* 59.73 Hz, the DMG's rate */
  info->timing.sample_rate = (double)AUDIO_SAMPLE_RATE;
}

static void gbc_run(void) {
  unsigned joy = 0xFF; /* active low: 1 = released */

  if (input_poll_cb)
    input_poll_cb();
  if (input_state_cb) {
#define PRESSED(id) input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, (id))
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_A)) joy &= ~JOYPAD_A;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_B)) joy &= ~JOYPAD_B;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_SELECT)) joy &= ~JOYPAD_SELECT;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_START)) joy &= ~JOYPAD_START;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_RIGHT)) joy &= ~JOYPAD_RIGHT;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_LEFT)) joy &= ~JOYPAD_LEFT;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_UP)) joy &= ~JOYPAD_UP;
    if (PRESSED(RETRO_DEVICE_ID_JOYPAD_DOWN)) joy &= ~JOYPAD_DOWN;
#undef PRESSED
  }
  gb.direct.joypad = (uint8_t)joy;

  gb_run_frame(&gb);

  if (video_cb)
    video_cb(fb, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH * sizeof(uint16_t));
  if (audio_batch_cb) {
    audio_sample_t samples[AUDIO_SAMPLES_TOTAL];
    minigb_apu_audio_callback(&apu, samples);
    audio_batch_cb(samples, AUDIO_SAMPLES);
  }
}

static void gbc_reset(void) {
  if (!loaded)
    return;
  gb_reset(&gb);
  minigb_apu_audio_init(&apu);
}

/* Save state: header, struct gb_s, cart RAM. */
typedef struct {
  char magic[4]; /* "PGBS" */
  uint32_t version;
  uint32_t gb_size;
  uint32_t rom_size;
  uint32_t ram_size;
} gb_state_header_t;

#define GB_STATE_VERSION 2u

static size_t gbc_state_bytes(void) {
  return sizeof(gb) + cart_ram_size + sizeof(apu);
}

static size_t gbc_serialize_size(void) {
  if (!loaded)
    return 0;
  return sizeof(gb_state_header_t) + gbc_state_bytes();
}

static bool gbc_serialize(void *data, size_t size) {
  gb_state_header_t h;
  uint8_t *p = data;

  if (!loaded || size < gbc_serialize_size())
    return false;
  memcpy(h.magic, "PGBS", 4);
  h.version = GB_STATE_VERSION;
  h.gb_size = (uint32_t)sizeof(gb);
  h.rom_size = (uint32_t)rom_size;
  h.ram_size = (uint32_t)cart_ram_size;
  memcpy(p, &h, sizeof(h));
  memcpy(p + sizeof(h), &gb, sizeof(gb));
  if (cart_ram_size)
    memcpy(p + sizeof(h) + sizeof(gb), cart_ram, cart_ram_size);
  memcpy(p + sizeof(h) + sizeof(gb) + cart_ram_size, &apu, sizeof(apu));
  return true;
}

static bool gbc_unserialize(const void *data, size_t size) {
  gb_state_header_t h;
  const uint8_t *p = data;

  if (!loaded || size < sizeof(h))
    return false;
  memcpy(&h, p, sizeof(h));
  if (memcmp(h.magic, "PGBS", 4) != 0 || h.version != GB_STATE_VERSION ||
      h.gb_size != sizeof(gb) || h.rom_size != rom_size ||
      h.ram_size != cart_ram_size)
    return false;
  if (size < sizeof(h) + gbc_state_bytes())
    return false;
  memcpy(&gb, p + sizeof(h), sizeof(gb));
  if (cart_ram_size)
    memcpy(cart_ram, p + sizeof(h) + sizeof(gb), cart_ram_size);
  memcpy(&apu, p + sizeof(h) + sizeof(gb) + cart_ram_size, sizeof(apu));
  fix_pointers();
  return true;
}

static void *gbc_get_memory_data(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM)
    return cart_ram;
  return NULL;
}

static size_t gbc_get_memory_size(unsigned id) {
  if (id == RETRO_MEMORY_SAVE_RAM)
    return cart_ram_size;
  return 0;
}

const ra_core_t ra_core_gb = {
  .library_name = "Peanut-GB",
  .library_version = "head",
  .valid_extensions = "gb|dmg",
  .set_callbacks = gbc_set_callbacks,
  .init = gbc_init,
  .deinit = gbc_deinit,
  .load_game = gbc_load_game,
  .unload_game = gbc_unload_game,
  .get_system_av_info = gbc_get_system_av_info,
  .run = gbc_run,
  .reset = gbc_reset,
  .serialize_size = gbc_serialize_size,
  .serialize = gbc_serialize,
  .unserialize = gbc_unserialize,
  .get_memory_data = gbc_get_memory_data,
  .get_memory_size = gbc_get_memory_size,
};
