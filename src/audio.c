/* PCM playback through the Aurora ARM11 core.
 *
 * NovaLauncher and the OS itself play 16-bit PCM by writing samples into
 * FCRAM and posting AUDIO_CMD_VOICE. CSND then reads them in place. There is
 * no stream command an app can see, so this keeps one looping stereo pair
 * (voices 6 and 7, the same channels the OS uses for video) and writes new
 * samples ahead of where the channel should be, timed from sys_micros().
 *
 * The rings live in the 10 MB PCM pool at AUDIO_PCM_ADDR. snd_load() uses
 * that pool too; this frontend does not call it.
 */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdint.h>
#include <string.h>

/* The fields this file uses, from AuroraOS include/audio.h. The SDK headers
 * an app is given do not publish them; the running core still does. */
#define AUDIO_PCM_ADDR 0x23400000u
#define AUDIO_CMD_VOICE 9u
#define AUDIO_CMD_VOICE_STOP 10u
#define AUDIO_VOICE_LOOP (1u << 8)
#define AUDIO_VOICE_PCM16 (1u << 9)
#define AUDIO_VOICE_VOL(v) (((v) & 0xFFFFu) << 16)

extern void app_core_post(uint32_t cmd, uint32_t a0, uint32_t a1, uint32_t a2,
                          uint32_t a3);
extern void os_dcache_clean_range(const void *addr, uint32_t len);

#define RA_AUDIO_RING 8192u /* samples per side; power of two */
#define RA_AUDIO_LATENCY 1024u
#define RA_AUDIO_VOICE_L 6u
#define RA_AUDIO_VOICE_R 7u
#define RA_AUDIO_VOL 0x7000u /* of 0x8000 full scale */

static int16_t *ring_l = (int16_t *)(uintptr_t)AUDIO_PCM_ADDR;
static int16_t *ring_r =
    (int16_t *)(uintptr_t)(AUDIO_PCM_ADDR + RA_AUDIO_RING * sizeof(int16_t));

static int playing;
static uint32_t rate;
static uint32_t written; /* next sample index, absolute */
static uint64_t t0;

static uint32_t played_now(void) {
  uint64_t us = sys_micros() - t0;
  return (uint32_t)((us * rate) / 1000000ull);
}

void ra_audio_stop(void) {
  if (!playing)
    return;
  app_core_post(AUDIO_CMD_VOICE_STOP,
                (1u << RA_AUDIO_VOICE_L) | (1u << RA_AUDIO_VOICE_R), 0, 0, 0);
  playing = 0;
}

static void ra_audio_start(uint32_t new_rate) {
  uint32_t flags = AUDIO_VOICE_LOOP | AUDIO_VOICE_PCM16 | AUDIO_VOICE_VOL(RA_AUDIO_VOL);

  ra_audio_stop();
  if (!snd_available() || new_rate < 8000u || new_rate > 48000u)
    return;

  memset(ring_l, 0, RA_AUDIO_RING * sizeof(int16_t) * 2);
  os_dcache_clean_range(ring_l, RA_AUDIO_RING * sizeof(int16_t) * 2);
  rate = new_rate;
  written = RA_AUDIO_LATENCY; /* the channel plays silence until we catch up */
  t0 = sys_micros();
  app_core_post(AUDIO_CMD_VOICE, RA_AUDIO_VOICE_L | flags, (uint32_t)(uintptr_t)ring_l,
                RA_AUDIO_RING * sizeof(int16_t), rate);
  app_core_post(AUDIO_CMD_VOICE, RA_AUDIO_VOICE_R | flags, (uint32_t)(uintptr_t)ring_r,
                RA_AUDIO_RING * sizeof(int16_t), rate);
  playing = 1;
}

size_t ra_audio_batch_cb(const int16_t *data, size_t frames) {
  uint32_t played;
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
  if ((int32_t)(played - written) > 0) {
    /* The channel ran into samples we already played. Start clean. */
    ra_audio_start(want);
    if (!playing)
      return frames;
    played = 0;
  }
  /* One batch per frame. Fast-forward produces several; the extras are
   * dropped so the stream does not fall seconds behind. */
  if (written + (uint32_t)frames > played + RA_AUDIO_LATENCY + (uint32_t)frames + 256u)
    return frames;

  for (i = 0; i < frames; i++) {
    uint32_t p = (written + (uint32_t)i) & (RA_AUDIO_RING - 1u);
    ring_l[p] = data[i * 2];
    ring_r[p] = data[i * 2 + 1];
  }
  written += (uint32_t)frames;
  os_dcache_clean_range(ring_l, RA_AUDIO_RING * sizeof(int16_t) * 2);
  return frames;
}

void ra_audio_sample_cb(int16_t left, int16_t right) {
  int16_t frame[2];
  frame[0] = left;
  frame[1] = right;
  ra_audio_batch_cb(frame, 1);
}
