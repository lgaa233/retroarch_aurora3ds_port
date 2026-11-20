/* An integer 2A03 APU (NTSC); see ra_nes_apu.h. MIT. */
#include "ra_nes_apu.h"

#include <string.h>

/* CPU cycles per output sample, 16.16 (1789773 * 65536 / 32768). */
#define SAMPLE_STEP_FP \
  ((uint32_t)(((uint64_t)RA_NES_APU_CPU_HZ << 16) / RA_NES_APU_RATE))

static const uint8_t length_table[32] = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

static const uint8_t duty_table[4][8] = {{0, 1, 0, 0, 0, 0, 0, 0},
                                         {0, 1, 1, 0, 0, 0, 0, 0},
                                         {0, 1, 1, 1, 1, 0, 0, 0},
                                         {1, 0, 0, 1, 1, 1, 1, 1}};

static const uint8_t tri_table[32] = {15, 14, 13, 12, 11, 10, 9,  8,
                                      7,  6,  5,  4,  3,  2,  1,  0,
                                      0,  1,  2,  3,  4,  5,  6,  7,
                                      8,  9,  10, 11, 12, 13, 14, 15};

static const uint16_t noise_period[16] = {4,   8,   16,  32,  64,  96,
                                          128, 160, 202, 254, 380, 508,
                                          762, 1016, 2034, 4068};

static const uint16_t dmc_period[16] = {428, 380, 340, 320, 286, 254,
                                        226, 214, 190, 160, 142, 128,
                                        106, 84,  72,  54};

/* Frame sequencer events, in CPU cycles from the sequence start. In the
 * 4-step sequence the last one also raises the frame IRQ; the 5-step one has
 * nothing at 29829 and its last event at 37281. */
static const int32_t fs_events[2][4] = {{7457, 14913, 22371, 29829},
                                        {7457, 14913, 22371, 37281}};
static const int32_t fs_period[2] = {29830, 37282};

typedef struct {
  uint8_t start, divider, decay, loop, constant, volume;
} env_t;

typedef struct {
  env_t env;
  uint8_t duty, seq, length, ones;
  uint8_t sweep_en, sweep_period, sweep_neg, sweep_shift, sweep_reload,
      sweep_div;
  uint8_t level, out;
  uint16_t period;
  uint32_t timer;
} pulse_t;

typedef struct {
  uint8_t control, reload, linear, reload_flag, length, seq, out;
  uint16_t period;
  uint32_t timer;
} tri_t;

typedef struct {
  env_t env;
  uint8_t mode, length, period_index, level;
  uint16_t lfsr;
  uint32_t timer;
} noise_t;

typedef struct {
  uint8_t irq_en, loop, rate, level, buffer, buffer_full, shift, bits,
      silence, irq;
  uint16_t addr_start, len_start, addr, remaining;
  uint32_t timer;
} dmc_t;

/* Everything a save state needs: between frames, `pos` and `now` are 0. */
typedef struct {
  uint32_t magic;
  pulse_t p[2];
  tri_t t;
  noise_t n;
  dmc_t d;
  uint8_t enabled, fs_mode, fs_inhibit, frame_irq, fs_step;
  int32_t fs_base, now, pos;
  int32_t smp_start, smp_next; /* the sample being summed, in CPU cycles */
  uint32_t smp_frac;           /* and the next boundary's fraction, 0.16 */
  int32_t acc;
} apu_t;

#define APU_MAGIC 0x41505531u /* "APU1" */

static apu_t apu;
static int16_t buf[RA_NES_APU_MAX_SAMPLES];
static size_t buf_count;
static ra_nes_apu_reader_t dmc_reader;
static void *dmc_reader_ctx;

/* --- units ----------------------------------------------------------------- */

static uint8_t env_volume(const env_t *e) {
  return e->constant ? e->volume : e->decay;
}

static void env_clock(env_t *e) {
  if (e->start) {
    e->start = 0;
    e->decay = 15;
    e->divider = e->volume;
  } else if (e->divider == 0) {
    e->divider = e->volume;
    if (e->decay)
      e->decay--;
    else if (e->loop)
      e->decay = 15;
  } else {
    e->divider--;
  }
}

