/* The libretro core interface, statically linked.
 *
 * AuroraOS has no dynamic loader, so cores cannot be .so files exporting the
 * retro_* symbols as they do in mainline RetroArch. Instead each core is
 * compiled in and exposes the same libretro entry points (same signatures,
 * same semantics, same types from libretro.h) through a descriptor struct,
 * the way RetroArch's own static (griffin) builds rename and link cores.
 */
#ifndef RA_CORES_CORE_H
#define RA_CORES_CORE_H

#include <stddef.h>
#include <stdbool.h>
#include <libretro.h>

typedef struct ra_core {
  /* retro_get_system_info() */
  const char *library_name;
  const char *library_version;
  const char *valid_extensions; /* "gb|dmg" */

  /* retro_set_environment() and the retro_set_*_cb() family. */
  void (*set_callbacks)(retro_environment_t env,
                        retro_video_refresh_t video,
                        retro_audio_sample_batch_t audio_batch,
                        retro_input_poll_t input_poll,
                        retro_input_state_t input_state);

  void (*init)(void);   /* retro_init() */
  void (*deinit)(void); /* retro_deinit() */

  bool (*load_game)(const struct retro_game_info *game);
  void (*unload_game)(void);
  void (*get_system_av_info)(struct retro_system_av_info *info);

  void (*run)(void);   /* one video frame */
  void (*reset)(void); /* retro_reset() */

  size_t (*serialize_size)(void);
  bool (*serialize)(void *data, size_t size);
  bool (*unserialize)(const void *data, size_t size);

  void *(*get_memory_data)(unsigned id);  /* RETRO_MEMORY_* */
  size_t (*get_memory_size)(unsigned id);
} ra_core_t;

extern const ra_core_t ra_core_gb;    /* Peanut-GB: Game Boy (DMG) */
extern const ra_core_t ra_core_nes;   /* agnes: NES / Famicom */
extern const ra_core_t ra_core_sms;   /* SMS Plus GX: Master System, Game
                                         Gear, SG-1000, ColecoVision */
extern const ra_core_t ra_core_chip8; /* CHIP-8 */

extern const ra_core_t *const ra_cores[];
extern const int ra_core_count;

/* The core whose valid_extensions matches `path`'s extension, or NULL. */
const ra_core_t *ra_core_for_path(const char *path);

#endif /* RA_CORES_CORE_H */
