/* Host test for the frame scaler (arm11/ra_blit.c): every mode, format and
 * band size against a pixel-by-pixel reference. Built twice by `make test`,
 * with the ARM11's full colour expansion and with the ARM9's shifts. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ra_blit.c"

static int failures;

static uint32_t ref_conv(const uint8_t *row, unsigned sx, uint32_t fmt) {
  uint32_t p;
  if (fmt == RA_BLIT_FMT_XRGB8888)
    return ((const uint32_t *)row)[sx] & 0xFFFFFFu;
  p = ((const uint16_t *)row)[sx];
  if (fmt == RA_BLIT_FMT_RGB565) {
    uint32_t b = p & 31, g = (p >> 5) & 63, r = p >> 11;
#if RA_BLIT_FULL_EXPAND
    b = (b << 3) | (b >> 2);
    g = (g << 2) | (g >> 4);
    r = (r << 3) | (r >> 2);
#else
    b <<= 3;
    g <<= 2;
    r <<= 3;
#endif
    return b | (g << 8) | (r << 16);
  } else {
    uint32_t b = p & 31, g = (p >> 5) & 31, r = (p >> 10) & 31;
#if RA_BLIT_FULL_EXPAND
    b = (b << 3) | (b >> 2);
    g = (g << 3) | (g >> 2);
    r = (r << 3) | (r >> 2);
#else
    b <<= 3;
    g <<= 3;
    r <<= 3;
#endif
    return b | (g << 8) | (r << 16);
  }
}

/* The same geometry rules as src/video.c. */
static void geometry(int w, int h, int mode, int *dx, int *dy, int *dw,
                     int *dh) {
  if (mode == 2) {
    *dw = 400;
    *dh = 240;
  } else if (mode == 1) {
    if (400 * h <= 240 * w) {
      *dw = 400;
      *dh = h * 400 / w;
    } else {
      *dh = 240;
      *dw = w * 240 / h;
    }
  } else {
    *dw = w < 400 ? w : 400;
    *dh = h < 240 ? h : 240;
  }
  *dx = (400 - *dw) / 2;
  *dy = (240 - *dh) / 2;
}

static void run_case(int w, int h, uint32_t fmt, int mode, uint32_t band,
                     int clear, int touch_lines) {
  static uint8_t fb[400 * 240 * 3];
  static uint16_t lsx[400], lsy[240];
  unsigned bpp = fmt == RA_BLIT_FMT_XRGB8888 ? 4 : 2;
  size_t pitch = (size_t)w * bpp + 8; /* a pitch wider than the picture */
  uint8_t *src = malloc(pitch * (size_t)h);
  ra_blit_t b;
  int dx, dy, dw, dh, x, y, bad = 0;
  char name[128];

  for (size_t i = 0; i < pitch * (size_t)h; i++)
    src[i] = (uint8_t)(rand() >> 7);
  geometry(w, h, mode, &dx, &dy, &dw, &dh);
  for (x = 0; x < dw; x++)
    lsx[x] = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)dw);
  for (y = 0; y < dh; y++)
    lsy[y] = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)dh);
  memset(fb, 0xA5, sizeof(fb));

  memset(&b, 0, sizeof(b));
  b.src = (uintptr_t)src;
  b.dst = (uintptr_t)fb;
  b.lut_sx = (uintptr_t)lsx;
  b.lut_sy = (uintptr_t)lsy;
  b.src_pitch = (uint32_t)pitch;
  b.src_fmt = fmt;
  b.dst_x = dx;
  b.dst_y = dy;
  b.dst_w = dw;
  b.dst_h = dh;
  b.clear = (uint32_t)clear;
  b.band_rows = band;
  b.touch = (uint32_t)touch_lines;
  ra_blit_run(&b);

  for (x = 0; x < 400 && !bad; x++) {
    for (y = 0; y < 240; y++) {
      const uint8_t *p = fb + ((x * 240) + (239 - y)) * 3;
      uint32_t got = p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16);
      uint32_t want;
      int in = x >= dx && x < dx + dw && y >= dy && y < dy + dh;
      if (in)
        want = ref_conv(src + (size_t)lsy[y - dy] * pitch, lsx[x - dx], fmt);
      else
        want = clear ? 0u : 0xA5A5A5u;
      if (got != want) {
        printf("     at (%d,%d) got %06X want %06X\n", x, y, (unsigned)got,
               (unsigned)want);
        bad = 1;
        break;
      }
    }
  }
  snprintf(name, sizeof(name), "%dx%d fmt %u mode %d band %u%s%s", w, h,
           (unsigned)fmt, mode, (unsigned)band, clear ? " clear" : "",
           touch_lines ? " touch" : "");
  printf("%s %s\n", bad ? "FAIL" : "ok  ", name);
  failures += bad;
  free(src);
}

int main(void) {
  static const int sizes[][2] = {{160, 144}, {256, 240}, {256, 192},
                                 {64, 32},   {256, 224}, {320, 240},
                                 {512, 448}, {7, 5}};
  static const uint32_t fmts[] = {RA_BLIT_FMT_RGB565, RA_BLIT_FMT_0RGB1555,
                                  RA_BLIT_FMT_XRGB8888};
  static const uint32_t bands[] = {0, 40, 1, 3};
  unsigned s, f, m, bi;

  srand(1);
  printf("--- scaler (%s) ---\n",
         RA_BLIT_FULL_EXPAND ? "ARM11 colours" : "ARM9 colours");
  for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
    for (f = 0; f < 3; f++)
      for (m = 0; m < 3; m++)
        for (bi = 0; bi < 4; bi++)
          run_case(sizes[s][0], sizes[s][1], fmts[f], (int)m, bands[bi],
                   (int)((s + bi) & 1), (int)(bi & 1));
  printf("\n%s\n", failures ? "scaler tests FAILED" : "all scaler tests passed");
  return failures ? 1 : 0;
}
