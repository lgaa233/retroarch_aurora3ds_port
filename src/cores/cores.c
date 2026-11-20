#include "core.h"

#include <string.h>

const ra_core_t *const ra_cores[] = {
  &ra_core_gb,
  &ra_core_nes,
};
const int ra_core_count = (int)(sizeof(ra_cores) / sizeof(ra_cores[0]));

static int ext_matches(const char *list, const char *ext) {
  /* `list` is libretro's "gb|dmg" form. */
  size_t n = strlen(ext);
  const char *p = list;
  while (*p) {
    const char *bar = strchr(p, '|');
    size_t len = bar ? (size_t)(bar - p) : strlen(p);
    if (len == n) {
      size_t i;
      for (i = 0; i < n; i++) {
        char a = p[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) break;
      }
      if (i == n)
        return 1;
    }
    if (!bar)
      break;
    p = bar + 1;
  }
  return 0;
}

const ra_core_t *ra_core_for_path(const char *path) {
  const char *dot = strrchr(path, '.');
  int i;
  if (!dot || !dot[1])
    return NULL;
  for (i = 0; i < ra_core_count; i++) {
    if (ext_matches(ra_cores[i]->valid_extensions, dot + 1))
      return ra_cores[i];
  }
  return NULL;
}
