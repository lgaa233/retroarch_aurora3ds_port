/* src/audio.c against a model of the 3DS: a CSND voice that plays the ring at
 * 67027964 / 2045 Hz whatever happens, and frames that come at the screen's
 * rate, a varying time into each refresh. A 440 Hz sine goes in at each
 * core's rate and frame rate; what the voice plays must be that sine, at
 * 440 Hz times screen rate over core rate (one frame per refresh, as
 * RetroArch does), without gaps once started.
 *
 * Build and run with `make test`. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "retroarch.h"

#define AUDIO_PCM_ADDR 0u
#include "../../src/audio.c"

ra_config_t ra_config;
ra_content_t ra_content;

static int failures;
#define CHECK(cond, what)                                                     \
  do {                                                                        \
    if (cond) {                                                               \
      printf("ok   %s\n", what);                                              \
    } else {                                                                  \
      printf("FAIL %s\n", what);                                              \
      failures++;                                                             \
    }                                                                         \
  } while (0)

/* --- the model ------------------------------------------------------------- */

static int16_t test_ring[RA_AUDIO_RING];
static uint64_t now_us;
static int voice_on;
static uint64_t voice_t0;
static uint32_t voice_samples, voice_timer;
static uint64_t hw_played;

#define OUT_MAX (32768u * 70u)
static int16_t out[OUT_MAX];
static size_t out_n;

uint64_t sys_micros(void) { return now_us; }
bool snd_available(void) { return true; }
void os_dcache_clean_range(const void *addr, uint32_t len) {
  (void)addr;
  (void)len;
}

void app_core_post(uint32_t cmd, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint32_t a3) {
  (void)a0;
  (void)a1;
  if (cmd == AUDIO_CMD_VOICE) {
    voice_on = 1;
    voice_t0 = now_us;
    voice_samples = a2 / 2u;
    voice_timer = CSND_CLOCK / a3;
    hw_played = 0;
  } else if (cmd == AUDIO_CMD_VOICE_STOP) {
    voice_on = 0;
  }
}

/* The voice plays up to `now_us`. */
static void hw_run(void) {
  uint64_t pos;
  if (!voice_on)
    return;
  pos = (now_us - voice_t0) * CSND_CLOCK / ((uint64_t)voice_timer * 1000000u);
  while (hw_played < pos) {
    if (out_n < OUT_MAX)
      out[out_n++] = test_ring[hw_played % voice_samples];
    hw_played++;
  }
}

/* --- a run --------------------------------------------------------------- */

typedef struct {
  const char *name;
  double fps, rate, screen_hz;
  int spikes; /* frames 45 ms late, one every 5 s */
} scenario_t;

static void run(const scenario_t *sc) {
  const double seconds = 60.0;
  int16_t batch[2048 * 2];
  double phase = 0.0, acc = 0.0;
  long frame, frames = (long)(seconds * sc->screen_hz);
  uint32_t under_warm = 0;
  size_t warm = 0, i, first = 0, last = 0;
  int crossings = 0, max_jump = 0, spikes = 0;
  char what[200];

  memset(test_ring, 0, sizeof(test_ring));
  ring = test_ring;
  playing = 0;
  underruns = 0;
  out_n = 0;
  voice_on = 0;
  now_us = 1000000;
  ra_config.volume = 8;
  ra_content.av.timing.fps = sc->fps;
  ra_content.av.timing.sample_rate = sc->rate;
  srand(1);

  for (frame = 0; frame < frames; frame++) {
    uint64_t vbl = 1000000u + (uint64_t)((double)frame * 1e6 / sc->screen_hz);
    size_t n;
    /* The core's frame ends 4 to 15 ms into the refresh. */
    now_us = vbl + 4000u + (uint64_t)(rand() % 11000);
    if (sc->spikes && frame > 0 && frame % (long)(5 * sc->screen_hz) == 0) {
      now_us += 45000u;
      spikes++;
    }
    hw_run();
    acc += sc->rate / sc->fps;
    n = (size_t)acc;
    acc -= (double)n;
    for (i = 0; i < n; i++) {
      int16_t s = (int16_t)(8000.0 * sin(phase));
      phase += 2.0 * M_PI * 440.0 / sc->rate;
      batch[i * 2] = s;
      batch[i * 2 + 1] = s;
    }
    ra_audio_batch_cb(batch, n);
    if (!warm && now_us > 3000000u) {
      warm = out_n;
      under_warm = ra_audio_underruns();
    }
  }
  now_us += 1000000u / 60u;
  hw_run();

  /* Pitch, by rising zero crossings; and the largest step between samples
   * (a 440 Hz sine of 8000 moves at most ~680 a sample). */
  for (i = warm + 1; i < out_n; i++) {
    int jump = abs((int)out[i] - (int)out[i - 1]);
    if (jump > max_jump)
      max_jump = jump;
    if (out[i - 1] < 0 && out[i] >= 0) {
      if (!crossings)
        first = i;
      last = i;
      crossings++;
    }
  }
  {
    double hz = crossings > 1 ? (double)(crossings - 1) * CSND_CLOCK /
                                    (double)RA_AUDIO_TIMER /
                                    (double)(last - first)
                              : 0.0;
    /* One frame per refresh: the core's second of sound lasts fps/screen. */
    double want = 440.0 * sc->screen_hz / sc->fps;
    uint32_t extra = ra_audio_underruns() - under_warm;
    snprintf(what, sizeof(what),
             "%s: plays %.2f Hz (%.2f Hz), largest step %d, %u underruns "
             "after start-up (%d late frames)",
             sc->name, want, hz, max_jump, (unsigned)extra, spikes);
    CHECK(fabs(hz - want) < 0.5 && max_jump < 1000 &&
              extra <= (uint32_t)spikes,
          what);
  }
}

int main(void) {
  static const scenario_t sc[] = {
      {"GB 59.73 Hz, 32768", 4194304.0 / 70224.0, 32768.0, 59.83, 0},
      {"SMS 60 Hz, 22050", 60.0, 22050.0, 59.83, 0},
      {"CHIP-8 60 Hz, 32768", 60.0, 32768.0, 59.83, 0},
      {"GB, frames timed at 60.00 Hz", 4194304.0 / 70224.0, 32768.0, 60.0, 0},
      {"SMS, frames timed at 60.00 Hz", 60.0, 22050.0, 60.0, 0},
      {"GB with late frames", 4194304.0 / 70224.0, 32768.0, 59.83, 1},
  };
  size_t i;
  for (i = 0; i < sizeof(sc) / sizeof(sc[0]); i++)
    run(&sc[i]);
  if (failures) {
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
  }
  printf("\nall audio tests passed\n");
  return 0;
}
