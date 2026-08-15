#include "spdif_upsampler.h"

#include "cmsis_compiler.h"

#include <string.h>

#define SPDIF_UPSAMPLER_GAIN_MINUS_1DB_Q31 1913946816LL
#define SPDIF_UPSAMPLER_DITHER_LEFT_SEED    0xA341316CU
#define SPDIF_UPSAMPLER_DITHER_RIGHT_SEED   0xC8013EA4U

/* 129-point, 4th-band Nyquist prototype sampled into three 32-tap fractional
 * phases. The omitted phase contains one unity coefficient at the 64-sample
 * center and is implemented as the exact Phase-0 bypass below. Coefficients
 * use a Kaiser beta=4 window and each phase is normalized to Q15 unity DC.
 * This gives a practical balance between 20 kHz flatness and image rejection
 * for both 44.1 -> 176.4 kHz and 48 -> 192 kHz conversion. */
static const int16_t spdif_upsampler_coefficients[3][32] =
{
  {
    -47, 75, -112, 158, -216, 288, -376, 485,
    -622, 795, -1021, 1331, -1789, 2551, -4133, 9809,
    29527, -5846, 3171, -2115, 1535, -1163, 900, -703,
    550, -428, 330, -250, 186, -134, 92, -60
  },
  {
    -75, 118, -173, 243, -329, 436, -568, 731,
    -935, 1196, -1541, 2021, -2746, 4006, -6860, 20858,
    20862, -6860, 4006, -2746, 2021, -1541, 1196, -935,
    731, -568, 436, -329, 243, -173, 118, -75
  },
  {
    -60, 92, -134, 186, -250, 330, -428, 550,
    -703, 900, -1163, 1535, -2115, 3171, -5846, 29527,
    9809, -4133, 2551, -1789, 1331, -1021, 795, -622,
    485, -376, 288, -216, 158, -112, 75, -47
  }
};