static int32_t sweep_target(const pulse_t *p) {
  int32_t change = p->period >> p->sweep_shift;
  if (p->sweep_neg)
    return (int32_t)p->period - change - p->ones;
  return (int32_t)p->period + change;
}

static void pulse_update(pulse_t *p) {
  int muted = p->period < 8 || sweep_target(p) > 0x7FF;
  p->level = (p->length && !muted) ? env_volume(&p->env) : 0;
  p->out = duty_table[p->duty][p->seq] ? p->level : 0;
}

static void sweep_clock(pulse_t *p) {
  int32_t target = sweep_target(p);
  if (p->sweep_div == 0 && p->sweep_en && p->sweep_shift && p->period >= 8 &&
      target <= 0x7FF)
    p->period = (uint16_t)(target < 0 ? 0 : target);
  if (p->sweep_div == 0 || p->sweep_reload) {
    p->sweep_div = p->sweep_period;
    p->sweep_reload = 0;
  } else {
    p->sweep_div--;
  }
}

static void noise_update(noise_t *n) {
  n->level = n->length ? env_volume(&n->env) : 0;
}

static void quarter_frame(void) {
  env_clock(&apu.p[0].env);
  env_clock(&apu.p[1].env);
  env_clock(&apu.n.env);
  if (apu.t.reload_flag)
    apu.t.linear = apu.t.reload;
  else if (apu.t.linear)
    apu.t.linear--;
  if (!apu.t.control)
    apu.t.reload_flag = 0;
}

static void half_frame(void) {
  int i;
  for (i = 0; i < 2; i++) {
    if (!apu.p[i].env.loop && apu.p[i].length)
      apu.p[i].length--;
    sweep_clock(&apu.p[i]);
  }
  if (!apu.t.control && apu.t.length)
    apu.t.length--;
  if (!apu.n.env.loop && apu.n.length)
    apu.n.length--;
}

static void units_update(void) {
  pulse_update(&apu.p[0]);
  pulse_update(&apu.p[1]);
  noise_update(&apu.n);
}

static void fs_event(void) {
  int step = apu.fs_step;
  quarter_frame();
  if (step & 1)
    half_frame();
  if (step == 3 && apu.fs_mode == 0 && !apu.fs_inhibit)
    apu.frame_irq = 1;
  units_update();
  if (++apu.fs_step == 4) {
    apu.fs_step = 0;
    apu.fs_base += fs_period[apu.fs_mode];
  }
}

static int32_t fs_next(void) {
  return apu.fs_base + fs_events[apu.fs_mode][apu.fs_step];
}

/* --- the DMC's memory reader and output unit -------------------------------- */

static void dmc_restart(dmc_t *d) {
  d->addr = d->addr_start;
  d->remaining = d->len_start;
}

static void dmc_fetch(dmc_t *d) {
  if (d->buffer_full || !d->remaining)
    return;
  d->buffer = dmc_reader ? dmc_reader(dmc_reader_ctx, d->addr) : 0;
  d->buffer_full = 1;
  d->addr = d->addr == 0xFFFFu ? 0x8000u : (uint16_t)(d->addr + 1u);
  if (--d->remaining == 0) {
    if (d->loop)
      dmc_restart(d);
    else if (d->irq_en)
      d->irq = 1;
  }
}

static void dmc_clock(dmc_t *d) {
  if (!d->silence) {
    if (d->shift & 1) {
      if (d->level <= 125)
        d->level += 2;
    } else if (d->level >= 2) {
      d->level -= 2;
    }
  }
  d->shift >>= 1;
  if (--d->bits == 0) {
    d->bits = 8;
    if (d->buffer_full) {
      d->silence = 0;
      d->shift = d->buffer;
      d->buffer_full = 0;
      dmc_fetch(d);
    } else {
      d->silence = 1;
    }
  }
}

/* --- rendering: each channel's output summed over n CPU cycles --------------- */

