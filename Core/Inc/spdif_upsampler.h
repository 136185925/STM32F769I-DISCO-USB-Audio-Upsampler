#ifndef SPDIF_UPSAMPLER_H
#define SPDIF_UPSAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SPDIF_UPSAMPLER_FACTOR       4U
#define SPDIF_UPSAMPLER_PHASE_TAPS   32U
#define SPDIF_UPSAMPLER_DELAY_FRAMES 16U

typedef struct
{
  /* Mirrored history removes modulo operations from the real-time FIR loop. */
  int16_t left[SPDIF_UPSAMPLER_PHASE_TAPS * 2U];
  int16_t right[SPDIF_UPSAMPLER_PHASE_TAPS * 2U];
  uint32_t dither_left;
  uint32_t dither_right;
  uint8_t write_index;
} SPDIF_Upsampler4x;

void SPDIF_Upsampler4x_Reset(SPDIF_Upsampler4x *state);

/* Consume one stereo 16-bit source frame and produce four interleaved stereo
 * 16-bit frames. Phase 0 is an exact, delayed source sample; phases 1..3 are
 * 32-tap Q15 band-limited interpolation results. TPDF, when enabled, is
 * applied immediately before the final 16-bit quantization on all phases. */
void SPDIF_Upsampler4x_Process(SPDIF_Upsampler4x *state,
                               int16_t left, int16_t right,
                               int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U],
                               uint8_t attenuate_1db,
                               uint8_t tpdf_dither);

#ifdef __cplusplus
}
#endif

#endif /* SPDIF_UPSAMPLER_H */
