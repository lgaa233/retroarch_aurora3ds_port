/* The ARM11 job's entry, first in the image (blit11.ld). AuroraOS's core calls
 * it through AUDIO_CMD_RUN11 with the MMU on, FCRAM cached, interrupts off,
 * and writes the caches back to memory before it acknowledges the command. */
#include "ra_blit.h"

__attribute__((section(".text.entry"))) void blit11_entry(ra_blit_t *b) {
  ra_blit_run(b);
  b->done = b->ticket;
}
