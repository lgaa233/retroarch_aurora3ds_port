/* Where the ARM11 scaler job lives while the app runs: the end of the PCM pool
 * (AUDIO_PCM_ADDR + 10 MB), which this app owns and does not otherwise use
 * past its sound ring. blit11.ld links the code at RA_BLIT11_CODE. */
#ifndef RA_BLIT11_H
#define RA_BLIT11_H

#define RA_BLIT11_CODE 0x23D00000u
#define RA_BLIT11_CODE_MAX 0x10000u
#define RA_BLIT11_PARAMS 0x23D80000u

#endif
