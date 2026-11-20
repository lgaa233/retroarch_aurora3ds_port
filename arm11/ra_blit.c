/* The frame scaler (ra_blit.h). Built twice: into the ARM11 job blob, and into
 * src/video.c for the ARM9 when the ARM11 cannot take the job.
 *
 * It works in bands of source rows small enough for the data cache (4 KB on
 * the ARM9, 16 KB on the ARM11), and within a band one picture column at a
 * time, because a framebuffer column is contiguous. A picture column that
 * shows the same source column as the one before it is a copy of that one.
 * Four pixels go out as three word stores. */
#include "ra_blit.h"

#define COL_BYTES (RA_BLIT_FB_H * 3)

/* 1: 5 and 6 bit channels widen to the full 0..255 range. The ARM9 build
 * shifts only, which is cheaper and slightly darker. */
#ifndef RA_BLIT_FULL_EXPAND
#define RA_BLIT_FULL_EXPAND 1
#endif

#define INLINE static inline __attribute__((always_inline))

INLINE uint32_t conv(const uint8_t *row, uint32_t sx, uint32_t fmt) {
  uint32_t p;
  if (fmt == RA_BLIT_FMT_XRGB8888)
    return ((const uint32_t *)row)[sx] & 0xFFFFFFu;
  p = ((const uint16_t *)row)[sx];
  if (fmt == RA_BLIT_FMT_RGB565) {
#if RA_BLIT_FULL_EXPAND
    uint32_t b = p & 31u, g = (p >> 5) & 63u, r = p >> 11;
    return ((b << 3) | (b >> 2)) | (((g << 2) | (g >> 4)) << 8) |
           (((r << 3) | (r >> 2)) << 16);
#else
    return ((p & 0x1Fu) << 3) | ((p & 0x7E0u) << 5) | ((p & 0xF800u) << 8);
#endif
  }
  {
    uint32_t b = p & 31u, g = (p >> 5) & 31u, r = (p >> 10) & 31u;
#if RA_BLIT_FULL_EXPAND
    return ((b << 3) | (b >> 2)) | (((g << 3) | (g >> 2)) << 8) |
           (((r << 3) | (r >> 2)) << 16);
#else
    return (b << 3) | (g << 11) | (r << 19);
#endif
  }
}

INLINE void put3(uint8_t *d, uint32_t c) {
  d[0] = (uint8_t)c;
  d[1] = (uint8_t)(c >> 8);
  d[2] = (uint8_t)(c >> 16);
}

/* The ARM9 allocates a cache line on a read miss but not on a write miss, so a
 * plain store goes all the way to FCRAM. Reading the line first makes the
 * stores that follow hit the cache. */
static void touch(const uint8_t *p, uint32_t n) {
  uintptr_t a = (uintptr_t)p & ~(uintptr_t)31u;
  uintptr_t e = (uintptr_t)p + n;
  for (; a < e; a += 32u)
    (void)*(const volatile uint32_t *)a;
}

static void copy_run(uint8_t *d, const uint8_t *s, uint32_t n) {
  uint32_t *dw;
  const uint32_t *sw;
  while (n && ((uintptr_t)d & 3u)) {
    *d++ = *s++;
    n--;
  }
  dw = (uint32_t *)d;
  sw = (const uint32_t *)s;
  while (n >= 16u) {
    uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
    dw[0] = a;
    dw[1] = b;
    dw[2] = c;
    dw[3] = e;
    dw += 4;
    sw += 4;
    n -= 16u;
  }
  while (n >= 4u) {
    *dw++ = *sw++;
    n -= 4u;
  }
  d = (uint8_t *)dw;
  s = (const uint8_t *)sw;
  while (n--)
    *d++ = *s++;
}

static void zero_run(uint8_t *d, uint32_t n) {
  uint32_t *dw;
  while (n && ((uintptr_t)d & 3u)) {
    *d++ = 0;
    n--;
  }
  dw = (uint32_t *)d;
  while (n >= 16u) {
    dw[0] = 0;
    dw[1] = 0;
    dw[2] = 0;
    dw[3] = 0;
    dw += 4;
    n -= 16u;
  }
  while (n >= 4u) {
    *dw++ = 0;
    n -= 4u;
  }
  d = (uint8_t *)dw;
  while (n--)
    *d++ = 0;
}

typedef struct {
  const uint8_t *src;
  uint32_t pitch, sx;
  const uint16_t *lut_sy;
  int ybase; /* picture row = ybase - k */
  uint32_t last_sy, last;
} col_src_t;

INLINE uint32_t pix(col_src_t *c, int k, uint32_t fmt) {
  uint32_t sy = c->lut_sy[c->ybase - k];
  if (sy != c->last_sy) {
    c->last_sy = sy;
    c->last = conv(c->src + sy * c->pitch, c->sx, fmt);
  }
  return c->last;
}

