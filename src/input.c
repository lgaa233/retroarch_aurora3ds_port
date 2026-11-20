/* The input driver: the 3DS buttons as RetroPad port 0.
 *
 * The circle pad doubles as the d-pad, as in RetroArch's own 3DS port. The
 * New 3DS's ZL/ZR and C-stick cannot be read through the Aurora SDK yet.
 */
#include "retroarch.h"

#include <aurora_app.h>

static uint16_t joypad; /* bit n = RETRO_DEVICE_ID_JOYPAD_n */

void ra_input_update(void) {
  uint32_t held = hid_keys_held();
  uint16_t j = 0;

  if (held & KEY_A) j |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
  if (held & KEY_B) j |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
  if (held & KEY_X) j |= 1 << RETRO_DEVICE_ID_JOYPAD_X;
  if (held & KEY_Y) j |= 1 << RETRO_DEVICE_ID_JOYPAD_Y;
  if (held & KEY_START) j |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
  if (held & KEY_SELECT) j |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;
  if (held & (KEY_UP | KEY_CPAD_UP)) j |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
  if (held & (KEY_DOWN | KEY_CPAD_DOWN)) j |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
  if (held & (KEY_LEFT | KEY_CPAD_LEFT)) j |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
  if (held & (KEY_RIGHT | KEY_CPAD_RIGHT))
    j |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
  if (held & KEY_L) j |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
  if (held & KEY_R) j |= 1 << RETRO_DEVICE_ID_JOYPAD_R;

  joypad = j;
}

void ra_input_clear(void) { joypad = 0; }

void ra_input_poll_cb(void) {
  /* hid_scan() already ran this frame (app_loop() calls it). */
}

int16_t ra_input_state_cb(unsigned port, unsigned device, unsigned index,
                          unsigned id) {
  (void)index;
  if (port != 0)
    return 0;
  if (device == RETRO_DEVICE_JOYPAD) {
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      return (int16_t)joypad;
    if (id < 16)
      return (joypad >> id) & 1;
  }
  return 0;
}
