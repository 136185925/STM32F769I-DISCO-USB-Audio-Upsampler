#ifndef SPDIF_IIR_UPSAMPLER_H
#define SPDIF_IIR_UPSAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "spdif_upsampler.h"

#define SPDIF_IIR_UPSAMPLER_STAGES      7U
#define SPDIF_BESSEL_UPSAMPLER_STAGES   10U
#define SPDIF_IIR_UPSAMPLER_MAX_STAGES  SPDIF_BESSEL_UPSAMPLER_STAGES
#define SPDIF_IIR_UPSAMPLER_TAIL_FRAMES 256U
#define SPDIF_HYBRID_PHASE_TAPS          32U
#define SPDIF_OPT_NOISE_SHAPER_ORDER     5U
#define SPDIF_HYBRID_PHASE_DELAY_SAMPLES 44U
#define SPDIF_BUTTERWORTH_PHASE_DELAY_SAMPLES 36U
#define SPDIF_HYBRID_UPSAMPLER_TAIL_FRAMES \
    (SPDIF_IIR_UPSAMPLER_TAIL_FRAMES + \
     SPDIF_HYBRID_PHASE_TAPS / SPDIF_UPSAMPLER_FACTOR)

typedef struct
{
  float left_state[SPDIF_IIR_UPSAMPLER_MAX_STAGES][2];
  float right_state[SPDIF_IIR_UPSAMPLER_MAX_STAGES][2];
  const float (*coefficients)[5];
  float input_gain;
  uint32_t dither_left;
  uint32_t dither_right;
  uint8_t stage_count;
} SPDIF_IirUpsampler4x;

typedef struct
{
  SPDIF_IirUpsampler4x iir;
  /* Mirrored histories let the 32-tap correction FIR run without modulo. */
  float left_history[SPDIF_HYBRID_PHASE_TAPS * 2U];
  float right_history[SPDIF_HYBRID_PHASE_TAPS * 2U];
  const float *phase_coefficients;
  const float *noise_shaping_coefficients;
  /* Previous total quantization errors for second- or fifth-order shaping. */
  float noise_error_left[SPDIF_OPT_NOISE_SHAPER_ORDER];
  float noise_error_right[SPDIF_OPT_NOISE_SHAPER_ORDER];
  uint8_t write_index;
} SPDIF_HybridUpsampler4x;

void SPDIF_IirUpsampler4x_Init(SPDIF_IirUpsampler4x *state,
                               uint32_t source_rate);
void SPDIF_IirUpsampler4x_Reset(SPDIF_IirUpsampler4x *state);

/* Insert three zero-valued phases after each input frame, pass the resulting
 * 4x stream through a 14th-order low-pass IIR, and quantize every stereo
 * phase to 16 bit with final-stage TPDF. The selected profile supplies its
 * input headroom: normally -1 dB, or -2.25 dB for the open Bessel mode. */
void SPDIF_IirUpsampler4x_Process(
    SPDIF_IirUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U]);

void SPDIF_HybridUpsampler4x_Init(SPDIF_HybridUpsampler4x *state,
                                  uint32_t source_rate);
void SPDIF_HybridUpsampler4x_InitButterworth(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate);
void SPDIF_MinimumPhaseUpsampler4x_InitBessel(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate);
void SPDIF_MinimumPhaseUpsampler4x_InitBesselOpen(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate);
void SPDIF_MinimumPhaseUpsampler4x_InitBesselNoiseShaped5(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate);
void SPDIF_HybridUpsampler4x_Reset(SPDIF_HybridUpsampler4x *state);

/* Run the same IIR interpolator and final TPDF as 4X IIR, with a 32-tap FIR
 * phase equalizer inserted before quantization. The combined passband group
 * delay targets 44 output samples and is optimized through 18 kHz. */
void SPDIF_HybridUpsampler4x_Process(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U]);

/* HYBRID with second-order error-feedback noise shaping after its phase FIR.
 * TPDF remains enabled, but the combined dither and rounding error follows
 * the (1-z^-1)^2 noise-transfer function. Saturation clears both histories. */
void SPDIF_HybridUpsampler4x_ProcessNoiseShaped2(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U]);

/* Causal minimum-phase IIR path with the phase-correction FIR bypassed. It
 * supports the selected Butterworth or Bessel coefficients and keeps the
 * same TPDF plus second-order error feedback as HYBRID NS2. */
void SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped2(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U]);

/* BES MIN with an optimized fifth-order FIR error-feedback NTF. Its five
 * unit-circle zeros minimize integrated quantization noise over 0..20 kHz;
 * the finite history cannot recursively diverge, and overload clears it. */
void SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped5(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U]);

/* Shared final-stage NS5 primitive used by both Bessel and FIR MIN paths. */
const float *SPDIF_NoiseShaper5_GetCoefficients(uint32_t output_rate);
int16_t SPDIF_NoiseShaper5_Quantize(
    float value, uint32_t *dither_state,
    float error[SPDIF_OPT_NOISE_SHAPER_ORDER],
    const float coefficients[SPDIF_OPT_NOISE_SHAPER_ORDER]);

#ifdef __cplusplus
}
#endif

#endif /* SPDIF_IIR_UPSAMPLER_H */