/* Rows k in [ka, kb) of one column, k being the pixel's place in the column. */
INLINE void pack_run(uint8_t *col, int ka, int kb, col_src_t *c,
                     uint32_t fmt) {
  int k = ka;
  uint32_t *w;
  while (k < kb && (k & 3)) {
    put3(col + 3 * k, pix(c, k, fmt));
    k++;
  }
  w = (uint32_t *)(col + 3 * k);
  for (; k + 4 <= kb; k += 4) {
    uint32_t p0 = pix(c, k, fmt);
    uint32_t p1 = pix(c, k + 1, fmt);
    uint32_t p2 = pix(c, k + 2, fmt);
    uint32_t p3 = pix(c, k + 3, fmt);
    w[0] = p0 | (p1 << 24);
    w[1] = (p1 >> 8) | (p2 << 16);
    w[2] = (p2 >> 16) | (p3 << 8);
    w += 3;
  }
  while (k < kb) {
    put3(col + 3 * k, pix(c, k, fmt));
    k++;
  }
}

INLINE void band(const ra_blit_t *b, int ka, int kb, uint32_t fmt) {
  uint8_t *fb = (uint8_t *)b->dst;
  const uint16_t *lut_sx = (const uint16_t *)b->lut_sx;
  uint32_t n = (uint32_t)(kb - ka) * 3u;
  col_src_t c;
  int x;

  c.src = (const uint8_t *)b->src;
  c.pitch = b->src_pitch;
  c.lut_sy = (const uint16_t *)b->lut_sy;
  c.ybase = RA_BLIT_FB_H - 1 - b->dst_y;
  for (x = 0; x < b->dst_w; x++) {
    uint8_t *col = fb + (uint32_t)(b->dst_x + x) * COL_BYTES;
    if (b->touch)
      touch(col + 3 * ka, n);
    if (x > 0 && lut_sx[x] == lut_sx[x - 1]) {
      copy_run(col + 3 * ka, col - COL_BYTES + 3 * ka, n);
      continue;
    }
    c.sx = lut_sx[x];
    c.last_sy = 0xFFFFFFFFu;
    c.last = 0;
    pack_run(col, ka, kb, &c, fmt);
  }
}

static void clear_around(const ra_blit_t *b, int k0, int k1) {
  uint8_t *fb = (uint8_t *)b->dst;
  int x;
  for (x = 0; x < RA_BLIT_FB_W; x++) {
    uint8_t *col = fb + (uint32_t)x * COL_BYTES;
    if (x < b->dst_x || x >= b->dst_x + b->dst_w) {
      if (b->touch)
        touch(col, COL_BYTES);
      zero_run(col, COL_BYTES);
      continue;
    }
    if (k0 > 0) {
      if (b->touch)
        touch(col, (uint32_t)k0 * 3u);
      zero_run(col, (uint32_t)k0 * 3u);
    }
    if (k1 < RA_BLIT_FB_H) {
      if (b->touch)
        touch(col + 3 * k1, (uint32_t)(RA_BLIT_FB_H - k1) * 3u);
      zero_run(col + 3 * k1, (uint32_t)(RA_BLIT_FB_H - k1) * 3u);
    }
  }
}

void ra_blit_run(const ra_blit_t *b) {
  const uint16_t *lut_sy = (const uint16_t *)b->lut_sy;
  /* The picture's bottom row is first in each column. */
  int k0 = RA_BLIT_FB_H - b->dst_y - b->dst_h;
  int k1 = k0 + b->dst_h;
  int ybase = RA_BLIT_FB_H - 1 - b->dst_y;
  uint32_t rows = b->band_rows ? b->band_rows : 0xFFFFu;
  int ka = k0;

  if (b->dst_w <= 0 || b->dst_h <= 0 || k0 < 0 || k1 > RA_BLIT_FB_H ||
      b->dst_x < 0 || b->dst_x + b->dst_w > RA_BLIT_FB_W)
    return;
  if (b->clear)
    clear_around(b, k0, k1);

  while (ka < k1) {
    uint32_t sy_top = lut_sy[ybase - ka];
    int kb = ka + 1;
    while (kb < k1 && sy_top - lut_sy[ybase - kb] < rows)
      kb++;
    /* A band that ends on a 4-pixel boundary needs no byte stores there. */
    if (kb < k1 && (kb & ~3) > ka)
      kb &= ~3;
    switch (b->src_fmt) {
    case RA_BLIT_FMT_RGB565:
      band(b, ka, kb, RA_BLIT_FMT_RGB565);
      break;
    case RA_BLIT_FMT_XRGB8888:
      band(b, ka, kb, RA_BLIT_FMT_XRGB8888);
      break;
    default:
      band(b, ka, kb, RA_BLIT_FMT_0RGB1555);
      break;
    }
    ka = kb;
  }
}
