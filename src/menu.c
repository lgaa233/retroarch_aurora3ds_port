/* The menu, in the spirit of RGUI: the 8x8 font on a dark background, a
 * green title, a file browser, settings and an information screen. It draws
 * on the top screen; the bottom screen shows the controls. */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Browser paths and status lines truncate deliberately. */
#pragma GCC diagnostic ignored "-Wformat-truncation"

#define MENU_BG RGB(0x28, 0x28, 0x28)
#define MENU_BAR RGB(0x18, 0x18, 0x18)
#define MENU_SEL RGB(0x00, 0x60, 0x30)
#define MENU_TEXT RGB(0xE8, 0xE8, 0xE8)
#define MENU_DIM RGB(0x90, 0x90, 0x90)
#define MENU_GREEN RGB(0x60, 0xE0, 0x80)

#define ROW_H 12
#define LIST_Y 28
#define LIST_ROWS 16

typedef enum {
  SCR_MAIN = 0,
  SCR_BROWSER,
  SCR_SETTINGS,
  SCR_INFO,
} menu_screen_t;

static bool menu_open;
static menu_screen_t screen;
static int sel[4];    /* the cursor on each screen */
static int scroll[4]; /* the first visible row on each screen */
static char status[64];
static bool bottom_drawn;

/* --- the file browser -------------------------------------------------------- */

typedef struct {
  char name[FS_NAME_MAX];
  bool is_dir;
} browser_entry_t;

#define BROWSER_MAX 256

static char cur_dir[RA_PATH_MAX];
static browser_entry_t *entries;
static int entry_count;
static bool listing_truncated;

static bool has_known_extension(const char *name) {
  return ra_core_for_path(name) != NULL;
}

static int entry_cmp(const void *a, const void *b) {
  const browser_entry_t *ea = a, *eb = b;
  if (ea->is_dir != eb->is_dir)
    return ea->is_dir ? -1 : 1;
  return strcasecmp(ea->name, eb->name);
}

static void browser_list(void) {
  FsDir *d;
  FsEntry e;

  entry_count = 0;
  listing_truncated = false;
  if (!entries) {
    entries = malloc(sizeof(*entries) * BROWSER_MAX);
    if (!entries)
      return;
  }

  d = fs_dir_open(cur_dir);
  if (!d)
    return;
  while (fs_dir_read(d, &e)) {
    if (entry_count >= BROWSER_MAX) {
      listing_truncated = true;
      break;
    }
    if (!e.is_dir && !has_known_extension(e.name))
      continue;
    memcpy(entries[entry_count].name, e.name, FS_NAME_MAX);
    entries[entry_count].is_dir = e.is_dir;
    entry_count++;
  }
  fs_dir_close(d);

  qsort(entries, (size_t)entry_count, sizeof(*entries), entry_cmp);
  sel[SCR_BROWSER] = 0;
  scroll[SCR_BROWSER] = 0;
}

static void browser_enter(const char *dir) {
  strncpy(cur_dir, dir, sizeof(cur_dir) - 1);
  cur_dir[sizeof(cur_dir) - 1] = '\0';
  browser_list();
}

static bool browser_at_root(void) {
  return strcmp(cur_dir, "/") == 0 || cur_dir[0] == '\0';
}

static void browser_up(void) {
  char *slash = strrchr(cur_dir, '/');
  if (!slash || slash == cur_dir)
    strcpy(cur_dir, "/");
  else
    *slash = '\0';
  browser_list();
}

/* --- input with key repeat ----------------------------------------------------- */

static uint32_t held_frames;

static uint32_t menu_keys(void) {
  uint32_t down = hid_keys_down();
  uint32_t held = hid_keys_held();
  uint32_t vertical = KEY_UP | KEY_DOWN | KEY_CPAD_UP | KEY_CPAD_DOWN;

  if (held & vertical) {
    held_frames++;
    if (held_frames > 18 && (held_frames & 3) == 0)
      down |= held & vertical;
  } else {
    held_frames = 0;
  }
  /* The circle pad counts as the d-pad in the menu. */
  if (down & KEY_CPAD_UP) down |= KEY_UP;
  if (down & KEY_CPAD_DOWN) down |= KEY_DOWN;
  if (down & KEY_CPAD_LEFT) down |= KEY_LEFT;
  if (down & KEY_CPAD_RIGHT) down |= KEY_RIGHT;
  return down;
}

