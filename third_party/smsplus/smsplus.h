/* Port glue for SMS Plus GX on AuroraOS (replaces the SDL port's smsplus.h).
 *
 * SOUND_FREQUENCY is the rate the PSG/FM chips render at. The Aurora SDK has
 * no PCM output yet and the samples are discarded, so the lowest rate the
 * core accepts above its 8 kHz floor keeps the ARM9 cycles for emulation.
 */
#ifndef SMSPLUS_H
#define SMSPLUS_H

#define SOUND_FREQUENCY 11025

#define VIDEO_WIDTH_SMS 256
#define VIDEO_HEIGHT_SMS 192
#define VIDEO_WIDTH_GG 160
#define VIDEO_HEIGHT_GG 144

#define LOCK_VIDEO
#define UNLOCK_VIDEO

typedef struct {
  char gamename[256];
  char biosdir[512];
} gamedata_t;

#endif
