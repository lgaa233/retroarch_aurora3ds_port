/* PCM playback through the Aurora ARM11 core.
 *
 * One looping mono voice. The ring lives in the PCM pool and the ARM9 only
 * writes ahead of the play head. It does not restart the voice when the
 * emulator falls behind: a restart waits on the ARM11 and was stretching
 * both the game and the sound.
 */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdint.h>
#include <string.h>

#define AUDIO_PCM_ADDR 0x23400000u
#define AUDIO_CMD_VOICE 9u
#define AUDIO_CMD_VOICE_STOP 10u
#define AUDIO_VOICE_LOOP (1u << 8)
#define AUDIO_VOICE_PCM16 (1u << 9)
#define AUDIO_VOICE_VOL(v) (((v) & 0xFFFFu) << 16)

extern void app_core_post(uint32_t cmd, uint32_t a0, uint32_t a1, uint32_t a2,
                          uint32_t a3);
extern void os_dcache_clean_range(const void *addr, uint32_t len);

#define RA_AUDIO_RING 4096u
#define RA_AUDIO_AHEAD 256u
#define RA_AUDIO_VOICE 6u
#define RA_AUDIO_VOL 0x7000u

static int16_t *ring = (int16_t *)(uintptr_t)AUDIO_PCM_ADDR;
static int playing;
static uint32_t rate;
static uint32_t written;
static uint64_t t0;

static uint32_t played_now(void) {
  uint64_t us = sys_micros() - t0;
  return (uint32_t)((us * rate) / 1000000ull);
}

static void clean_span(uint32_t index, uint32_t count) {
  uint32_t first = index & (RA_AUDIO_RING - 1u);
  uint32_t n1 = count;
  if (first + n1 > RA_AUDIO_RING)
    n1 = RA_AUDIO_RING - first;
  os_dcache_clean_range(ring + first, n1 * sizeof(int16_t));
  if (n1 < count)
    os_dcache_clean_range(ring, (count - n1) * sizeof(int16_t));
}

void ra_audio_stop(void) {
  if (!playing)
    return;
  app_core_post(AUDIO_CMD_VOICE_STOP, 1u << RA_AUDIO_VOICE, 0, 0, 0);
  playing = 0;
}

static void ra_audio_start(uint32_t new_rate) {
  uint32_t flags = AUDIO_VOICE_LOOP | AUDIO_VOICE_PCM16 | AUDIO_VOICE_VOL(RA_AUDIO_VOL);

  ra_audio_stop();
  if (!snd_available() || new_rate < 8000u || new_rate > 48000u)
    return;
  memset(ring, 0, RA_AUDIO_RING * sizeof(int16_t));
  os_dcache_clean_range(ring, RA_AUDIO_RING * sizeof(int16_t));
  rate = new_rate;
  written = RA_AUDIO_AHEAD;
  t0 = sys_micros();
  app_core_post(AUDIO_CMD_VOICE, RA_AUDIO_VOICE | flags, (uint32_t)(uintptr_t)ring,
                RA_AUDIO_RING * sizeof(int16_t), rate);
  playing = 1;
}

size_t ra_audio_batch_cb(const int16_t *data, size_t frames) {
  uint32_t played, at;
  uint32_t want;
  size_t i;

  if (!data || !frames)
    return frames;
  want = (uint32_t)(ra_content.av.timing.sample_rate + 0.5);
  if (want < 8000u || want > 48000u || !snd_available())
    return frames;
  if (!playing || want != rate)
    ra_audio_start(want);
  if (!playing)
    return frames;

  played = played_now();
  /* Behind the speaker: jump the write head. Do not restart the voice. */
  if ((int32_t)(played + 64u - written) > 0)
    written = played + RA_AUDIO_AHEAD;
  /* Ahead (fast-forward produced extra batches): drop them. */
  if ((int32_t)(written - played) > 2048)
    return frames;

  at = written;
  for (i = 0; i < frames; i++) {
    int32_t mono = ((int32_t)data[i * 2] + (int32_t)data[i * 2 + 1]) >> 1;
    ring[(at + (uint32_t)i) & (RA_AUDIO_RING - 1u)] = (int16_t)mono;
  }
  clean_span(at, (uint32_t)frames);
  written = at + (uint32_t)frames;
  return frames;
}

void ra_audio_sample_cb(int16_t left, int16_t right) {
  int16_t frame[2];
  frame[0] = left;
  frame[1] = right;
  ra_audio_batch_cb(frame, 1);
}