static void clamp_scroll(menu_screen_t s, int count) {
  if (sel[s] < 0)
    sel[s] = count ? count - 1 : 0;
  if (sel[s] >= count)
    sel[s] = 0;
  if (sel[s] < scroll[s])
    scroll[s] = sel[s];
  if (sel[s] >= scroll[s] + LIST_ROWS)
    scroll[s] = sel[s] - LIST_ROWS + 1;
  if (scroll[s] < 0)
    scroll[s] = 0;
}

/* --- drawing --------------------------------------------------------------------- */

static void draw_frame(const char *subtitle) {
  gfx_clear(SCREEN_TOP, MENU_BG);
  gfx_rect(SCREEN_TOP, 0, 0, SCREEN_TOP_WIDTH, 16, MENU_BAR);
  gfx_text(SCREEN_TOP, 4, 4, MENU_GREEN, "%s %s", RA_NAME, RA_VERSION);
  gfx_text(SCREEN_TOP, 200, 4, MENU_DIM, "libretro on AuroraOS");
  gfx_text(SCREEN_TOP, 4, 18, MENU_DIM, "%s", subtitle);
  if (status[0]) {
    gfx_rect(SCREEN_TOP, 0, SCREEN_HEIGHT - 14, SCREEN_TOP_WIDTH, 14,
             MENU_BAR);
    gfx_text(SCREEN_TOP, 4, SCREEN_HEIGHT - 11, COLOR_YELLOW, "%s", status);
  }
}

static void draw_row(int row, bool selected, uint32_t color, const char *fmt,
                     ...) {
  char buf[96];
  va_list ap;
  int y = LIST_Y + row * ROW_H;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (selected)
    gfx_rect(SCREEN_TOP, 0, y - 2, SCREEN_TOP_WIDTH, ROW_H, MENU_SEL);
  gfx_text(SCREEN_TOP, 8, y, color, "%s", buf);
}

static void draw_bottom_menu(void) {
  int c, y = 64;

  gfx_clear(SCREEN_BOTTOM, COLOR_BLACK);
  gfx_rect(SCREEN_BOTTOM, 0, 0, SCREEN_BOTTOM_WIDTH, 16, MENU_BAR);
  gfx_text(SCREEN_BOTTOM, 4, 4, COLOR_AURORA, "%s %s", RA_NAME, RA_VERSION);
  gfx_text(SCREEN_BOTTOM, 4, 28, MENU_DIM, "D-pad: move    A: choose");
  gfx_text(SCREEN_BOTTOM, 4, 40, MENU_DIM, "B: back        HOME: quit");
  gfx_text(SCREEN_BOTTOM, 4, y, MENU_DIM, "Cores:");
  for (c = 0; c < ra_core_count; c++, y += 12)
    gfx_text(SCREEN_BOTTOM, 60, y, MENU_DIM, "%s (%s)",
             ra_cores[c]->library_name, ra_cores[c]->valid_extensions);
  gfx_text(SCREEN_BOTTOM, 4, y + 12, MENU_DIM, "ROMs go in SD:/Aurora/ROMs");
  bottom_drawn = true;
}

/* --- the screens -------------------------------------------------------------------- */

enum {
  MAIN_RESUME = 0,
  MAIN_RESTART,
  MAIN_SAVE_STATE,
  MAIN_LOAD_STATE,
  MAIN_STATE_SLOT,
  MAIN_SEP,
  MAIN_LOAD_CONTENT,
  MAIN_SETTINGS,
  MAIN_INFO,
  MAIN_QUIT,
  MAIN_COUNT
};

