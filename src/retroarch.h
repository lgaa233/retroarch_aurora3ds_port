/* RetroArch for AuroraOS: a libretro frontend for the Nintendo 3DS running
 * AuroraOS, built on the Aurora C SDK. */
#ifndef RA_RETROARCH_H
#define RA_RETROARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libretro.h>
#include "cores/core.h"

#define RA_VERSION "0.1.0"
#define RA_NAME "RetroArch"

#define RA_PATH_MAX 300

/* --- configuration -------------------------------------------------------- */

typedef enum {
  RA_SCALE_1X = 0,  /* unscaled, centered */
  RA_SCALE_FIT = 1, /* largest size that keeps the aspect ratio */
  RA_SCALE_FULL = 2 /* stretched to 400x240 */
} ra_scale_t;

typedef struct {
  int scale;        /* ra_scale_t */
  int frameskip;    /* 0..4: present 1 frame out of frameskip+1 */
  bool show_fps;
  bool fast_forward; /* hold R to run extra frames */
  char last_dir[RA_PATH_MAX];
} ra_config_t;

extern ra_config_t ra_config;

void ra_config_load(void);
void ra_config_save(void);

/* --- content state -------------------------------------------------------- */

typedef struct {
  const ra_core_t *core;
  char rom_path[RA_PATH_MAX];
  char rom_name[64]; /* file name without folder or extension */
  void *rom_data;    /* kept for the life of the game (agnes aliases it) */
  size_t rom_size;
  struct retro_system_av_info av;
  bool running;
  int state_slot; /* 0..9 */
} ra_content_t;

extern ra_content_t ra_content;

bool ra_content_load(const char *path);
void ra_content_unload(void);

/* --- paths ---------------------------------------------------------------- */

const char *ra_dir_roms(void);   /* /Aurora/ROMs */
const char *ra_dir_saves(void);  /* <app_dir>/saves */
const char *ra_dir_states(void); /* <app_dir>/states */
void ra_paths_init(void);

/* --- libretro callbacks (environ.c) --------------------------------------- */

bool ra_env_cb(unsigned cmd, void *data);
void ra_video_cb(const void *data, unsigned width, unsigned height,
                 size_t pitch);
void ra_audio_sample_cb(int16_t left, int16_t right);
size_t ra_audio_batch_cb(const int16_t *data, size_t frames);
void ra_input_poll_cb(void);
int16_t ra_input_state_cb(unsigned port, unsigned device, unsigned index,
                          unsigned id);

/* --- video (video.c) ------------------------------------------------------ */

void ra_video_init(void);
/* Called by ra_video_cb with the core's frame. */
void ra_video_submit(const void *data, unsigned width, unsigned height,
                     size_t pitch);
void ra_video_set_pixel_format(enum retro_pixel_format fmt);
/* Repaint borders next frame (after leaving the menu). */
void ra_video_invalidate(void);
/* True when the last submitted frame was actually drawn (not frameskipped). */
bool ra_video_frame_drawn(void);
/* Drop the next submitted frame (fast-forward runs the core blind). */
void ra_video_skip_next(void);

/* --- input (input.c) ------------------------------------------------------ */

void ra_input_update(void); /* latch hid state into the retropad */
void ra_input_clear(void);  /* release everything (while in the menu) */

/* --- saves and states (saves.c) ------------------------------------------- */

bool ra_sram_load(void);
bool ra_sram_save(void);
bool ra_state_save(int slot, char *msg, size_t msg_size);
bool ra_state_load(int slot, char *msg, size_t msg_size);

/* --- menu (menu.c) --------------------------------------------------------- */

typedef enum {
  RA_MENU_NONE = 0,   /* keep showing the menu */
  RA_MENU_RESUME,     /* back to the game */
  RA_MENU_LOADED,     /* new content was loaded */
  RA_MENU_QUIT        /* leave RetroArch */
} ra_menu_action_t;

void ra_menu_open(void);
bool ra_menu_is_open(void);
/* Runs one frame of the menu: input, logic, drawing. */
ra_menu_action_t ra_menu_tick(void);

/* The OSD line drawn over the game for a moment ("State saved" etc.). */
void ra_osd(const char *fmt, ...);
void ra_osd_draw(void);

#endif /* RA_RETROARCH_H */
