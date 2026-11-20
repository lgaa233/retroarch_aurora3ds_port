/* The libretro environment: what the frontend answers when a core asks. */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
  /* The console draws over the game, so core logs are dropped unless they
   * are warnings or worse; those end up on the Aurora console. */
  va_list ap;
  if (level < RETRO_LOG_WARN)
    return;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

bool ra_env_cb(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
    const enum retro_pixel_format *fmt = data;
    if (*fmt != RETRO_PIXEL_FORMAT_0RGB1555 &&
        *fmt != RETRO_PIXEL_FORMAT_RGB565 &&
        *fmt != RETRO_PIXEL_FORMAT_XRGB8888)
      return false;
    ra_video_set_pixel_format(*fmt);
    return true;
  }
  case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
    bool *b = data;
    *b = true;
    return true;
  }
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
  case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
    const char **dir = data;
    *dir = cmd == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY ? ra_dir_saves()
                                                       : ra_dir_roms();
    return true;
  }
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
    struct retro_log_callback *cb = data;
    cb->log = log_cb;
    return true;
  }
  case RETRO_ENVIRONMENT_SET_GEOMETRY:
  case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    /* The blitter notices a new width/height on its own. */
    return true;
  case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    return true;
  case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    return false;
  default:
    return false;
  }
}

void ra_video_cb(const void *data, unsigned width, unsigned height,
                 size_t pitch) {
  ra_video_submit(data, width, height, pitch);
}

void ra_audio_sample_cb(int16_t left, int16_t right) {
  /* The Aurora SDK has no PCM streaming (only preloaded WAV voices), so
   * core audio has nowhere to go yet. */
  (void)left;
  (void)right;
}

size_t ra_audio_batch_cb(const int16_t *data, size_t frames) {
  (void)data;
  return frames;
}