static bool main_entry_enabled(int i) {
  switch (i) {
  case MAIN_RESUME:
  case MAIN_RESTART:
  case MAIN_SAVE_STATE:
  case MAIN_LOAD_STATE:
  case MAIN_STATE_SLOT:
    return ra_content.running;
  case MAIN_SEP:
    return false;
  default:
    return true;
  }
}

static ra_menu_action_t tick_main(void) {
  uint32_t down = menu_keys();
  int i = sel[SCR_MAIN];

  if (down & (KEY_UP | KEY_DOWN)) {
    int dir = (down & KEY_UP) ? -1 : 1;
    do {
      i += dir;
      if (i < 0) i = MAIN_COUNT - 1;
      if (i >= MAIN_COUNT) i = 0;
    } while (!main_entry_enabled(i));
    sel[SCR_MAIN] = i;
  }

  if ((down & (KEY_LEFT | KEY_RIGHT)) && i == MAIN_STATE_SLOT) {
    int d = (down & KEY_LEFT) ? -1 : 1;
    ra_content.state_slot = (ra_content.state_slot + 10 + d) % 10;
  }

  if (down & KEY_B && ra_content.running)
    return RA_MENU_RESUME;

  if (down & KEY_A) {
    char msg[64];
    switch (i) {
    case MAIN_RESUME:
      return RA_MENU_RESUME;
    case MAIN_RESTART:
      ra_content.core->reset();
      ra_osd("Restarted");
      return RA_MENU_RESUME;
    case MAIN_SAVE_STATE:
      ra_state_save(ra_content.state_slot, msg, sizeof(msg));
      snprintf(status, sizeof(status), "%s", msg);
      break;
    case MAIN_LOAD_STATE:
      if (ra_state_load(ra_content.state_slot, msg, sizeof(msg))) {
        ra_osd("%s", msg);
        return RA_MENU_RESUME;
      }
      snprintf(status, sizeof(status), "%s", msg);
      break;
    case MAIN_STATE_SLOT:
      ra_content.state_slot = (ra_content.state_slot + 1) % 10;
      break;
    case MAIN_LOAD_CONTENT:
      screen = SCR_BROWSER;
      if (ra_config.last_dir[0] && fs_is_dir(ra_config.last_dir))
        browser_enter(ra_config.last_dir);
      else
        browser_enter(ra_dir_roms());
      break;
    case MAIN_SETTINGS:
      screen = SCR_SETTINGS;
      break;
    case MAIN_INFO:
      screen = SCR_INFO;
      break;
    case MAIN_QUIT:
      return RA_MENU_QUIT;
    }
  }

  draw_frame(ra_content.running ? ra_content.rom_name : "No content loaded");
  {
    int row = 0;
    bool run = ra_content.running;
    draw_row(row++, i == MAIN_RESUME, run ? MENU_TEXT : MENU_DIM, "Resume");
    draw_row(row++, i == MAIN_RESTART, run ? MENU_TEXT : MENU_DIM, "Restart");
    draw_row(row++, i == MAIN_SAVE_STATE, run ? MENU_TEXT : MENU_DIM,
             "Save State");
    draw_row(row++, i == MAIN_LOAD_STATE, run ? MENU_TEXT : MENU_DIM,
             "Load State");
    draw_row(row++, i == MAIN_STATE_SLOT, run ? MENU_TEXT : MENU_DIM,
             "State Slot          < %d >", ra_content.state_slot);
    row++;
    draw_row(row++, i == MAIN_LOAD_CONTENT, MENU_TEXT, "Load Content");
    draw_row(row++, i == MAIN_SETTINGS, MENU_TEXT, "Settings");
    draw_row(row++, i == MAIN_INFO, MENU_TEXT, "Information");
    draw_row(row++, i == MAIN_QUIT, MENU_TEXT, "Quit RetroArch");
  }
  return RA_MENU_NONE;
}

