/* The few Aurora SDK calls src/audio.c makes, for tests/host/test_audio.c. */
#ifndef FAKE_AURORA_APP_H
#define FAKE_AURORA_APP_H

#include <stdbool.h>
#include <stdint.h>

bool snd_available(void);
uint64_t sys_micros(void);

#endif
