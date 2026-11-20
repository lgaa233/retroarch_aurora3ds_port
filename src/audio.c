/* PCM playback through AuroraOS's ARM11 core.
 *
 * One looping CSND voice plays a ring in the PCM pool; the ARM9 writes ahead
 * of where it plays. The core's AUDIO_CMD_VOICE gives a voice the same volume
 * on both speakers, so the cores' stereo is mixed to mono here: two voices
 * would both play on both sides, started a moment apart, and sound hollow.
 *
 * Where the voice is is not readable from the ARM9, so it is worked out from
 * the clock and the voice's real rate, which is the CSND clock over a whole
 * divider: 67027964 / 2045 = 32776.5 Hz, not the 32768 asked for. The cores
 * do not make sound at exactly that rate either (a frame is paced by the
 * screen, ~59.8 Hz), so the resampler's ratio follows how full the ring is,
 * within half a percent, as RetroArch's dynamic rate control does.
 *
 * Behind what is being written, the ring is kept silent: what played is
 * cleared, and every write ends in a short fade to zero. A frame that comes
 * late then plays silence, not a lap-old piece of the ring. */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdint.h>
#include <string.h>

#ifndef AUDIO_PCM_ADDR
#define AUDIO_PCM_ADDR 0x23400000u
#endif
#define AUDIO_CMD_VOICE 9u
#define AUDIO_CMD_VOICE_STOP 10u
#define AUDIO_VOICE_LOOP (1u << 8)
#define AUDIO_VOICE_PCM16 (1u << 9)
#define AUDIO_VOICE_VOL(v) (((v) & 0xFFFFu) << 16)

extern void app_core_post(uint32_t cmd, uint32_t a0, uint32_t a1, uint32_t a2,
                          uint32_t a3);
extern void os_dcache_clean_range(const void *addr, uint32_t len);

#define CSND_CLOCK 67027964u
#define RA_AUDIO_ASK 32768u /* the rate passed to the core */
#define RA_AUDIO_TIMER (CSND_CLOCK / RA_AUDIO_ASK) /* what it programs: 2045 */

#define RA_AUDIO_RING 4096u /* samples, 125 ms */
#define RA_AUDIO_VOICE 6u
#define RA_AUDIO_VOICE_VOL 0x8000u
/* Ring fill wanted just before a frame's samples go in. A frame brings ~550. */
#define RA_AUDIO_TARGET 640
#define RA_AUDIO_MAX 2600 /* more than this ahead: drop (fast-forward) */
#define RA_AUDIO_FADE 256u
/* The top screen refreshes at about 59.83 Hz and paces the frames, so a core
 * that runs at 60.10 Hz (NES) has its sound stretched by the same ratio. */
#define RA_AUDIO_SCREEN_MHZ 59830u
#define RA_AUDIO_BEHIND 64u /* cleared this far behind the play estimate */
#define RA_AUDIO_DRC_PPM 5000 /* at most 0.5% faster or slower */

static int16_t *ring = (int16_t *)(uintptr_t)AUDIO_PCM_ADDR;
static int playing;
static uint32_t written; /* samples written since the voice started */
static uint32_t cleared; /* everything before this is silence again */
static uint64_t t0;      /* when the voice started, in microseconds */
static uint32_t underruns;

/* Resampler, 16.16: phase between the previous input and the next. */
static uint32_t base_step; /* input samples per output sample at 0 ppm */
static uint32_t phase;
static int32_t prev_in;
static int32_t fill_avg; /* x16 */

/* Filters: DC blocker (~20 Hz) and a gentle low-pass (~10 kHz). */
static int32_t dc_x, dc_y, lp;
static int32_t last_out;
static uint32_t fade_in; /* samples left of the ramp back from silence */

static uint32_t played_now(void) {
  uint64_t us = sys_micros() - t0;
  return (uint32_t)((us * CSND_CLOCK) / (RA_AUDIO_TIMER * 1000000ull));
}

static void clean_span(uint32_t index, uint32_t count) {
  uint32_t first = index & (RA_AUDIO_RING - 1u);
  uint32_t n1 = count;
  if (!count)
    return;
  if (count > RA_AUDIO_RING)
    count = n1 = RA_AUDIO_RING;
  if (first + n1 > RA_AUDIO_RING)
    n1 = RA_AUDIO_RING - first;
  os_dcache_clean_range(ring + first, n1 * sizeof(int16_t));
  if (n1 < count)
    os_dcache_clean_range(ring, (count - n1) * sizeof(int16_t));
}

static void zero_span(uint32_t index, uint32_t count) {
  uint32_t i;
  if (count > RA_AUDIO_RING)
    count = RA_AUDIO_RING;
  for (i = 0; i < count; i++)
    ring[(index + i) & (RA_AUDIO_RING - 1u)] = 0;
  clean_span(index, count);
}

void ra_audio_stop(void) {
  if (!playing)
    return;
  app_core_post(AUDIO_CMD_VOICE_STOP, 1u << RA_AUDIO_VOICE, 0, 0, 0);
  playing = 0;
}