static ra_menu_action_t tick_browser(void) {
  uint32_t down = menu_keys();
  int total = entry_count + (browser_at_root() ? 0 : 1); /* ".." first */
  int i;

  if (down & KEY_UP) sel[SCR_BROWSER]--;
  if (down & KEY_DOWN) sel[SCR_BROWSER]++;
  if (down & KEY_L) sel[SCR_BROWSER] -= LIST_ROWS;
  if (down & KEY_R) sel[SCR_BROWSER] += LIST_ROWS;
  if (sel[SCR_BROWSER] < 0) sel[SCR_BROWSER] = total ? total - 1 : 0;
  if (sel[SCR_BROWSER] >= total) sel[SCR_BROWSER] = 0;
  clamp_scroll(SCR_BROWSER, total ? total : 1);
  i = sel[SCR_BROWSER];

  if (down & KEY_B) {
    if (browser_at_root())
      screen = SCR_MAIN;
    else
      browser_up();
  } else if ((down & KEY_A) && total > 0) {
    int idx = browser_at_root() ? i : i - 1;
    if (idx < 0) {
      browser_up();
    } else if (idx < entry_count) {
      browser_entry_t *e = &entries[idx];
      char path[RA_PATH_MAX];
      if (strcmp(cur_dir, "/") == 0)
        snprintf(path, sizeof(path), "/%s", e->name);
      else
        snprintf(path, sizeof(path), "%s/%s", cur_dir, e->name);
      if (e->is_dir) {
        browser_enter(path);
      } else if (ra_content_load(path)) {
        strncpy(ra_config.last_dir, cur_dir, sizeof(ra_config.last_dir) - 1);
        ra_config.last_dir[sizeof(ra_config.last_dir) - 1] = '\0';
        ra_config_save();
        status[0] = '\0';
        menu_open = false;
        screen = SCR_MAIN;
        sel[SCR_MAIN] = MAIN_RESUME;
        ra_osd("%s", ra_content.rom_name);
        return RA_MENU_LOADED;
      } else {
        snprintf(status, sizeof(status), "Could not load %s", e->name);
      }
    }
  }

  draw_frame(cur_dir[0] ? cur_dir : "/");
  {
    int row, shown = 0;
    for (row = scroll[SCR_BROWSER];
         row < total && shown < LIST_ROWS; row++, shown++) {
      int idx = browser_at_root() ? row : row - 1;
      bool s = row == sel[SCR_BROWSER];
      if (idx < 0)
        draw_row(shown, s, MENU_DIM, "..");
      else if (entries[idx].is_dir)
        draw_row(shown, s, MENU_GREEN, "[%s]", entries[idx].name);
      else
        draw_row(shown, s, MENU_TEXT, "%s", entries[idx].name);
    }
    if (!total)
      draw_row(0, false, MENU_DIM, "(no folders or known ROMs here)");
    if (listing_truncated)
      gfx_text(SCREEN_TOP, 4, SCREEN_HEIGHT - 26, MENU_DIM,
               "(first %d entries shown)", BROWSER_MAX);
  }
  return RA_MENU_NONE;
}

enum {
  SET_SCALE = 0,
  SET_FRAMESKIP,
  SET_SHOW_FPS,
  SET_FAST_FORWARD,
  SET_BACK,
  SET_COUNT
};

