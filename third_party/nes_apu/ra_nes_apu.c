/* NTSC 2A03 APU: integer-only, stepped with CPU cycles from agnes. */
#include "ra_nes_apu.h"

#include <string.h>

#define CPU_NTSC 1789773u

enum {
  REG_P1_DUTY = 0x4000,
  REG_P1_TIMER = 0x4002,
  REG_P1_SWEEP = 0x4001,
  REG_P1_LEN = 0x4003,
  REG_P2_DUTY = 0x4004,
  REG_P2_SWEEP = 0x4005,
  REG_P2_TIMER = 0x4006,
  REG_P2_LEN = 0x4007,
  REG_TRI_LINEAR = 0x4008,
  REG_TRI_TIMER = 0x400a,
  REG_TRI_LEN = 0x400b,
  REG_NOISE_CTRL = 0x400c,
  REG_NOISE_PERIOD = 0x400e,
  REG_NOISE_LEN = 0x400f,
  REG_DMC_CTRL = 0x4010,
  REG_DMC_DA = 0x4011,
  REG_DMC_ADDR = 0x4012,
  REG_DMC_LEN = 0x4013,
  REG_STATUS = 0x4015,
  REG_FRAME = 0x4017
};

static const uint8_t length_table[32] = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

static const uint16_t noise_period[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

static const uint8_t duty_tab[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};

static const int16_t tri_tab[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

typedef struct {
  uint8_t duty, const_vol, halt, vol_env;
  uint8_t duty_pos, env_div, env_vol;
  uint16_t timer_period, timer;
  uint8_t len;
  uint8_t sweep_en, neg, shift, sweep_period, sweep_reload;
  uint16_t sweep_timer;
  uint8_t enabled, sweep_mute;
  uint8_t id;
} pulse_t;

typedef struct {
  uint8_t linear_reload, linear, halt;
  uint8_t len;
  uint16_t timer_period, timer;
  uint8_t step;
  uint8_t enabled;
} tri_t;

typedef struct {
  uint8_t const_vol, halt, vol_env;
  uint8_t mode;
  uint8_t env_div, env_vol;
  uint16_t period, timer;
  uint16_t lfsr;
  uint8_t len;
  uint8_t enabled;
} noise_t;

typedef struct {
  uint8_t ctrl, loop, irq;
  uint16_t rate, rate_timer;
  uint16_t sample_addr, sample_len;
  uint8_t out_level;
  uint8_t enabled;
  uint8_t irq_flag;
} dmc_t;

static pulse_t pulse[2];
static tri_t tri;
static noise_t noise;
static dmc_t dmc;
static uint8_t status_reg;
static uint8_t frame_mode, inhibit_irq;
static uint32_t frame_timer;
static uint8_t frame_step;
static uint32_t sample_acc;
static uint32_t cycle_acc;

#define RA_APU_BUF 768
static int16_t sample_buf[RA_APU_BUF * 2];
static size_t sample_count;

static void clock_pulse_length(pulse_t *p) {
  if (!p->halt && p->len)
    p->len--;
}

static void clock_tri_length(void) {
  if (!tri.halt && tri.len)
    tri.len--;
}

static void clock_noise_length(void) {
  if (!noise.halt && noise.len)
    noise.len--;
}

static void clock_envelope(uint8_t halt, uint8_t *div, uint8_t *vol) {
  if (*div == 0) {
    *div = 15;
    if (*vol > 0)
      (*vol)--;
    else if (halt)
      *vol = 15;
  } else
    (*div)--;
}

static uint16_t pulse_target_period(pulse_t *p) {
  uint16_t period = p->timer_period;
  uint16_t change = period >> p->shift;
  if (p->neg)
    return period - change + (p->id ? p->timer_period : 0);
  return period + change;
}

static void clock_sweep(pulse_t *p) {
  if (p->sweep_reload) {
    p->sweep_reload = 0;
    p->sweep_timer = p->sweep_period;
  } else if (p->sweep_timer) {
    p->sweep_timer--;
  } else {
    p->sweep_timer = p->sweep_period;
    if (p->sweep_en && p->shift && p->timer_period >= 8) {
      uint16_t target = pulse_target_period(p);
      if (target < 0x800) {
        p->timer_period = target;
        p->timer = target;
      }
    }
  }
  p->sweep_mute = (p->timer_period < 8) || (p->neg && pulse_target_period(p) > 0x7FF);
}

static void frame_quarter(void) {
  int i;
  for (i = 0; i < 2; i++) {
    clock_pulse_length(&pulse[i]);
    clock_envelope(pulse[i].halt, &pulse[i].env_div, &pulse[i].env_vol);
    clock_sweep(&pulse[i]);
  }
  clock_tri_length();
  if (tri.linear)
    tri.linear--;
  clock_noise_length();
  clock_envelope(noise.halt, &noise.env_div, &noise.env_vol);
}

static void frame_half(void) { frame_quarter(); }

static void run_frame_seq(uint32_t cycles) {
  while (cycles--) {
    if (!frame_timer) {
      if (frame_mode == 0) {
        static const uint8_t steps[] = {1, 2, 0, 0, 1, 2, 0, 0, 0};
        uint8_t act = steps[frame_step & 7u];
        if (act == 1)
          frame_quarter();
        else if (act == 2)
          frame_half();
        frame_step++;
        frame_timer = 7457;
      } else {
        static const uint8_t steps[] = {0, 1, 0, 1, 2, 0, 0, 0, 0};
        uint8_t act = steps[frame_step & 7u];
        if (act == 1)
          frame_quarter();
        else if (act == 2) {
          frame_half();
          if (!inhibit_irq)
            dmc.irq_flag = 1;
        }
        frame_step++;
        frame_timer = 7457;
      }
    } else
      frame_timer--;
  }
}

static int16_t mix_sample(void) {
  int32_t o = 0;
  int i;
  for (i = 0; i < 2; i++) {
    if (pulse[i].enabled && pulse[i].len && !pulse[i].sweep_mute) {
      uint8_t vol = pulse[i].const_vol ? pulse[i].vol_env : pulse[i].env_vol;
      if (duty_tab[pulse[i].duty][pulse[i].duty_pos])
        o += vol;
      else
        o -= vol;
    }
  }
  if (tri.enabled && tri.len && tri.linear)
    o += (tri_tab[tri.step] - 8) * 2;
  if (noise.enabled && noise.len) {
    uint8_t vol = noise.const_vol ? noise.vol_env : noise.env_vol;
    uint8_t bit = noise.mode ? 6 : 1;
    if (noise.lfsr & 1)
      o -= vol;
    else
      o += vol;
    (void)bit;
  }
  if (dmc.enabled)
    o += ((int)dmc.out_level) - 64;
  if (o > 12000)
    o = 12000;
  if (o < -12000)
    o = -12000;
  return (int16_t)o;
}

static void emit_sample(void) {
  int16_t s;
  if (sample_count >= RA_APU_BUF)
    return;
  s = mix_sample();
  sample_buf[sample_count * 2] = s;
  sample_buf[sample_count * 2 + 1] = s;
  sample_count++;
}

static void step_channel_timers(uint32_t n) {
  while (n--) {
    run_frame_seq(1);
    if (!pulse[0].timer) {
      pulse[0].timer = pulse[0].timer_period;
      pulse[0].duty_pos = (pulse[0].duty_pos + 1u) & 7u;
    } else if (pulse[0].timer)
      pulse[0].timer--;
    if (!pulse[1].timer) {
      pulse[1].timer = pulse[1].timer_period;
      pulse[1].duty_pos = (pulse[1].duty_pos + 1u) & 7u;
    } else if (pulse[1].timer)
      pulse[1].timer--;
    if (!tri.timer) {
      if (tri.len && tri.linear)
        tri.step = (tri.step + 1u) & 31u;
      tri.timer = tri.timer_period;
    } else if (tri.timer)
      tri.timer--;
    if (!noise.timer) {
      noise.lfsr = (uint16_t)((noise.lfsr & 0x7FFFu) << 1);
      if (noise.mode) {
        if (((noise.lfsr >> 6) ^ noise.lfsr) & 1)
          noise.lfsr |= 1;
      } else {
        if (((noise.lfsr >> 1) ^ noise.lfsr) & 1)
          noise.lfsr |= 1;
      }
      noise.timer = noise.period;
    } else if (noise.timer)
      noise.timer--;

    cycle_acc++;
    sample_acc += RA_NES_APU_RATE;
    while (sample_acc >= CPU_NTSC) {
      sample_acc -= CPU_NTSC;
      emit_sample();
    }
  }
}

void ra_nes_apu_init(void) { ra_nes_apu_reset(); }

void ra_nes_apu_reset(void) {
  memset(pulse, 0, sizeof(pulse));
  memset(&tri, 0, sizeof(tri));
  memset(&noise, 0, sizeof(noise));
  memset(&dmc, 0, sizeof(dmc));
  pulse[1].id = 1;
  noise.lfsr = 1;
  status_reg = 0;
  frame_mode = 0;
  inhibit_irq = 1;
  frame_timer = 0;
  frame_step = 0;
  sample_acc = 0;
  cycle_acc = 0;
  sample_count = 0;
}

void ra_nes_apu_write(uint16_t addr, uint8_t val) {
  pulse_t *p;
  switch (addr) {
  case REG_P1_DUTY:
  case REG_P2_DUTY:
    p = &pulse[(addr == REG_P2_DUTY) ? 1 : 0];
    p->duty = (val >> 6) & 3;
    p->halt = (val >> 5) & 1;
    p->const_vol = (val >> 4) & 1;
    p->vol_env = val & 15;
    p->env_div = 15;
    break;
  case REG_P1_SWEEP:
    pulse[0].sweep_period = (val >> 4) & 0x0Fu;
    pulse[0].neg = (val >> 3) & 1;
    pulse[0].shift = val & 7;
    pulse[0].sweep_en = pulse[0].shift != 0;
    pulse[0].sweep_reload = 1;
    break;
  case REG_P2_SWEEP:
    pulse[1].sweep_period = (val >> 4) & 0x0Fu;
    pulse[1].neg = (val >> 3) & 1;
    pulse[1].shift = val & 7;
    pulse[1].sweep_en = pulse[1].shift != 0;
    pulse[1].sweep_reload = 1;
    break;
  case REG_P1_TIMER:
    pulse[0].timer_period = (pulse[0].timer_period & 0x700u) | val;
    pulse[0].timer = pulse[0].timer_period;
    break;
  case REG_P2_TIMER:
    pulse[1].timer_period = (pulse[1].timer_period & 0x700u) | val;
    pulse[1].timer = pulse[1].timer_period;
    break;
  case REG_P1_LEN:
    pulse[0].timer_period = (uint16_t)((pulse[0].timer_period & 0xFFu) | ((val & 7u) << 8));
    pulse[0].timer = pulse[0].timer_period;
    pulse[0].duty_pos = 0;
    if (pulse[0].enabled)
      pulse[0].len = length_table[(val >> 3) & 31];
    pulse[0].env_div = 15;
    pulse[0].env_vol = pulse[0].vol_env;
    break;
  case REG_P2_LEN:
    pulse[1].timer_period = (uint16_t)((pulse[1].timer_period & 0xFFu) | ((val & 7u) << 8));
    pulse[1].timer = pulse[1].timer_period;
    pulse[1].duty_pos = 0;
    if (pulse[1].enabled)
      pulse[1].len = length_table[(val >> 3) & 31];
    pulse[1].env_div = 15;
    pulse[1].env_vol = pulse[1].vol_env;
    break;
  case REG_TRI_LINEAR:
    tri.halt = (val >> 7) & 1;
    tri.linear_reload = val & 0x7F;
    break;
  case REG_TRI_TIMER:
    tri.timer_period = (tri.timer_period & 0x700u) | val;
    tri.timer = tri.timer_period;
    break;
  case REG_TRI_LEN:
    tri.timer_period = (uint16_t)((tri.timer_period & 0xFFu) | ((val & 7u) << 8));
    tri.timer = tri.timer_period;
    tri.step = 0;
    if (tri.enabled)
      tri.len = length_table[(val >> 3) & 31];
    tri.linear = tri.linear_reload;
    break;
  case REG_NOISE_CTRL:
    noise.halt = (val >> 5) & 1;
    noise.const_vol = (val >> 4) & 1;
    noise.vol_env = val & 15;
    noise.env_div = 15;
    break;
  case REG_NOISE_PERIOD:
    noise.mode = (val >> 7) & 1;
    noise.period = noise_period[val & 15];
    noise.timer = noise.period;
    break;
  case REG_NOISE_LEN:
    if (noise.enabled)
      noise.len = length_table[(val >> 3) & 31];
    noise.env_div = 15;
    noise.env_vol = noise.vol_env;
    break;
  case REG_DMC_CTRL:
    dmc.irq = (val >> 7) & 1;
    dmc.loop = (val >> 6) & 1;
    dmc.rate = (uint16_t)(428u + 3808u * (val & 15u));
    dmc.rate_timer = dmc.rate;
    break;
  case REG_DMC_DA:
    dmc.out_level = val & 0x7F;
    break;
  case REG_DMC_ADDR:
    dmc.sample_addr = (uint16_t)(0xC000u | (val << 6));
    break;
  case REG_DMC_LEN:
    dmc.sample_len = (uint16_t)(((val << 4) | 1) << 3);
    break;
  case REG_STATUS:
    pulse[0].enabled = val & 1;
    pulse[1].enabled = (val >> 1) & 1;
    tri.enabled = (val >> 2) & 1;
    noise.enabled = (val >> 3) & 1;
    dmc.enabled = (val >> 4) & 1;
      if (!pulse[0].enabled)
      pulse[0].len = 0;
    if (!pulse[1].enabled)
      pulse[1].len = 0;
    if (!tri.enabled)
      tri.len = 0;
    if (!noise.enabled)
      noise.len = 0;
    dmc.irq_flag = 0;
    status_reg = (uint8_t)(status_reg & 0x1F);
    break;
  case REG_FRAME:
    inhibit_irq = (val >> 6) & 1;
    frame_mode = (val >> 7) & 1;
    frame_step = 0;
    frame_timer = 0;
    if (inhibit_irq)
      dmc.irq_flag = 0;
    break;
  default:
    break;
  }
}

uint8_t ra_nes_apu_read(uint16_t addr) {
  uint8_t res = 0;
  if (addr != REG_STATUS)
    return 0;
  res = (uint8_t)((pulse[0].len ? 0x01 : 0) | (pulse[1].len ? 0x02 : 0) |
                  (tri.len ? 0x04 : 0) | (noise.len ? 0x08 : 0) |
                  (dmc.sample_len ? 0x10 : 0));
  if (dmc.irq_flag)
    res |= 0x80;
  dmc.irq_flag = 0;
  return res;
}

void ra_nes_apu_step(uint32_t cpu_cycles) {
  step_channel_timers(cpu_cycles);
}

size_t ra_nes_apu_read_samples(int16_t *out, size_t max_frames) {
  size_t n = sample_count;
  if (n > max_frames)
    n = max_frames;
  if (n)
    memcpy(out, sample_buf, n * 2 * sizeof(int16_t));
  sample_count = 0;
  return n;
}