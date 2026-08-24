#ifndef SPDIF_WLS_LISTEN_H
#define SPDIF_WLS_LISTEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "spdif_iir_upsampler.h"

#define SPDIF_WLS_LISTEN_FACTOR 4U
#define SPDIF_WLS_LISTEN_STAGE1_MAX_PHASE_TAPS 176U
#define SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS 16U

typedef struct
{
  float stage1_left_history[SPDIF_WLS_LISTEN_STAGE1_MAX_PHASE_TAPS * 2U];
  float stage1_right_history[SPDIF_WLS_LISTEN_STAGE1_MAX_PHASE_TAPS * 2U];
  float stage2_left_history[SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS * 2U];
  float stage2_right_history[SPDIF_WLS_LISTEN_STAGE2_PHASE_TAPS * 2U];
  float noise_error_left[SPDIF_OPT_NOISE_SHAPER_ORDER];
  float noise_error_right[SPDIF_OPT_NOISE_SHAPER_ORDER];
  const float *stage1_coefficients;
  const float *stage2_coefficients;
  const float *noise_shaping_coefficients;
  float input_gain;
  uint32_t dither_left;
  uint32_t dither_right;
  uint16_t stage1_taps_per_phase;
  uint16_t stage1_write_index;
  uint8_t stage2_write_index;
} SPDIF_WlsListen4x;

/* Two cascaded 2x minimum-phase WLS stages. The first stage follows the
 * audible-band constrained WLS LISTEN profile; the short second stage removes
 * the new 2x image. Final quantization always uses TPDF plus optimized NS5. */
void SPDIF_WlsListen4x_Init(SPDIF_WlsListen4x *state,
                            uint32_t source_rate);
void SPDIF_WlsListen4x_Reset(SPDIF_WlsListen4x *state);
void SPDIF_WlsListen4x_Process(
    SPDIF_WlsListen4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_WLS_LISTEN_FACTOR * 2U]);
uint16_t SPDIF_WlsListen4x_GetTailFrames(
    const SPDIF_WlsListen4x *state);

#ifdef __cplusplus
}
#endif

#endif /* SPDIF_WLS_LISTEN_H */