static ra_menu_action_t tick_settings(void) {
  uint32_t down = menu_keys();
  int i;
  static const char *scale_names[] = {"1x (centered)", "Native (fast)",
                                      "Stretch (400x240)"};

  if (down & KEY_UP) sel[SCR_SETTINGS]--;
  if (down & KEY_DOWN) sel[SCR_SETTINGS]++;
  if (sel[SCR_SETTINGS] < 0) sel[SCR_SETTINGS] = SET_COUNT - 1;
  if (sel[SCR_SETTINGS] >= SET_COUNT) sel[SCR_SETTINGS] = 0;
  i = sel[SCR_SETTINGS];

  if (down & (KEY_LEFT | KEY_RIGHT | KEY_A)) {
    int d = (down & KEY_LEFT) ? -1 : 1;
    switch (i) {
    case SET_SCALE:
      ra_config.scale = (ra_config.scale + 3 + d) % 3;
      break;
    case SET_FRAMESKIP:
      ra_config.frameskip = (ra_config.frameskip + 5 + d) % 5;
      break;
    case SET_SHOW_FPS:
      ra_config.show_fps = !ra_config.show_fps;
      break;
    case SET_FAST_FORWARD:
      ra_config.fast_forward = !ra_config.fast_forward;
      break;
    case SET_BACK:
      if (down & KEY_A) {
        ra_config_save();
        screen = SCR_MAIN;
      }
      break;
    }
  }
  if (down & KEY_B) {
    ra_config_save();
    screen = SCR_MAIN;
  }

  draw_frame("Settings");
  draw_row(0, i == SET_SCALE, MENU_TEXT, "Video Scale        < %s >",
           scale_names[ra_config.scale]);
  draw_row(1, i == SET_FRAMESKIP, MENU_TEXT, "Frameskip          < %d >",
           ra_config.frameskip);
  draw_row(2, i == SET_SHOW_FPS, MENU_TEXT, "Show FPS           < %s >",
           ra_config.show_fps ? "On" : "Off");
  draw_row(3, i == SET_FAST_FORWARD, MENU_TEXT, "Fast Fwd (hold R)  < %s >",
           ra_config.fast_forward ? "On" : "Off");
  draw_row(4, i == SET_BACK, MENU_TEXT, "Back");
  return RA_MENU_NONE;
}

static ra_menu_action_t tick_info(void) {
  uint32_t down = menu_keys();
  int row = 0, c;

  if (down & (KEY_B | KEY_A))
    screen = SCR_MAIN;

  draw_frame("Information");
  draw_row(row++, false, MENU_TEXT, "%s %s for AuroraOS", RA_NAME, RA_VERSION);
  draw_row(row++, false, MENU_DIM, "a libretro frontend, statically linked");
  row++;
  draw_row(row++, false, MENU_TEXT, "Cores:");
  for (c = 0; c < ra_core_count; c++)
    draw_row(row++, false, MENU_DIM, "  %s %s (%s)",
             ra_cores[c]->library_name, ra_cores[c]->library_version,
             ra_cores[c]->valid_extensions);
  row++;
  draw_row(row++, false, MENU_DIM, "Console: %s",
           sys_is_new3ds() ? "New 3DS / New 2DS" : "Old 3DS / 2DS");
  draw_row(row++, false, MENU_DIM, "CPU: ARM9 @ 134 MHz (all models)");
  draw_row(row++, false, MENU_DIM, "Audio: stereo PCM (NES has none)");
  row++;
  draw_row(row++, false, MENU_DIM, "Saves:  %s", ra_dir_saves());
  draw_row(row++, false, MENU_DIM, "States: %s", ra_dir_states());
  return RA_MENU_NONE;
}

/* --- the public face -------------------------------------------------------------- */

void ra_menu_open(void) {
  ra_audio_stop();
  menu_open = true;
  screen = SCR_MAIN;
  sel[SCR_MAIN] = ra_content.running ? MAIN_RESUME : MAIN_LOAD_CONTENT;
  status[0] = '\0';
  bottom_drawn = false;
}

bool ra_menu_is_open(void) { return menu_open; }

ra_menu_action_t ra_menu_tick(void) {
  ra_menu_action_t act = RA_MENU_NONE;

  if (!bottom_drawn)
    draw_bottom_menu();

  switch (screen) {
  case SCR_MAIN:
    act = tick_main();
    break;
  case SCR_BROWSER:
    act = tick_browser();
    break;
  case SCR_SETTINGS:
    act = tick_settings();
    break;
  case SCR_INFO:
    act = tick_info();
    break;
  }

  if (act == RA_MENU_RESUME || act == RA_MENU_LOADED)
    menu_open = false;
  if (act == RA_MENU_RESUME)
    status[0] = '\0';
  return act;
}
