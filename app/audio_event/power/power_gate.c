/*
 * Lightweight PCM energy gate for the audio event application.
 */

#include <nuttx/config.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "audio_event_config.h"
#include "power/power_gate.h"

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_POWER_ENERGY_GATE
static uint64_t isqrt_u64(uint64_t value)
{
  uint64_t result = 0;
  uint64_t bit = (uint64_t)1 << 62;

  while (bit > value)
    {
      bit >>= 2;
    }

  while (bit != 0)
    {
      if (value >= result + bit)
        {
          value -= result + bit;
          result = (result >> 1) + bit;
        }
      else
        {
          result >>= 1;
        }

      bit >>= 2;
    }

  return result;
}

static uint64_t ms_to_samples(uint32_t milliseconds)
{
  return (uint64_t)AUDIO_EVENT_SAMPLE_RATE * milliseconds / 1000;
}

static uint32_t clamp_u32(uint64_t value, uint32_t minimum,
                          uint32_t maximum)
{
  if (value < minimum)
    {
      return minimum;
    }

  if (value > maximum)
    {
      return maximum;
    }

  return (uint32_t)value;
}

static void update_thresholds(struct power_gate_s *gate)
{
  uint64_t rms = (uint64_t)gate->stats.noise_floor_rms *
                 CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_RMS_RATIO_PERMILLE /
                 1000 + CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_RMS_OFFSET;
  uint64_t peak;

  gate->stats.rms_threshold =
      clamp_u32(rms, CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_RMS_MIN,
                CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_RMS_MAX);

  peak = (uint64_t)gate->stats.rms_threshold *
         CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_PEAK_RATIO_PERMILLE / 1000;
  gate->stats.peak_threshold =
      clamp_u32(peak, CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_PEAK_MIN,
                INT16_MAX);
}

static void update_noise_floor(struct power_gate_s *gate, uint32_t rms)
{
  int64_t delta;

  if (gate->stats.noise_floor_rms == 0)
    {
      gate->stats.noise_floor_rms = rms;
      return;
    }

  delta = (int64_t)rms - gate->stats.noise_floor_rms;
  gate->stats.noise_floor_rms =
      (uint32_t)((int64_t)gate->stats.noise_floor_rms +
                 delta * CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_NOISE_ALPHA_PERMILLE /
                 1000);
}
#endif

void power_gate_init(struct power_gate_s *gate)
{
  if (gate == NULL)
    {
      return;
    }

  memset(gate, 0, sizeof(*gate));

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_POWER_ENERGY_GATE
  gate->stats.enabled = true;
#endif
}

void power_gate_process(struct power_gate_s *gate, const int16_t *samples,
                        size_t sample_count, uint64_t sample_end)
{
#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_POWER_ENERGY_GATE
  uint64_t sum_squares = 0;
  uint32_t rms;
  uint32_t peak = 0;
  size_t i;
  bool activity;

  if (gate == NULL || samples == NULL || sample_count == 0)
    {
      return;
    }

  for (i = 0; i < sample_count; i++)
    {
      int32_t sample = samples[i];
      uint32_t magnitude = sample < 0 ? (uint32_t)-(int64_t)sample :
                                        (uint32_t)sample;

      sum_squares += (uint64_t)((int64_t)sample * sample);
      if (magnitude > peak)
        {
          peak = magnitude;
        }
    }

  rms = (uint32_t)isqrt_u64(sum_squares / sample_count);
  gate->stats.blocks++;
  gate->stats.last_rms = rms;
  gate->stats.last_peak = peak;

  if (!gate->stats.calibrated)
    {
      if (gate->stats.noise_floor_rms == 0 ||
          rms < gate->stats.noise_floor_rms)
        {
          gate->stats.noise_floor_rms = rms;
        }

      gate->calibration_samples += sample_count;
      if (gate->calibration_samples >=
          ms_to_samples(CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_CALIBRATION_MS))
        {
          gate->stats.calibrated = true;
          update_thresholds(gate);
          gate->next_probe_samples = sample_end +
              ms_to_samples(CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_FORCE_PROBE_MS);
        }

      return;
    }

  update_thresholds(gate);
  activity = rms >= gate->stats.rms_threshold ||
             peak >= gate->stats.peak_threshold;

  if (activity)
    {
      if (!gate->stats.active)
        {
          gate->stats.active_entries++;
        }

      gate->stats.active = true;
      gate->active_until_samples = sample_end +
          ms_to_samples(CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_ACTIVE_HOLD_MS);
    }
  else if (gate->stats.active && sample_end >= gate->active_until_samples)
    {
      gate->stats.active = false;
    }

  if (gate->stats.active)
    {
      gate->stats.active_blocks++;
    }
  else
    {
      update_noise_floor(gate, rms);
      update_thresholds(gate);

      if (CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_FORCE_PROBE_MS > 0 &&
          sample_end >= gate->next_probe_samples)
        {
          gate->probe_pending = true;
          gate->next_probe_samples = sample_end +
              ms_to_samples(CONFIG_EXAMPLES_AUDIO_EVENT_ENERGY_GATE_FORCE_PROBE_MS);
        }
    }
#else
  (void)gate;
  (void)samples;
  (void)sample_count;
  (void)sample_end;
#endif
}

bool power_gate_take_inference(struct power_gate_s *gate,
                               uint64_t sample_end)
{
  bool run = true;

  if (gate == NULL)
    {
      return true;
    }

#ifdef CONFIG_EXAMPLES_AUDIO_EVENT_POWER_ENERGY_GATE
  if (gate->stats.calibrated && !gate->stats.active)
    {
      run = gate->probe_pending;
      gate->probe_pending = false;
    }
#else
  (void)sample_end;
#endif

  (void)sample_end;
  if (run)
    {
      gate->stats.inference_windows++;
    }
  else
    {
      gate->stats.skipped_windows++;
    }

  return run;
}

void power_gate_get_stats(const struct power_gate_s *gate,
                          struct power_gate_stats_s *stats)
{
  if (gate == NULL || stats == NULL)
    {
      return;
    }

  *stats = gate->stats;
}
