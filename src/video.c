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

/* One framebuffer column, packed BGR, so the copy into VRAM can be word
 * stores. A byte store to this RAM costs a full memory round trip; a word
 * moves four bytes for the same price. Aurora's own screen fill does this. */
static uint8_t colbuf[SCREEN_HEIGHT * 3 + 4] __attribute__((aligned(4)));

static void store_run(volatile uint8_t *d, const uint8_t *s, unsigned n) {
  if ((((uintptr_t)d ^ (uintptr_t)s) & 3u) != 0u) {
    while (n--)
      *d++ = *s++;
    return;
  }
  while (n && ((uintptr_t)d & 3u)) {
    *d++ = *s++;
    n--;
  }
  {
    volatile uint32_t *dw = (volatile uint32_t *)d;
    const uint32_t *sw = (const uint32_t *)s;
    unsigned words = n >> 2;
    unsigned i;
    for (i = 0; i < words; i++)
      dw[i] = sw[i];
    d += words << 2;
    s += words << 2;
    n &= 3u;
  }
  while (n--)
    *d++ = *s++;
}

/* Four BGR pixels packed into three words, framebuffer order. */
static void pack4(uint8_t *d, uint8_t b0, uint8_t g0, uint8_t r0, uint8_t b1,
                  uint8_t g1, uint8_t r1, uint8_t b2, uint8_t g2, uint8_t r2,
                  uint8_t b3, uint8_t g3, uint8_t r3) {
  uint32_t w0 = (uint32_t)b0 | ((uint32_t)g0 << 8) | ((uint32_t)r0 << 16) |
                ((uint32_t)b1 << 24);
  uint32_t w1 = (uint32_t)g1 | ((uint32_t)r1 << 8) | ((uint32_t)b2 << 16) |
                ((uint32_t)g2 << 24);
  uint32_t w2 = (uint32_t)r2 | ((uint32_t)b3 << 8) | ((uint32_t)g3 << 16) |
                ((uint32_t)r3 << 24);
  ((uint32_t *)d)[0] = w0;
  ((uint32_t *)d)[1] = w1;
  ((uint32_t *)d)[2] = w2;
}

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
  case RA_SCALE_FIT:
    /* The larger size that still fits, compared without floats. */
    if (SCREEN_TOP_WIDTH * h <= SCREEN_HEIGHT * w) {
      dst_w = SCREEN_TOP_WIDTH;
      dst_h = h * SCREEN_TOP_WIDTH / w;
    } else {
      dst_h = SCREEN_HEIGHT;
      dst_w = w * SCREEN_HEIGHT / h;
    }
    break;
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
  int x;

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
    /* Increasing address is decreasing y, so the run starts at the bottom. */
    int y_bottom = dst_y + dst_h - 1;
    uint8_t *run = fb + (((dst_x + x) * SCREEN_HEIGHT) +
                         (SCREEN_HEIGHT - 1 - y_bottom)) *
                            3;
    uint8_t *d = colbuf;
    int sx = lut_sx[x];
    int y = dst_h - 1;

    while (y >= 3) {
      uint8_t b[4], g[4], rch[4];
      int k;
      for (k = 0; k < 4; k++) {
        int sy = lut_sy[y - k];
        if (src_fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
          uint32_t c = ((const uint32_t *)(src + sy * pitch))[sx];
          b[k] = (uint8_t)c;
          g[k] = (uint8_t)(c >> 8);
          rch[k] = (uint8_t)(c >> 16);
        } else if (src_fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
          uint16_t c = ((const uint16_t *)(src + sy * pitch))[sx];
          b[k] = (uint8_t)((c & 0x1F) << 3);
          g[k] = (uint8_t)(((c >> 5) & 0x1F) << 3);
          rch[k] = (uint8_t)(((c >> 10) & 0x1F) << 3);
        } else {
          uint16_t c = ((const uint16_t *)(src + sy * pitch))[sx];
          b[k] = (uint8_t)((c & 0x1F) << 3);
          g[k] = (uint8_t)((c >> 3) & 0xFC);
          rch[k] = (uint8_t)((c >> 8) & 0xF8);
        }
      }
      pack4(d, b[0], g[0], rch[0], b[1], g[1], rch[1], b[2], g[2], rch[2],
            b[3], g[3], rch[3]);
      d += 12;
      y -= 4;
    }
    while (y >= 0) {
      uint8_t b, g, rch;
      int sy = lut_sy[y];
      if (src_fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        uint32_t c = ((const uint32_t *)(src + sy * pitch))[sx];
        b = (uint8_t)c;
        g = (uint8_t)(c >> 8);
        rch = (uint8_t)(c >> 16);
      } else if (src_fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
        uint16_t c = ((const uint16_t *)(src + sy * pitch))[sx];
        b = (uint8_t)((c & 0x1F) << 3);
        g = (uint8_t)(((c >> 5) & 0x1F) << 3);
        rch = (uint8_t)(((c >> 10) & 0x1F) << 3);
      } else {
        uint16_t c = ((const uint16_t *)(src + sy * pitch))[sx];
        b = (uint8_t)((c & 0x1F) << 3);
        g = (uint8_t)((c >> 3) & 0xFC);
        rch = (uint8_t)((c >> 8) & 0xF8);
      }
      *d++ = b;
      *d++ = g;
      *d++ = rch;
      y--;
    }
    store_run(run, colbuf, (unsigned)dst_h * 3u);
  }

  gfx_mark(SCREEN_TOP);
  frame_drawn = true;
}
