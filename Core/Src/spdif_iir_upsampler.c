#include "spdif_iir_upsampler.h"

#include <string.h>

#define SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB 3.56500363f
#define SPDIF_IIR_DITHER_LEFT_SEED         0x9E3779B9U
#define SPDIF_IIR_DITHER_RIGHT_SEED        0x243F6A88U

/* 14th-order Chebyshev-I low-pass, seven unity-DC-gain DF2T sections.
 * The 0.25 dB passband ends at 20 kHz. Separate coefficient sets retain the
 * same acoustic cutoff at 176.4 and 192 kHz output rates. Coefficients are
 * stored as b0, b1, b2, +a1, +a2 for the DF2T update used below. */
static const float spdif_iir_coefficients_176k4
    [SPDIF_IIR_UPSAMPLER_STAGES][5] =
{
  { 0.004417295859f, 0.008834591719f, 0.004417295859f,
    1.781084930f, -0.7987541134f },
  { 0.01626590794f, 0.03253181589f, 0.01626590794f,
    1.745094631f, -0.8101582623f },
  { 0.03723779658f, 0.07447559317f, 0.03723779658f,
    1.682707091f, -0.8316582769f },
  { 0.06283676279f, 0.1256735256f, 0.06283676279f,
    1.609805693f, -0.8611527441f },
  { 0.08811408336f, 0.1762281667f, 0.08811408336f,
    1.543992531f, -0.8964488641f },
  { 0.1087105623f, 0.2174211246f, 0.1087105623f,
    1.500943810f, -0.9357860589f },
  { 0.1213756712f, 0.2427513423f, 0.1213756712f,
    1.492514708f, -0.9780173926f }
};

static const float spdif_iir_coefficients_192k
    [SPDIF_IIR_UPSAMPLER_STAGES][5] =
{
  { 0.003712502370f, 0.007425004741f, 0.003712502370f,
    1.799767954f, -0.8146179636f },
  { 0.01369117210f, 0.02738234420f, 0.01369117210f,
    1.770095691f, -0.8248603793f },
  { 0.03142515918f, 0.06285031835f, 0.03142515918f,
    1.718589665f, -0.8442903014f },
  { 0.05318990039f, 0.1063798008f, 0.05318990039f,
    1.658420468f, -0.8711800697f },
  { 0.07479113672f, 0.1495822734f, 0.07479113672f,
    1.604499132f, -0.9036636787f },
  { 0.09243628602f, 0.1848725720f, 0.09243628602f,
    1.570409500f, -0.9401546438f },
  { 0.1032340830f, 0.2064681660f, 0.1032340830f,
    1.566570880f, -0.9795072119f }
};

/* 32-tap causal phase equalizers fitted offline against each IIR response.
 * They approximate exp(-j*44*w) / exp(j*arg(H_iir)) in the 0..18 kHz band.
 * This revision halves the real-time FIR load so a complete 192 kHz DMA half
 * remains comfortably bounded. DC gain is one and passband ripple is small. */
static const float spdif_phase_coefficients_176k4
    [SPDIF_HYBRID_PHASE_TAPS] =
{
  0.1246455262f, -0.1598618000f, -0.1231661113f, 0.03412563755f,
  0.1502830726f, 0.1389845895f, 0.02954015493f, -0.1032348437f,
  -0.1674913273f, -0.1331955632f, -0.01667468517f, 0.1077778743f,
  0.1784206579f, 0.1523701980f, 0.05261340892f, -0.07440183856f,
  -0.1586661292f, -0.1702725887f, -0.1057157722f, -0.01230467694f,
  0.06828892905f, 0.09851359234f, 0.09206260585f, 0.07604335776f,
  0.09533829395f, 0.1510707064f, 0.2172888026f, 0.2260977226f,
  0.1436328580f, -0.005866759972f, -0.07408753121f, 0.1678416391f
};

static const float spdif_phase_coefficients_192k
    [SPDIF_HYBRID_PHASE_TAPS] =
{
  -0.1482425251f, 0.08893691936f, 0.1645981226f, 0.08670470615f,
  -0.04799743396f, -0.1534544966f, -0.1612691368f, -0.08104852964f,
  0.04933812500f, 0.1529975451f, 0.1881014228f, 0.1305022525f,
  0.01745149505f, -0.1080979805f, -0.1828253817f, -0.1869670250f,
  -0.1153289060f, -0.01238213480f, 0.08629858662f, 0.1359392892f,
  0.1383882815f, 0.1034580768f, 0.07636940762f, 0.07654126389f,
  0.1195250608f, 0.1709837962f, 0.1985838454f, 0.1572284855f,
  0.05794067397f, -0.05505257855f, -0.06956457477f, 0.1223433475f
};

