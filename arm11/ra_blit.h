/* The scaler that puts a core's frame on the top screen, shared by the ARM11
 * job (blit11.c, run by AuroraOS's core through AUDIO_CMD_RUN11) and the ARM9
 * fallback in src/video.c. Freestanding C: no C library, no .bss, no floats.
 *
 * Aurora's framebuffer is 400x240, 3 bytes a pixel (B, G, R), stored by
 * column and rotated: pixel (x, y) is at ((x * 240) + (239 - y)) * 3. Scaling
 * is nearest-neighbour through two index tables the ARM9 builds. */
#ifndef RA_BLIT_H
#define RA_BLIT_H

#include <stdint.h>

#define RA_BLIT_FB_W 400
#define RA_BLIT_FB_H 240

enum {
  RA_BLIT_FMT_0RGB1555 = 0,
  RA_BLIT_FMT_XRGB8888 = 1,
  RA_BLIT_FMT_RGB565 = 2,
};

/* Pointers are 32 bits on both CPUs, so the ARM9 and the ARM11 agree on this
 * layout. */
typedef struct {
  uintptr_t src;    /* the core's frame */
  uintptr_t dst;    /* the framebuffer */
  uintptr_t lut_sx; /* uint16_t[dst_w]: source column of each picture column */
  uintptr_t lut_sy; /* uint16_t[dst_h]: source row of each picture row */
  uint32_t src_pitch;
  uint32_t src_fmt; /* RA_BLIT_FMT_* */
  int32_t dst_x, dst_y, dst_w, dst_h;
  uint32_t clear;     /* paint everything around the picture black as well */
  uint32_t band_rows; /* source rows per pass, sized to the data cache */
  uint32_t touch;     /* read each line before writing it (ARM9 caches) */
  uint32_t ticket;    /* set by the ARM9 */
  uint32_t done;      /* the ARM11 job copies ticket here when finished */
} ra_blit_t;

void ra_blit_run(const ra_blit_t *b);

#endif
