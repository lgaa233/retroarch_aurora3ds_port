/* A devkitARM/libsysbase-style <dirent.h> so the AuroraOS SDK runtime
 * (sdk/runtime/app_dirent.c) builds with a plain arm-none-eabi toolchain
 * (e.g. Ubuntu's gcc-arm-none-eabi), whose newlib does not define DIR.
 *
 * The SDK fills these structures in itself over fs_dir_open(); only the
 * layout below matters: app_dirent.c reaches the FsDir through
 * dir->dirData->dirStruct and keeps the entry in dir->fileData.
 *
 * Put this folder on CPATH (the Makefile does) so it is found before the
 * toolchain's own <dirent.h>. With devkitARM this header is not needed.
 */
#ifndef _AURORA_COMPAT_DIRENT_H_
#define _AURORA_COMPAT_DIRENT_H_

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12

struct dirent {
  ino_t d_ino;
  unsigned char d_type;
  char d_name[NAME_MAX + 1];
};

/* libsysbase's devoptab iterator; the SDK stores its FsDir in dirStruct. */
typedef struct {
  int device;
  void *dirStruct;
} DIR_ITER;

typedef struct {
  long position;
  DIR_ITER *dirData;
  struct dirent fileData;
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int readdir_r(DIR *d, struct dirent *entry, struct dirent **result);
int closedir(DIR *d);
void rewinddir(DIR *d);
long int telldir(DIR *d);
void seekdir(DIR *d, long int loc);
int alphasort(const struct dirent **a, const struct dirent **b);
int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **));

#ifdef __cplusplus
}
#endif

#endif /* _AURORA_COMPAT_DIRENT_H_ */