static uint32_t SPDIF_IirUpsampler4x_Random(uint32_t *state)
{
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

static float SPDIF_IirUpsampler4x_Tpdf(uint32_t *state)
{
  const int32_t first =
      (int32_t)(SPDIF_IirUpsampler4x_Random(state) >> 17);
  const int32_t second =
      (int32_t)(SPDIF_IirUpsampler4x_Random(state) >> 17);
  return (float)(first - second) * (1.0f / 32768.0f);
}

static int16_t SPDIF_IirUpsampler4x_Quantize(float value,
                                              uint32_t *dither_state)
{
  int32_t rounded;
  value += SPDIF_IirUpsampler4x_Tpdf(dither_state);
  if (value >= 32767.0f) return 32767;
  if (value <= -32768.0f) return -32768;
  rounded = (value >= 0.0f) ? (int32_t)(value + 0.5f) :
                              (int32_t)(value - 0.5f);
  return (int16_t)rounded;
}

void SPDIF_IirUpsampler4x_Reset(SPDIF_IirUpsampler4x *state)
{
  if (state == NULL) return;
  memset(state->left_state, 0, sizeof(state->left_state));
  memset(state->right_state, 0, sizeof(state->right_state));
  state->dither_left = SPDIF_IIR_DITHER_LEFT_SEED;
  state->dither_right = SPDIF_IIR_DITHER_RIGHT_SEED;
}

void SPDIF_IirUpsampler4x_Init(SPDIF_IirUpsampler4x *state,
                               uint32_t source_rate)
{
  if (state == NULL) return;
  state->coefficients =
      (source_rate == 44100U) ? spdif_iir_coefficients_176k4 :
                                spdif_iir_coefficients_192k;
  SPDIF_IirUpsampler4x_Reset(state);
}

static void SPDIF_IirUpsampler4x_ProcessFloat(
    SPDIF_IirUpsampler4x *state, int16_t left, int16_t right,
    float output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    float left_value = (phase == 0U) ?
        (float)left * SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB : 0.0f;
    float right_value = (phase == 0U) ?
        (float)right * SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB : 0.0f;

    for (uint32_t stage = 0U;
         stage < SPDIF_IIR_UPSAMPLER_STAGES; ++stage)
    {
      const float *coefficients = state->coefficients[stage];
      const float left_output =
          coefficients[0] * left_value + state->left_state[stage][0];
      const float right_output =
          coefficients[0] * right_value + state->right_state[stage][0];

      state->left_state[stage][0] =
          coefficients[1] * left_value +
          coefficients[3] * left_output + state->left_state[stage][1];
      state->right_state[stage][0] =
          coefficients[1] * right_value +
          coefficients[3] * right_output + state->right_state[stage][1];
      state->left_state[stage][1] =
          coefficients[2] * left_value + coefficients[4] * left_output;
      state->right_state[stage][1] =
          coefficients[2] * right_value + coefficients[4] * right_output;

      left_value = left_output;
      right_value = right_output;
    }

    output[phase * 2U] = left_value;
    output[phase * 2U + 1U] = right_value;
  }
}

void SPDIF_IirUpsampler4x_Process(
    SPDIF_IirUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  float filtered[SPDIF_UPSAMPLER_FACTOR * 2U];
  SPDIF_IirUpsampler4x_ProcessFloat(state, left, right, filtered);
  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    output[phase * 2U] = SPDIF_IirUpsampler4x_Quantize(
        filtered[phase * 2U], &state->dither_left);
    output[phase * 2U + 1U] = SPDIF_IirUpsampler4x_Quantize(
        filtered[phase * 2U + 1U], &state->dither_right);
  }
}

void SPDIF_HybridUpsampler4x_Reset(SPDIF_HybridUpsampler4x *state)
{
  if (state == NULL) return;
  SPDIF_IirUpsampler4x_Reset(&state->iir);
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  state->write_index = 0U;
}

void SPDIF_HybridUpsampler4x_Init(SPDIF_HybridUpsampler4x *state,
                                  uint32_t source_rate)
{
  if (state == NULL) return;
  SPDIF_IirUpsampler4x_Init(&state->iir, source_rate);
  state->phase_coefficients =
      (source_rate == 44100U) ? spdif_phase_coefficients_176k4 :
                                spdif_phase_coefficients_192k;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  state->write_index = 0U;
}

void SPDIF_HybridUpsampler4x_Process(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  float iir_output[SPDIF_UPSAMPLER_FACTOR * 2U];
  SPDIF_IirUpsampler4x_ProcessFloat(&state->iir, left, right, iir_output);

  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    const uint32_t write = state->write_index;
    const float left_input = iir_output[phase * 2U];
    const float right_input = iir_output[phase * 2U + 1U];
    const float *left_current;
    const float *right_current;
    float left_accumulator = 0.0f;
    float right_accumulator = 0.0f;

    state->left_history[write] = left_input;
    state->left_history[write + SPDIF_HYBRID_PHASE_TAPS] = left_input;
    state->right_history[write] = right_input;
    state->right_history[write + SPDIF_HYBRID_PHASE_TAPS] = right_input;
    left_current = &state->left_history[write + SPDIF_HYBRID_PHASE_TAPS];
    right_current = &state->right_history[write + SPDIF_HYBRID_PHASE_TAPS];

    const float *coefficients = state->phase_coefficients;
    for (uint32_t tap = 0U; tap < SPDIF_HYBRID_PHASE_TAPS; tap += 4U)
    {
      left_accumulator += coefficients[0] * left_current[0];
      right_accumulator += coefficients[0] * right_current[0];
      left_accumulator += coefficients[1] * left_current[-1];
      right_accumulator += coefficients[1] * right_current[-1];
      left_accumulator += coefficients[2] * left_current[-2];
      right_accumulator += coefficients[2] * right_current[-2];
      left_accumulator += coefficients[3] * left_current[-3];
      right_accumulator += coefficients[3] * right_current[-3];
      coefficients += 4;
      left_current -= 4;
      right_current -= 4;
    }

    output[phase * 2U] = SPDIF_IirUpsampler4x_Quantize(
        left_accumulator, &state->iir.dither_left);
    output[phase * 2U + 1U] = SPDIF_IirUpsampler4x_Quantize(
        right_accumulator, &state->iir.dither_right);
    state->write_index = (uint8_t)((write + 1U) &
                                   (SPDIF_HYBRID_PHASE_TAPS - 1U));
  }
}