static int32_t pulse_run(pulse_t *p, uint32_t n) {
  int32_t area = 0;
  uint32_t per;
  if (!p->level)
    return 0; /* its phase is inaudible while it is silent */
  per = 2u * ((uint32_t)p->period + 1u);
  while (n >= p->timer) {
    area += (int32_t)(p->timer * p->out);
    n -= p->timer;
    p->timer = per;
    p->seq = (uint8_t)((p->seq + 1u) & 7u);
    p->out = duty_table[p->duty][p->seq] ? p->level : 0;
  }
  p->timer -= n;
  return area + (int32_t)(n * p->out);
}

static int32_t tri_run(tri_t *t, uint32_t n) {
  int32_t area = 0;
  uint32_t per;
  if (!t->length || !t->linear)
    return (int32_t)(n * t->out); /* the sequencer holds its step */
  if (t->period < 2)
    return (int32_t)(n * 7u); /* ultrasonic: what a speaker makes of it */
  per = (uint32_t)t->period + 1u;
  while (n >= t->timer) {
    area += (int32_t)(t->timer * t->out);
    n -= t->timer;
    t->timer = per;
    t->seq = (uint8_t)((t->seq + 1u) & 31u);
    t->out = tri_table[t->seq];
  }
  t->timer -= n;
  return area + (int32_t)(n * t->out);
}

static int32_t noise_run(noise_t *ns, uint32_t n) {
  int32_t area = 0;
  uint32_t per, out, shift;
  if (!ns->level)
    return 0;
  per = noise_period[ns->period_index];
  shift = ns->mode ? 6u : 1u;
  out = (ns->lfsr & 1u) ? 0u : ns->level;
  while (n >= ns->timer) {
    uint32_t fb;
    area += (int32_t)(ns->timer * out);
    n -= ns->timer;
    ns->timer = per;
    fb = (ns->lfsr ^ (ns->lfsr >> shift)) & 1u;
    ns->lfsr = (uint16_t)((ns->lfsr >> 1) | (fb << 14));
    out = (ns->lfsr & 1u) ? 0u : ns->level;
  }
  ns->timer -= n;
  return area + (int32_t)(n * out);
}

static int32_t dmc_run(dmc_t *d, uint32_t n) {
  int32_t area = 0;
  uint32_t per;
  if (d->silence && !d->buffer_full && !d->remaining)
    return (int32_t)(n * d->level);
  per = dmc_period[d->rate];
  while (n >= d->timer) {
    area += (int32_t)(d->timer * d->level);
    n -= d->timer;
    d->timer = per;
    dmc_clock(d);
  }
  d->timer -= n;
  return area + (int32_t)(n * d->level);
}

/* The linear approximation of the 2A03's mixer (NESdev "APU Mixer"), in
 * units of 1e-5 of full scale. */
static int32_t mix_run(uint32_t n) {
  return 752 * (pulse_run(&apu.p[0], n) + pulse_run(&apu.p[1], n)) +
         851 * tri_run(&apu.t, n) + 494 * noise_run(&apu.n, n) +
         335 * dmc_run(&apu.d, n);
}

static void emit(int32_t boundary) {
  int32_t len = boundary - apu.smp_start;
  if (len > 0 && buf_count < RA_NES_APU_MAX_SAMPLES) {
    /* Full scale (1.0) at 0.45 of int16: the pulses, triangle and noise
     * together peak near 19000, and a DMC resting at mid-level adds ~9700
     * of DC that the frontend's DC blocker takes away. */
    int32_t s = (apu.acc / len) * 29 >> 6;
    if (s > 32767)
      s = 32767;
    buf[buf_count++] = (int16_t)s;
  }
  apu.acc = 0;
  apu.smp_start = boundary;
  apu.smp_frac += SAMPLE_STEP_FP & 0xFFFFu;
  apu.smp_next += (int32_t)(SAMPLE_STEP_FP >> 16) + (int32_t)(apu.smp_frac >> 16);
  apu.smp_frac &= 0xFFFFu;
}

