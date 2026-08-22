#ifndef SPDIF_MINPHASE_FIR_H
#define SPDIF_MINPHASE_FIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "spdif_iir_upsampler.h"

#define SPDIF_MINPHASE_FIR_FACTOR              4U
#define SPDIF_MINPHASE_FIR_MAX_TAPS_PER_PHASE 64U
#define SPDIF_MINPHASE_FIR_TAIL_FRAMES         64U

typedef struct
{
  /* Mirrored histories remove modulo operations from every FIR tap. */
  float left_history[SPDIF_MINPHASE_FIR_MAX_TAPS_PER_PHASE * 2U];
  float right_history[SPDIF_MINPHASE_FIR_MAX_TAPS_PER_PHASE * 2U];
  float noise_error_left[SPDIF_OPT_NOISE_SHAPER_ORDER];
  float noise_error_right[SPDIF_OPT_NOISE_SHAPER_ORDER];
  const float *coefficients;
  const float *noise_shaping_coefficients;
  float input_gain;
  uint32_t dither_left;
  uint32_t dither_right;
  uint8_t taps_per_phase;
  uint8_t write_index;
} SPDIF_MinimumPhaseFir4x;

/* Kaiser beta=8.9 prototypes transformed offline by real-cepstrum spectral
 * factorization. 44.1 kHz uses 256 taps and 48 kHz uses 160 taps. */
void SPDIF_MinimumPhaseFir4x_Init(SPDIF_MinimumPhaseFir4x *state,
                                  uint32_t source_rate);
/* Weighted constrained/least-squares prototype with 1:100 pass/stop energy
 * weights and -0.3 dB input headroom. The real-time polyphase core is shared. */
void SPDIF_WeightedMinimumPhaseFir4x_Init(SPDIF_MinimumPhaseFir4x *state,
                                          uint32_t source_rate);
void SPDIF_MinimumPhaseFir4x_Reset(SPDIF_MinimumPhaseFir4x *state);
void SPDIF_MinimumPhaseFir4x_Process(
    SPDIF_MinimumPhaseFir4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_MINPHASE_FIR_FACTOR * 2U]);

#ifdef __cplusplus
}
#endif

#endif /* SPDIF_MINPHASE_FIR_H */
