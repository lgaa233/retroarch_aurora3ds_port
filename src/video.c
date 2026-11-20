/* The video driver: the core's frame to the top screen.
 *
 * Aurora's off-screen framebuffer is 3 bytes per pixel, B G R, stored by
 * column and rotated: pixel (x, y) lives at ((x * 240) + (239 - y)) * 3. So
 * the blit walks destination columns; within one column consecutive y are
 * consecutive (descending) bytes, which keeps the inner loop tight. Scaling
 * is nearest-neighbour through two small lookup tables, rebuilt only when
 * the geometry, the scale mode or the pixel format changes. Everything is
 * integer: the ARM9 has no FPU.
 */
#include "retroarch.h"

#include <aurora_app.h>
#include <string.h>

static enum retro_pixel_format src_fmt = RETRO_PIXEL_FORMAT_0RGB1555;

static int lut_w, lut_h, lut_mode; /* geometry the tables were built for */
static uint16_t lut_sx[SCREEN_TOP_WIDTH];
static uint16_t lut_sy[SCREEN_HEIGHT];
static int dst_x, dst_y, dst_w, dst_h;
static bool borders_dirty = true;
static int skip_count;
static bool frame_drawn;
static bool skip_next;

void ra_video_init(void) {
  lut_w = lut_h = 0;
  lut_mode = -1;
  borders_dirty = true;
  skip_count = 0;
}

void ra_video_set_pixel_format(enum retro_pixel_format fmt) { src_fmt = fmt; }

void ra_video_invalidate(void) { borders_dirty = true; }

bool ra_video_frame_drawn(void) { return frame_drawn; }

void ra_video_skip_next(void) { skip_next = true; }

bool ra_video_will_skip(void) { return skip_next; }

static void build_luts(int w, int h, int mode) {
  int x, y;

  switch (mode) {
  case RA_SCALE_FULL:
    dst_w = SCREEN_TOP_WIDTH;
    dst_h = SCREEN_HEIGHT;
    break;
  case RA_SCALE_FIT: {
    /* Largest integer multiple that fits. Fractional upscaling painted
     * several times more pixels than the core produced and pushed games
     * under 60 fps, which slowed the sound with them. */
    int s = 1;
    while ((s + 1) * w <= SCREEN_TOP_WIDTH && (s + 1) * h <= SCREEN_HEIGHT)
      s++;
    dst_w = w * s;
    dst_h = h * s;
    break;
  }
  case RA_SCALE_1X:
  default:
    dst_w = w < SCREEN_TOP_WIDTH ? w : SCREEN_TOP_WIDTH;
    dst_h = h < SCREEN_HEIGHT ? h : SCREEN_HEIGHT;
    break;
  }
  if (dst_w < 1) dst_w = 1;
  if (dst_h < 1) dst_h = 1;
  dst_x = (SCREEN_TOP_WIDTH - dst_w) / 2;
  dst_y = (SCREEN_HEIGHT - dst_h) / 2;

  for (x = 0; x < dst_w; x++)
    lut_sx[x] = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)dst_w);
  for (y = 0; y < dst_h; y++)
    lut_sy[y] = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)dst_h);

  lut_w = w;
  lut_h = h;
  lut_mode = mode;
  borders_dirty = true;
}

static void paint_borders(void) {
  gfx_clear(SCREEN_TOP, COLOR_BLACK);
  borders_dirty = false;
}

void ra_video_submit(const void *data, unsigned width, unsigned height,
                     size_t pitch) {
  uint8_t *fb;
  const uint8_t *src = data;
  int x, y;

  frame_drawn = false;
  if (skip_next) {
    skip_next = false;
    return;
  }
  if (!data || !width || !height)
    return; /* a duped frame: keep what is on screen */

  /* Frameskip: present 1 frame out of (frameskip + 1). */
  if (ra_config.frameskip > 0) {
    if (skip_count > 0) {
      skip_count--;
      return;
    }
    skip_count = ra_config.frameskip;
  }

  if ((int)width != lut_w || (int)height != lut_h ||
      ra_config.scale != lut_mode)
    build_luts((int)width, (int)height, ra_config.scale);
  if (borders_dirty)
    paint_borders();

  fb = gfx_framebuffer(SCREEN_TOP);

  for (x = 0; x < dst_w; x++) {
    /* Column (dst_x + x): y increasing means the address decreasing. */
    uint8_t *dst = fb + (((dst_x + x) * SCREEN_HEIGHT) +
                         (SCREEN_HEIGHT - 1 - dst_y)) * 3;
    int sx = lut_sx[x];

    switch (src_fmt) {
    case RETRO_PIXEL_FORMAT_RGB565: {
      for (y = 0; y < dst_h; y++) {
        const uint16_t *row = (const uint16_t *)(src + lut_sy[y] * pitch);
        uint16_t c = row[sx];
        dst[0] = (uint8_t)((c & 0x1F) << 3);         /* B */
        dst[1] = (uint8_t)((c >> 3) & 0xFC);         /* G */
        dst[2] = (uint8_t)((c >> 8) & 0xF8);         /* R */
        dst -= 3;
      }
      break;
    }
    case RETRO_PIXEL_FORMAT_XRGB8888: {
      for (y = 0; y < dst_h; y++) {
        const uint32_t *row = (const uint32_t *)(src + lut_sy[y] * pitch);
        uint32_t c = row[sx];
        dst[0] = (uint8_t)(c & 0xFF);         /* B */
        dst[1] = (uint8_t)((c >> 8) & 0xFF);  /* G */
        dst[2] = (uint8_t)((c >> 16) & 0xFF); /* R */
        dst -= 3;
      }
      break;
    }
    case RETRO_PIXEL_FORMAT_0RGB1555:
    default: {
      for (y = 0; y < dst_h; y++) {
        const uint16_t *row = (const uint16_t *)(src + lut_sy[y] * pitch);
        uint16_t c = row[sx];
        dst[0] = (uint8_t)((c & 0x1F) << 3);        /* B */
        dst[1] = (uint8_t)(((c >> 5) & 0x1F) << 3); /* G */
        dst[2] = (uint8_t)(((c >> 10) & 0x1F) << 3);/* R */
        dst -= 3;
      }
      break;
    }
    }
  }

  gfx_mark(SCREEN_TOP);
  frame_drawn = true;
}
