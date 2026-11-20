/* RetroArch for AuroraOS (Nintendo 3DS): the entry point and the run loop.
 *
 * A libretro frontend over the Aurora C SDK. Cores are statically linked
 * (there is no dynamic loader on AuroraOS) and the frame loop is paced by
 * app_loop(), which presents both screens at 60 Hz and scans input.
 */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ra_content_t ra_content;

/* --- the OSD ---------------------------------------------------------------- */

static char osd_text[64];
static uint32_t osd_until;

void ra_osd(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(osd_text, sizeof(osd_text), fmt, ap);
  va_end(ap);
  osd_until = sys_millis() + 2000;
}

void ra_osd_draw(void) {
  static bool was_visible;

  if (!osd_text[0] || sys_millis() >= osd_until) {
    osd_text[0] = '\0';
    if (was_visible) {
      /* The OSD also covered border pixels the blit does not repaint. */
      was_visible = false;
      ra_video_invalidate();
    }
    return;
  }
  was_visible = true;
  gfx_rect(SCREEN_TOP, 0, SCREEN_HEIGHT - 14, SCREEN_TOP_WIDTH, 14,
           COLOR_BLACK);
  gfx_text(SCREEN_TOP, 4, SCREEN_HEIGHT - 11, COLOR_YELLOW, "%s", osd_text);
}

/* --- content ----------------------------------------------------------------- */

static void set_rom_name(const char *path) {
  const char *slash = strrchr(path, '/');
  const char *base = slash ? slash + 1 : path;
  const char *dot = strrchr(base, '.');
  size_t n = dot && dot > base ? (size_t)(dot - base) : strlen(base);
  if (n >= sizeof(ra_content.rom_name))
    n = sizeof(ra_content.rom_name) - 1;
  memcpy(ra_content.rom_name, base, n);
  ra_content.rom_name[n] = '\0';
}

bool ra_content_load(const char *path) {
  const ra_core_t *core = ra_core_for_path(path);
  struct retro_game_info game;
  void *data;
  size_t size = 0;

  if (!core)
    return false;

  data = fs_read_file(path, &size);
  if (!data || !size) {
    free(data);
    return false;
  }

  if (ra_content.running)
    ra_content_unload();

  core->set_callbacks(ra_env_cb, ra_video_cb, ra_audio_batch_cb,
                      ra_input_poll_cb, ra_input_state_cb);
  core->init();

  memset(&game, 0, sizeof(game));
  game.path = path;
  game.data = data;
  game.size = size;
  if (!core->load_game(&game)) {
    core->deinit();
    free(data);
    return false;
  }

  ra_content.core = core;
  ra_content.rom_data = data;
  ra_content.rom_size = size;
  strncpy(ra_content.rom_path, path, sizeof(ra_content.rom_path) - 1);
  ra_content.rom_path[sizeof(ra_content.rom_path) - 1] = '\0';
  set_rom_name(path);
  core->get_system_av_info(&ra_content.av);
  ra_content.running = true;
  ra_content.state_slot = 0;

  ra_sram_load();
  ra_video_init();
  return true;
}

void ra_content_unload(void) {
  if (!ra_content.running)
    return;
  ra_sram_save();
  ra_content.core->unload_game();
  ra_content.core->deinit();
  free(ra_content.rom_data);
  memset(&ra_content, 0, sizeof(ra_content));
}

/* --- screens ------------------------------------------------------------------ */

static void draw_bottom_game(void) {
  gfx_clear(SCREEN_BOTTOM, COLOR_BLACK);
  gfx_rect(SCREEN_BOTTOM, 0, 0, SCREEN_BOTTOM_WIDTH, 16, RGB(0x20, 0x20, 0x20));
  gfx_text(SCREEN_BOTTOM, 4, 4, COLOR_AURORA, "%s %s", RA_NAME, RA_VERSION);
  gfx_text(SCREEN_BOTTOM, 4, 28, COLOR_WHITE, "%s", ra_content.rom_name);
  gfx_text(SCREEN_BOTTOM, 4, 40, COLOR_GRAY, "core: %s %s",
           ra_content.core->library_name, ra_content.core->library_version);
  gfx_text(SCREEN_BOTTOM, 4, 64, COLOR_GRAY, "Touch the screen for the menu");
  gfx_text(SCREEN_BOTTOM, 4, 76, COLOR_GRAY, "HOME quits to the Home Menu");
  if (ra_config.fast_forward)
    gfx_text(SCREEN_BOTTOM, 4, 88, COLOR_GRAY, "Hold R to fast-forward");
}

static void on_exit_save(void) {
  /* Runs on exit(), on HOME and when main() returns. */
  if (ra_content.running)
    ra_sram_save();
  ra_config_save();
}

/* --- main ---------------------------------------------------------------------- */

int main(void) {
  int fps = 0, fps_frames = 0;
  uint32_t fps_t0;

  ra_paths_init();
  ra_config_load();
  atexit(on_exit_save);
  ra_video_init();
  ra_menu_open();

  fps_t0 = sys_millis();

  while (app_loop()) {
    if (ra_menu_is_open()) {
      ra_menu_action_t act = ra_menu_tick();
      if (act == RA_MENU_QUIT)
        break;
      if (act == RA_MENU_RESUME || act == RA_MENU_LOADED) {
        ra_video_invalidate();
        draw_bottom_game();
        ra_input_clear();
      }
      continue;
    }

    /* In the game. */
    if (!ra_content.running) {
      ra_menu_open();
      continue;
    }
    if (hid_touch_down()) {
      ra_menu_open();
      continue;
    }

    ra_input_update();
    {
      /* Expanded frames cost more to draw than 16 ms. Run the emulator
       * often enough to stay near 60 Hz, and only copy the last picture,
       * so Stretch and Fit do not play in slow motion. */
      static uint32_t emu_clock;
      static int fresh = 1;
      uint32_t now = sys_millis();
      int guard = 0;
      if (fresh || now - emu_clock > 80) {
        emu_clock = now;
        fresh = 0;
      }
      while (emu_clock + 16 < now && guard < 3) {
        ra_video_skip_next();
        ra_content.core->run();
        emu_clock += 16;
        guard++;
      }
      ra_content.core->run();
      emu_clock += 16;
    }

    if (ra_config.fast_forward && (hid_keys_held() & KEY_R)) {
      int extra;
      for (extra = 0; extra < 3; extra++) {
        ra_video_skip_next();
        ra_content.core->run();
      }
    }

    fps_frames++;
    {
      uint32_t now = sys_millis();
      if (now - fps_t0 >= 1000) {
        fps = fps_frames;
        fps_frames = 0;
        fps_t0 = now;
      }
    }
    if (ra_config.show_fps) {
      /* Drawn after the blit, so the game repaints under it next frame;
       * leaving the menu repaints the borders it may sit on. */
      gfx_rect(SCREEN_TOP, 0, 0, 64, 12, COLOR_BLACK);
      gfx_text(SCREEN_TOP, 2, 2, COLOR_GREEN, "%d fps", fps);
    }
    ra_osd_draw();
  }

  ra_content_unload();
  return 0;
}