static void render(int32_t to) {
  while (apu.pos < to) {
    int32_t sb = apu.smp_next;
    int32_t fe = fs_next();
    int32_t end = to;
    if (sb < end)
      end = sb;
    if (fe < end)
      end = fe;
    if (end > apu.pos) {
      apu.acc += mix_run((uint32_t)(end - apu.pos));
      apu.pos = end;
    }
    if (apu.pos == fe)
      fs_event();
    if (apu.pos == sb)
      emit(sb);
  }
}

/* --- the interface -------------------------------------------------------- */

void ra_nes_apu_init(void) { ra_nes_apu_reset(); }

void ra_nes_apu_reset(void) {
  int i;
  memset(&apu, 0, sizeof(apu));
  apu.magic = APU_MAGIC;
  for (i = 0; i < 2; i++)
    apu.p[i].timer = 1;
  apu.p[0].ones = 1;
  apu.t.timer = 1;
  apu.n.timer = 1;
  apu.n.lfsr = 1;
  apu.d.timer = dmc_period[0];
  apu.d.bits = 8;
  apu.d.silence = 1;
  apu.d.addr_start = apu.d.addr = 0xC000u;
  apu.d.len_start = 1;
  apu.smp_next = (int32_t)(SAMPLE_STEP_FP >> 16);
  apu.smp_frac = SAMPLE_STEP_FP & 0xFFFFu;
  buf_count = 0;
}

void ra_nes_apu_set_reader(ra_nes_apu_reader_t reader, void *ctx) {
  dmc_reader = reader;
  dmc_reader_ctx = ctx;
}

void ra_nes_apu_step(uint32_t cpu_cycles) { apu.now += (int32_t)cpu_cycles; }

void ra_nes_apu_write(uint16_t addr, uint8_t val) {
  pulse_t *p;
  render(apu.now);

  switch (addr) {
  case 0x4000:
  case 0x4004:
    p = &apu.p[(addr >> 2) & 1];
    p->duty = (uint8_t)(val >> 6);
    p->env.loop = (val >> 5) & 1u;
    p->env.constant = (val >> 4) & 1u;
    p->env.volume = val & 15u;
    pulse_update(p);
    break;
  case 0x4001:
  case 0x4005:
    p = &apu.p[(addr >> 2) & 1];
    p->sweep_en = (uint8_t)(val >> 7);
    p->sweep_period = (val >> 4) & 7u;
    p->sweep_neg = (val >> 3) & 1u;
    p->sweep_shift = val & 7u;
    p->sweep_reload = 1;
    pulse_update(p);
    break;
  case 0x4002:
  case 0x4006:
    p = &apu.p[(addr >> 2) & 1];
    p->period = (uint16_t)((p->period & 0x700u) | val);
    pulse_update(p);
    break;
  case 0x4003:
  case 0x4007:
    p = &apu.p[(addr >> 2) & 1];
    p->period = (uint16_t)((p->period & 0xFFu) | ((val & 7u) << 8));
    if (apu.enabled & (1u << ((addr >> 2) & 1)))
      p->length = length_table[val >> 3];
    p->seq = 0;
    p->env.start = 1;
    pulse_update(p);
    break;
  case 0x4008:
    apu.t.control = (uint8_t)(val >> 7);
    apu.t.reload = val & 0x7Fu;
    break;
  case 0x400A:
    apu.t.period = (uint16_t)((apu.t.period & 0x700u) | val);
    break;
  case 0x400B:
    apu.t.period = (uint16_t)((apu.t.period & 0xFFu) | ((val & 7u) << 8));
    if (apu.enabled & 4u)
      apu.t.length = length_table[val >> 3];
    apu.t.reload_flag = 1;
    break;
  case 0x400C:
    apu.n.env.loop = (val >> 5) & 1u;
    apu.n.env.constant = (val >> 4) & 1u;
    apu.n.env.volume = val & 15u;
    noise_update(&apu.n);
    break;
  case 0x400E:
    apu.n.mode = (uint8_t)(val >> 7);
    apu.n.period_index = val & 15u;
    break;
  case 0x400F:
    if (apu.enabled & 8u)
      apu.n.length = length_table[val >> 3];
    apu.n.env.start = 1;
    noise_update(&apu.n);
    break;
  case 0x4010:
    apu.d.irq_en = (uint8_t)(val >> 7);
    apu.d.loop = (val >> 6) & 1u;
    apu.d.rate = val & 15u;
    if (!apu.d.irq_en)
      apu.d.irq = 0;
    break;
  case 0x4011:
    apu.d.level = val & 0x7Fu;
    break;
  case 0x4012:
    apu.d.addr_start = (uint16_t)(0xC000u + (uint32_t)val * 64u);
    break;
  case 0x4013:
    apu.d.len_start = (uint16_t)((uint32_t)val * 16u + 1u);
    break;
  case 0x4015:
    apu.enabled = val & 0x1Fu;
    if (!(val & 1u))
      apu.p[0].length = 0;
    if (!(val & 2u))
      apu.p[1].length = 0;
    if (!(val & 4u))
      apu.t.length = 0;
    if (!(val & 8u))
      apu.n.length = 0;
    apu.d.irq = 0;
    if (val & 16u) {
      if (!apu.d.remaining)
        dmc_restart(&apu.d);
      dmc_fetch(&apu.d);
    } else {
      apu.d.remaining = 0;
    }
    units_update();
    break;
  case 0x4017:
    apu.fs_mode = (uint8_t)(val >> 7);
    apu.fs_inhibit = (val >> 6) & 1u;
    if (apu.fs_inhibit)
      apu.frame_irq = 0;
    apu.fs_base = apu.pos;
    apu.fs_step = 0;
    if (apu.fs_mode) {
      quarter_frame();
      half_frame();
      units_update();
    }
    break;
  default:
    break;
  }
}

