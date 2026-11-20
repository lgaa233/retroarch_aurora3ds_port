/* Minimal integer 2A03 APU for the agnes NES core (NTSC).
 * MIT — vendored for RetroArch AuroraOS; register layout follows NESdev wiki. */
#ifndef RA_NES_APU_H
#define RA_NES_APU_H

#include <stddef.h>
#include <stdint.h>

#define RA_NES_APU_RATE 32768u
#define RA_NES_APU_MAX_SAMPLES 768u

void ra_nes_apu_init(void);
void ra_nes_apu_reset(void);
void ra_nes_apu_write(uint16_t addr, uint8_t val);
uint8_t ra_nes_apu_read(uint16_t addr);
void ra_nes_apu_step(uint32_t cpu_cycles);
size_t ra_nes_apu_read_samples(int16_t *out, size_t max_frames);

#endif