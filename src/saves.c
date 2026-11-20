/* Paths, the config file, SRAM and save states. */
#include "retroarch.h"

#include <aurora_app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paths are built with snprintf and deliberately truncate at RA_PATH_MAX:
 * FAT names longer than that cannot be opened anyway. */
#pragma GCC diagnostic ignored "-Wformat-truncation"

ra_config_t ra_config = {
  .scale = RA_SCALE_FIT,
  .frameskip = 0,
  .show_fps = false,
  .fast_forward = false,
  .volume = 8,
  .last_dir = "",
};

static char dir_roms[RA_PATH_MAX];
static char dir_saves[RA_PATH_MAX];
static char dir_states[RA_PATH_MAX];
static char cfg_path[RA_PATH_MAX];

const char *ra_dir_roms(void) { return dir_roms; }
const char *ra_dir_saves(void) { return dir_saves; }
const char *ra_dir_states(void) { return dir_states; }

void ra_paths_init(void) {
  snprintf(dir_roms, sizeof(dir_roms), "/Aurora/ROMs");
  snprintf(dir_saves, sizeof(dir_saves), "%s/saves", app_dir());
  snprintf(dir_states, sizeof(dir_states), "%s/states", app_dir());
  snprintf(cfg_path, sizeof(cfg_path), "%s/retroarch.cfg", app_dir());
  fs_mkdir(dir_roms);
  fs_mkdir(dir_saves);
  fs_mkdir(dir_states);
}

/* --- retroarch.cfg: one "key = value" per line, like mainline ------------- */

void ra_config_save(void) {
  char buf[RA_PATH_MAX + 160];
  int n = snprintf(buf, sizeof(buf),
                   "scale = %d\n"
                   "frameskip = %d\n"
                   "show_fps = %d\n"
                   "fast_forward = %d\n"
                   "volume = %d\n"
                   "last_dir = %s\n",
                   ra_config.scale, ra_config.frameskip,
                   ra_config.show_fps ? 1 : 0, ra_config.fast_forward ? 1 : 0,
                   ra_config.volume, ra_config.last_dir);
  if (n > 0)
    fs_write_file(cfg_path, buf, (size_t)n);
}

void ra_config_load(void) {
  size_t size = 0;
  char *text = fs_read_file(cfg_path, &size);
  char *line, *next;

  if (!text)
    return;
  for (line = text; line; line = next) {
    char *eq;
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    /* Trim the single spaces "key = value" leaves behind. */
    {
      char *key = line, *val = eq + 1;
      size_t klen;
      while (*key == ' ') key++;
      klen = strlen(key);
      while (klen && key[klen - 1] == ' ') key[--klen] = '\0';
      while (*val == ' ') val++;

      if (!strcmp(key, "scale"))
        ra_config.scale = atoi(val);
      else if (!strcmp(key, "frameskip"))
        ra_config.frameskip = atoi(val);
      else if (!strcmp(key, "show_fps"))
        ra_config.show_fps = atoi(val) != 0;
      else if (!strcmp(key, "fast_forward"))
        ra_config.fast_forward = atoi(val) != 0;
      else if (!strcmp(key, "volume"))
        ra_config.volume = atoi(val);
      else if (!strcmp(key, "last_dir")) {
        strncpy(ra_config.last_dir, val, sizeof(ra_config.last_dir) - 1);
        ra_config.last_dir[sizeof(ra_config.last_dir) - 1] = '\0';
      }
    }
  }
  free(text);

  if (ra_config.scale < RA_SCALE_1X || ra_config.scale > RA_SCALE_FULL)
    ra_config.scale = RA_SCALE_FIT;
  if (ra_config.frameskip < 0 || ra_config.frameskip > 4)
    ra_config.frameskip = 0;
  if (ra_config.volume < 0 || ra_config.volume > 10)
    ra_config.volume = 8;
}

/* --- SRAM ------------------------------------------------------------------ */

static void sram_path(char *out, size_t out_size) {
  snprintf(out, out_size, "%s/%s.srm", dir_saves, ra_content.rom_name);
}

bool ra_sram_load(void) {
  char path[RA_PATH_MAX];
  void *data;
  size_t size = 0;
  void *mem;
  size_t mem_size;

  if (!ra_content.core)
    return false;
  mem = ra_content.core->get_memory_data(RETRO_MEMORY_SAVE_RAM);
  mem_size = ra_content.core->get_memory_size(RETRO_MEMORY_SAVE_RAM);
  if (!mem || !mem_size)
    return false;

  sram_path(path, sizeof(path));
  data = fs_read_file(path, &size);
  if (!data)
    return false;
  if (size > mem_size)
    size = mem_size;
  memcpy(mem, data, size);
  free(data);
  return true;
}

bool ra_sram_save(void) {
  char path[RA_PATH_MAX];
  void *mem;
  size_t mem_size;

  if (!ra_content.core)
    return false;
  mem = ra_content.core->get_memory_data(RETRO_MEMORY_SAVE_RAM);
  mem_size = ra_content.core->get_memory_size(RETRO_MEMORY_SAVE_RAM);
  if (!mem || !mem_size)
    return false;

  sram_path(path, sizeof(path));
  return fs_write_file(path, mem, mem_size);
}

/* --- save states ----------------------------------------------------------- */

static void state_path(char *out, size_t out_size, int slot) {
  snprintf(out, out_size, "%s/%s.state%d", dir_states, ra_content.rom_name,
           slot);
}

bool ra_state_save(int slot, char *msg, size_t msg_size) {
  char path[RA_PATH_MAX];
  size_t size;
  void *buf;
  bool ok;

  if (!ra_content.core || !ra_content.running) {
    snprintf(msg, msg_size, "No content running");
    return false;
  }
  size = ra_content.core->serialize_size();
  if (!size) {
    snprintf(msg, msg_size, "Core cannot save states");
    return false;
  }
  buf = malloc(size);
  if (!buf) {
    snprintf(msg, msg_size, "Out of memory (%u KB)", (unsigned)(size / 1024));
    return false;
  }
  ok = ra_content.core->serialize(buf, size);
  if (ok) {
    state_path(path, sizeof(path), slot);
    ok = fs_write_file(path, buf, size);
    snprintf(msg, msg_size, ok ? "Saved state to slot %d" : "SD write failed",
             slot);
  } else {
    snprintf(msg, msg_size, "Core failed to serialize");
  }
  free(buf);
  return ok;
}

bool ra_state_load(int slot, char *msg, size_t msg_size) {
  char path[RA_PATH_MAX];
  void *buf;
  size_t size = 0;
  bool ok;

  if (!ra_content.core || !ra_content.running) {
    snprintf(msg, msg_size, "No content running");
    return false;
  }
  state_path(path, sizeof(path), slot);
  buf = fs_read_file(path, &size);
  if (!buf) {
    snprintf(msg, msg_size, "No state in slot %d", slot);
    return false;
  }
  ok = ra_content.core->unserialize(buf, size);
  snprintf(msg, msg_size, ok ? "Loaded state from slot %d" : "Bad state file",
           slot);
  free(buf);
  return ok;
}