uint8_t ra_nes_apu_read(uint16_t addr) {
  uint8_t r = 0;
  if (addr != 0x4015)
    return 0;
  render(apu.now);
  if (apu.p[0].length) r |= 1u;
  if (apu.p[1].length) r |= 2u;
  if (apu.t.length) r |= 4u;
  if (apu.n.length) r |= 8u;
  if (apu.d.remaining) r |= 16u;
  if (apu.frame_irq) r |= 64u;
  if (apu.d.irq) r |= 128u;
  apu.frame_irq = 0;
  return r;
}

size_t ra_nes_apu_read_samples(int16_t *out, size_t max_frames) {
  size_t i, n;
  int32_t shift;

  render(apu.now);
  n = buf_count < max_frames ? buf_count : max_frames;
  if (out) {
    for (i = 0; i < n; i++) {
      out[i * 2] = buf[i];
      out[i * 2 + 1] = buf[i];
    }
  }
  buf_count = 0;

  /* Count the next frame from 0, so the times stay small. */
  shift = apu.pos;
  apu.pos = 0;
  apu.now -= shift;
  apu.fs_base -= shift;
  apu.smp_start -= shift;
  apu.smp_next -= shift;
  return out ? n : 0;
}

size_t ra_nes_apu_state_size(void) { return sizeof(apu_t); }

void ra_nes_apu_save_state(void *dst) { memcpy(dst, &apu, sizeof(apu)); }

bool ra_nes_apu_load_state(const void *src, size_t size) {
  apu_t a;
  if (size < sizeof(a))
    return false;
  memcpy(&a, src, sizeof(a));
  if (a.magic != APU_MAGIC || a.fs_mode > 1 || a.fs_step > 3 ||
      a.p[0].duty > 3 || a.p[1].duty > 3 || a.p[0].seq > 7 ||
      a.p[1].seq > 7 || a.t.seq > 31 || a.n.period_index > 15 ||
      a.d.rate > 15 || !a.p[0].timer || !a.p[1].timer || !a.t.timer ||
      !a.n.timer || !a.d.timer || !a.d.bits || a.d.bits > 8)
    return false;
  apu = a;
  buf_count = 0;
  return true;
}
