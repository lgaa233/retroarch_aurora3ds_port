/* The video driver: the core's frame to the top screen.
 *
 * The scaling itself is arm11/ra_blit.c. Where AuroraOS's ARM11 core takes
 * AUDIO_CMD_RUN11 (core 116 and later) it runs there, at 268 MHz or more with
 * write-allocating caches; the ARM9 waits for it. Writing a whole 400x240
 * screen from the ARM9 costs about 11 ms of FCRAM stores (docs/gpu.md in
 * AuroraOS measures 9 ms for 320x240), which is most of a frame, so the ARM9
 * only does it when the ARM11 cannot. Everything is integer: the ARM9 has no
 * FPU. */
#include "retroarch.h"

#include <aurora_app.h>
#include <string.h>

#include "blit11.h"
#include "blit11_blob.h"

#define RA_BLIT_FULL_EXPAND 0
#include "ra_blit.c"

/* The SDK runtime's (sdk/runtime/app_main.c), not in aurora_app.h. */
extern bool app_core(void);
extern uint32_t audio_version(void);
extern void app_core_post(uint32_t cmd, uint32_t a0, uint32_t a1, uint32_t a2,
                          uint32_t a3);

#define AUDIO_CMD_RUN11 13u
#define AUDIO_RUN11_VERSION 116u

/* Rows of source per pass on the ARM9: 40 lines of a 4 KB data cache leave
 * room for the framebuffer lines being written. The ARM11 has 16 KB. */
#define BAND_ARM9 40u
#define BAND_ARM11 0u

static enum retro_pixel_format src_fmt = RETRO_PIXEL_FORMAT_0RGB1555;

static int lut_w, lut_h, lut_mode; /* geometry the tables were built for */
static uint16_t lut_sx[SCREEN_TOP_WIDTH] __attribute__((aligned(32)));
static uint16_t lut_sy[SCREEN_HEIGHT] __attribute__((aligned(32)));
static int dst_x, dst_y, dst_w, dst_h;
static bool borders_dirty = true;
static int skip_count;
static bool frame_drawn;
static bool skip_next;

static int arm11_state; /* 0 not tried yet, 1 working, -1 not available */
static uint32_t arm11_ticket;
static uint32_t blit_us; /* the last frame's scaling time */

/* Clean and invalidate the whole ARM9 data cache by index (4 KB, 4 ways,
 * 32-byte lines), then drain the write buffer: the ARM11 then reads the frame
 * from memory, and the ARM9 holds no stale copy of what it writes. */
static void dcache_flush(void) {
  uint32_t set, way;
  for (set = 0; set < 0x400u; set += 0x20u)
    for (way = 0; way < 4u; way++)
      __asm__ volatile("mcr p15, 0, %0, c7, c14, 2" ::"r"((way << 30) | set)
                       : "memory");
  __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(0) : "memory");
}

static void inval_line(const volatile void *p) {
  __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" ::"r"(p) : "memory");
}

static bool arm11_ready(void) {
  if (arm11_state)
    return arm11_state > 0;
  arm11_state = -1;
  if (!app_core() || audio_version() < AUDIO_RUN11_VERSION ||
      blit11_blob_size > RA_BLIT11_CODE_MAX)
    return false;
  memcpy((void *)(uintptr_t)RA_BLIT11_CODE, blit11_blob, blit11_blob_size);
  arm11_state = 1;
  return true;
}

/* False when the job did not run; the ARM9 then does it. */
static bool arm11_blit(ra_blit_t *job) {
  volatile ra_blit_t *p = (volatile ra_blit_t *)(uintptr_t)RA_BLIT11_PARAMS;
  if (!arm11_ready())
    return false;
  job->ticket = ++arm11_ticket;
  job->done = 0;
  memcpy((void *)(uintptr_t)RA_BLIT11_PARAMS, job, sizeof(*job));
  dcache_flush();
  app_core_post(AUDIO_CMD_RUN11, RA_BLIT11_CODE, RA_BLIT11_PARAMS, 0, 0);
  inval_line(&p->done);
  if (p->done != job->ticket) {
    /* Refused, or a core that does not know the command: not again. */
    arm11_state = -1;
    return false;
  }
  return true;
}

const char *ra_video_path(void) {
  if (arm11_state > 0)
    return "ARM11";
  return arm11_state < 0 ? "ARM9" : "-";
}

uint32_t ra_video_blit_us(void) { return blit_us; }

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

void ra_video_submit(const void *data, unsigned width, unsigned height,
                     size_t pitch) {
  ra_blit_t job;
  uint64_t t0;

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

  memset(&job, 0, sizeof(job));
  job.src = (uintptr_t)data;
  /* Waits for a present still reading the backbuffer. */
  job.dst = (uintptr_t)gfx_framebuffer(SCREEN_TOP);
  job.lut_sx = (uintptr_t)lut_sx;
  job.lut_sy = (uintptr_t)lut_sy;
  job.src_pitch = (uint32_t)pitch;
  job.src_fmt = src_fmt == RETRO_PIXEL_FORMAT_RGB565     ? RA_BLIT_FMT_RGB565
                : src_fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? RA_BLIT_FMT_XRGB8888
                                                         : RA_BLIT_FMT_0RGB1555;
  job.dst_x = dst_x;
  job.dst_y = dst_y;
  job.dst_w = dst_w;
  job.dst_h = dst_h;
  job.clear = borders_dirty ? 1u : 0u;

  t0 = sys_micros();
  job.band_rows = BAND_ARM11;
  if (!arm11_blit(&job)) {
    job.band_rows = BAND_ARM9;
    job.touch = 1;
    ra_blit_run(&job);
  }
  blit_us = (uint32_t)(sys_micros() - t0);
  borders_dirty = false;

  gfx_mark(SCREEN_TOP);
  frame_drawn = true;
}