static uint32_t SPDIF_Upsampler4x_Random(uint32_t *state)
{
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

static int64_t SPDIF_Upsampler4x_Tpdf(uint32_t *state, uint32_t shift)
{
  /* Difference of two independent U[0,32767] values is triangular and spans
   * almost exactly +/- one output LSB (two LSB peak-to-peak). Scaling it into
   * the accumulator domain keeps the dither ahead of the final quantizer. */
  const int32_t first =
      (int32_t)(SPDIF_Upsampler4x_Random(state) >> 17);
  const int32_t second =
      (int32_t)(SPDIF_Upsampler4x_Random(state) >> 17);
  return (int64_t)(first - second) * (1LL << (shift - 15U));
}

static int16_t SPDIF_Upsampler4x_Quantize(int64_t accumulator,
                                          uint8_t attenuate_1db,
                                          uint8_t tpdf_dither,
                                          uint32_t *dither_state)
{
  uint32_t shift = 15U;
  int64_t rounded;
  if (attenuate_1db != 0U)
  {
    /* Q30 FIR accumulator x Q31 gain -> Q61. Shift directly to Q15 so the
     * gain and FIR share one final deterministic rounding operation. */
    accumulator *= SPDIF_UPSAMPLER_GAIN_MINUS_1DB_Q31;
    shift = 46U;
  }
  if (tpdf_dither != 0U)
  {
    accumulator += SPDIF_Upsampler4x_Tpdf(dither_state, shift);
  }
  if (accumulator >= 0)
  {
    rounded = (accumulator + (1LL << (shift - 1U))) >> shift;
  }
  else
  {
    rounded = -(((-accumulator) + (1LL << (shift - 1U))) >> shift);
  }
  if (rounded > 32767LL) return 32767;
  if (rounded < -32768LL) return -32768;
  return (int16_t)rounded;
}

static void SPDIF_Upsampler4x_AccumulateStereo(
    const int16_t *left_current, const int16_t *right_current,
    const int16_t *coefficients, int64_t *left_result,
    int64_t *right_result)
{
  int64_t left_accumulator = 0;
  int64_t right_accumulator = 0;
  /* Samples are traversed newest-to-oldest. A packed history word contains
   * [older,newer], so SMLALDX exchanges the coefficient halfwords and performs
   * two signed Q15 MACs per Cortex-M7 DSP instruction. */
  for (uint32_t pair = 0U;
       pair < (SPDIF_UPSAMPLER_PHASE_TAPS / 2U); ++pair)
  {
    const uint32_t left_samples =
        __UNALIGNED_UINT32_READ(left_current - 1);
    const uint32_t right_samples =
        __UNALIGNED_UINT32_READ(right_current - 1);
    const uint32_t packed_coefficients =
        __UNALIGNED_UINT32_READ(coefficients);
    left_accumulator = (int64_t)__SMLALDX(
        left_samples, packed_coefficients, (uint64_t)left_accumulator);
    right_accumulator = (int64_t)__SMLALDX(
        right_samples, packed_coefficients, (uint64_t)right_accumulator);
    left_current -= 2;
    right_current -= 2;
    coefficients += 2;
  }
  *left_result = left_accumulator;
  *right_result = right_accumulator;
}

void SPDIF_Upsampler4x_Reset(SPDIF_Upsampler4x *state)
{
  if (state != NULL)
  {
    memset(state, 0, sizeof(*state));
    state->dither_left = SPDIF_UPSAMPLER_DITHER_LEFT_SEED;
    state->dither_right = SPDIF_UPSAMPLER_DITHER_RIGHT_SEED;
  }
}

void SPDIF_Upsampler4x_Process(SPDIF_Upsampler4x *state,
                               int16_t left, int16_t right,
                               int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U],
                               uint8_t attenuate_1db,
                               uint8_t tpdf_dither)
{
  const uint32_t write = state->write_index;
  state->left[write] = left;
  state->right[write] = right;
  state->left[write + SPDIF_UPSAMPLER_PHASE_TAPS] = left;
  state->right[write + SPDIF_UPSAMPLER_PHASE_TAPS] = right;

  /* The 4th-band prototype has exact zero crossings on Phase 0. Copying the
   * delayed source sample guarantees that every fourth output sample remains
   * bit-identical to the original 16-bit PCM. */
  const uint32_t anchor = write + SPDIF_UPSAMPLER_PHASE_TAPS -
                          SPDIF_UPSAMPLER_DELAY_FRAMES;
  if (attenuate_1db != 0U)
  {
    output[0] = SPDIF_Upsampler4x_Quantize(
        (int64_t)state->left[anchor] * 32768LL, 1U, tpdf_dither,
        &state->dither_left);
    output[1] = SPDIF_Upsampler4x_Quantize(
        (int64_t)state->right[anchor] * 32768LL, 1U, tpdf_dither,
        &state->dither_right);
  }
  else
  {
    output[0] = state->left[anchor];
    output[1] = state->right[anchor];
  }

  for (uint32_t phase = 0U; phase < 3U; ++phase)
  {
    const int16_t *left_current =
        &state->left[write + SPDIF_UPSAMPLER_PHASE_TAPS];
    const int16_t *right_current =
        &state->right[write + SPDIF_UPSAMPLER_PHASE_TAPS];
    int64_t left_accumulator;
    int64_t right_accumulator;
    SPDIF_Upsampler4x_AccumulateStereo(
        left_current, right_current, spdif_upsampler_coefficients[phase],
        &left_accumulator, &right_accumulator);
    output[(phase + 1U) * 2U] =
        SPDIF_Upsampler4x_Quantize(left_accumulator, attenuate_1db,
                                   tpdf_dither, &state->dither_left);
    output[(phase + 1U) * 2U + 1U] =
        SPDIF_Upsampler4x_Quantize(right_accumulator, attenuate_1db,
                                   tpdf_dither, &state->dither_right);
  }
  state->write_index = (uint8_t)((write + 1U) &
                                  (SPDIF_UPSAMPLER_PHASE_TAPS - 1U));
}