static void ra_audio_start(uint32_t in_rate) {
  uint32_t flags = AUDIO_VOICE_LOOP | AUDIO_VOICE_PCM16 |
                   AUDIO_VOICE_VOL(RA_AUDIO_VOICE_VOL);
  uint32_t fps_mhz = (uint32_t)(ra_content.av.timing.fps * 1000.0 + 0.5);

  ra_audio_stop();
  if (!snd_available())
    return;
  if (fps_mhz < 30000u || fps_mhz > 120000u)
    fps_mhz = RA_AUDIO_SCREEN_MHZ;
  /* in_rate * (screen / fps) * timer / CSND clock, 16.16 */
  base_step = (uint32_t)((((uint64_t)in_rate * RA_AUDIO_SCREEN_MHZ / fps_mhz) *
                          RA_AUDIO_TIMER << 16) /
                         CSND_CLOCK);
  memset(ring, 0, RA_AUDIO_RING * sizeof(int16_t));
  os_dcache_clean_range(ring, RA_AUDIO_RING * sizeof(int16_t));
  phase = 0;
  prev_in = 0;
  dc_x = dc_y = lp = 0;
  last_out = 0;
  fade_in = 0;
  fill_avg = RA_AUDIO_TARGET * 16;
  app_core_post(AUDIO_CMD_VOICE, RA_AUDIO_VOICE | flags,
                (uint32_t)(uintptr_t)ring, RA_AUDIO_RING * sizeof(int16_t),
                RA_AUDIO_ASK);
  /* The command is acknowledged once the channel has started. */
  t0 = sys_micros();
  written = RA_AUDIO_TARGET;
  cleared = written;
  playing = 1;
}

/* 0..10 from the menu, applied in 1/8 steps around unity at 8. */
static int32_t gain_q8(void) {
  int v = ra_config.volume;
  if (v < 0)
    v = 0;
  if (v > 10)
    v = 10;
  return (int32_t)v * 32;
}

static int16_t shape(int32_t x, int32_t gain) {
  int32_t y;
  /* y[n] = x[n] - x[n-1] + (255/256) y[n-1], in 24.8 */
  dc_y = ((x - dc_x) << 8) + dc_y - (dc_y >> 8);
  dc_x = x;
  y = dc_y >> 8;
  lp += ((y - lp) * 218) >> 8;
  y = (lp * gain) >> 8;
  if (y > 32767)
    y = 32767;
  if (y < -32768)
    y = -32768;
  return (int16_t)y;
}

size_t ra_audio_batch_cb(const int16_t *data, size_t frames) {
  uint32_t played, at, step;
  int32_t fill, adj, gain;
  size_t i;

  if (!data || !frames)
    return frames;
  if (!playing) {
    uint32_t in_rate = (uint32_t)(ra_content.av.timing.sample_rate + 0.5);
    if (in_rate < 8000u || in_rate > 96000u)
      return frames;
    ra_audio_start(in_rate);
    if (!playing)
      return frames;
  }

  played = played_now();
  fill = (int32_t)(written - played);
  if (fill < 0) {
    /* The voice ran past what was written (a slow frame): it has been
     * playing the faded tail and silence. Start again a little ahead. */
    underruns++;
    written = played + RA_AUDIO_TARGET / 2;
    fill = RA_AUDIO_TARGET / 2;
    fade_in = RA_AUDIO_FADE;
  } else if (fill > RA_AUDIO_MAX) {
    return frames;
  }

  /* What has played is cleared, so a late frame finds silence there. */
  if ((int32_t)(played - RA_AUDIO_BEHIND - cleared) > 0) {
    uint32_t n = played - RA_AUDIO_BEHIND - cleared;
    zero_span(cleared, n);
    cleared += n;
  }

  /* Dynamic rate control: a ring fuller than wanted plays the input a little
   * faster (fewer output samples), an emptier one a little slower. */
  fill_avg += fill - (fill_avg >> 4);
  adj = ((fill_avg >> 4) - RA_AUDIO_TARGET) * (RA_AUDIO_DRC_PPM / 8) /
        (RA_AUDIO_TARGET / 8);
  if (adj > RA_AUDIO_DRC_PPM)
    adj = RA_AUDIO_DRC_PPM;
  if (adj < -RA_AUDIO_DRC_PPM)
    adj = -RA_AUDIO_DRC_PPM;
  step = (uint32_t)(((uint64_t)base_step * (uint32_t)(1000000 + adj)) /
                    1000000u);

  gain = gain_q8();
  at = written;
  for (i = 0; i < frames; i++) {
    int32_t in = ((int32_t)data[i * 2] + (int32_t)data[i * 2 + 1]) >> 1;
    while (phase < 0x10000u) {
      int32_t s = prev_in + (((in - prev_in) * (int32_t)(phase >> 1)) >> 15);
      last_out = shape(s, gain);
      if (fade_in)
        last_out = (last_out * (int32_t)(RA_AUDIO_FADE - fade_in--)) >> 8;
      ring[written & (RA_AUDIO_RING - 1u)] = (int16_t)last_out;
      written++;
      phase += step;
    }
    phase -= 0x10000u;
    prev_in = in;
  }

  /* The fade after the newest sample, overwritten by the next frame. */
  for (i = 0; i < RA_AUDIO_FADE; i++)
    ring[(written + i) & (RA_AUDIO_RING - 1u)] =
        (int16_t)((last_out * (int32_t)(RA_AUDIO_FADE - i)) >> 8);
  clean_span(at, written - at + RA_AUDIO_FADE);
  return frames;
}

void ra_audio_sample_cb(int16_t left, int16_t right) {
  int16_t frame[2];
  frame[0] = left;
  frame[1] = right;
  ra_audio_batch_cb(frame, 1);
}

uint32_t ra_audio_underruns(void) { return underruns; }
