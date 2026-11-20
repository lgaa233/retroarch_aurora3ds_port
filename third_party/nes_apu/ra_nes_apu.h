/* An integer 2A03 APU (NTSC) for the agnes NES core: the two pulse channels,
 * the triangle, the noise and the DMC, with the frame sequencer, envelopes,
 * sweeps and length/linear counters. MIT; written for RetroArch AuroraOS
 * after the NESdev wiki's description of the hardware.
 *
 * The CPU only reports elapsed cycles (ra_nes_apu_step, cheap). Sound is
 * produced lazily, up to the current cycle, before a register is read or
 * written and at the end of the frame, each sample being the average of the
 * channels' output over the CPU cycles it covers. The output is mono, given
 * as left = right. The frame IRQ and the DMC IRQ set their $4015 flags but
 * are not raised on the CPU. */
#ifndef RA_NES_APU_H
#define RA_NES_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RA_NES_APU_RATE 32768u
#define RA_NES_APU_CPU_HZ 1789773u
/* More than a frame's worth (~546) at this rate. */
#define RA_NES_APU_MAX_SAMPLES 1024u

/* Reads CPU memory for the DMC's samples ($8000-$FFFF). */
typedef uint8_t (*ra_nes_apu_reader_t)(void *ctx, uint16_t addr);

void ra_nes_apu_init(void);
void ra_nes_apu_reset(void);
void ra_nes_apu_set_reader(ra_nes_apu_reader_t reader, void *ctx);
void ra_nes_apu_write(uint16_t addr, uint8_t val);
uint8_t ra_nes_apu_read(uint16_t addr);
void ra_nes_apu_step(uint32_t cpu_cycles);
/* Ends the frame: the samples since the last call, as stereo frames. */
size_t ra_nes_apu_read_samples(int16_t *out, size_t max_frames);

/* Save states, taken between frames. */
size_t ra_nes_apu_state_size(void);
void ra_nes_apu_save_state(void *dst);
bool ra_nes_apu_load_state(const void *src, size_t size);

#endif
