#include "spdif_iir_upsampler.h"

#include <string.h>

#define SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB 3.56500363f
#define SPDIF_BESSEL_OPEN_GAIN_4X_MINUS_2P25DB 3.08716606f
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

/* 14th-order maximally-flat Butterworth alternatives. The 44.1 kHz family
 * uses a 21 kHz corner to keep its narrow image transition bounded; the
 * 48 kHz family places the -3 dB corner at the 24 kHz source Nyquist. This
 * deliberately trades some ultrasonic image rejection for a gentler,
 * ripple-free transition. Sections run from low Q to high Q so intermediate
 * DF2T levels remain well behaved. Each section has unity DC gain. */
static const float spdif_butterworth_coefficients_176k4
    [SPDIF_IIR_UPSAMPLER_STAGES][5] =
{
  { 0.079643407300f, 0.15928681460f, 0.079643407300f,
    0.87481787867f, -0.19339150787f },
  { 0.081287309743f, 0.16257461949f, 0.081287309743f,
    0.89287480637f, -0.21802404534f },
  { 0.084696033284f, 0.16939206657f, 0.084696033284f,
    0.93031685461f, -0.26910098774f },
  { 0.090127037042f, 0.18025407408f, 0.090127037042f,
    0.98997200182f, -0.35048014999f },
  { 0.098007665987f, 0.19601533197f, 0.098007665987f,
    1.0765342840f, -0.46856494794f },
  { 0.10898984310f, 0.21797968619f, 0.10898984310f,
    1.1971645434f, -0.63312391578f },
  { 0.12402864014f, 0.24805728029f, 0.12402864014f,
    1.3623534646f, -0.85846802513f }
};

static const float spdif_butterworth_coefficients_192k
    [SPDIF_IIR_UPSAMPLER_STAGES][5] =
{
  { 0.086010450809f, 0.17202090162f, 0.086010450809f,
    0.83059038739f, -0.17463219063f },
  { 0.087827935243f, 0.17565587049f, 0.087827935243f,
    0.84814156967f, -0.19945331064f },
  { 0.091602159380f, 0.18320431876f, 0.091602159380f,
    0.88458870207f, -0.25099733959f },
  { 0.097631072938f, 0.19526214588f, 0.097631072938f,
    0.94280904158f, -0.33333333333f },
  { 0.10641348504f, 0.21282697009f, 0.10641348504f,
    1.0276195153f, -0.45327345543f },
  { 0.11872035358f, 0.23744070717f, 0.11872035358f,
    1.1464651510f, -0.62134656536f },
  { 0.13570289702f, 0.27140579404f, 0.13570289702f,
    1.3104630977f, -0.85327468581f }
};

/* 20th-order Bessel-Thomson, ten unity-DC-gain DF2T sections ordered from
 * the smallest to largest pole radius. The analog prototype is contracted
 * to 78 percent of the source-Nyquist-normalized corner, placing -3 dB at
 * 17.55/19.10 kHz for the 44.1/48 kHz families. This keeps all four
 * polyphase peaks below full scale with the common -1 dB input headroom.
 * The mode deliberately prioritizes a causal transient and flat group delay
 * over high-treble flatness and the very narrow 44.1 kHz image transition. */
static const float spdif_bessel_coefficients
    [SPDIF_BESSEL_UPSAMPLER_STAGES][5] =
{
  { 0.211001400575f, 0.422002801150f, 0.211001400575f,
    0.163700286161f, -0.00770588846051f },
  { 0.213877535395f, 0.427755070790f, 0.213877535395f,
    0.160036113555f, -0.0155462551350f },
  { 0.219821655365f, 0.439643310730f, 0.219821655365f,
    0.152368059768f, -0.0316546812286f },
  { 0.229221233307f, 0.458442466614f, 0.229221233307f,
    0.140097272018f, -0.0569822052463f },
  { 0.242773973465f, 0.485547946930f, 0.242773973465f,
    0.122030234793f, -0.0931261286520f },
  { 0.261590897064f, 0.523181794129f, 0.261590897064f,
    0.0963151189865f, -0.142678707244f },
  { 0.287517080571f, 0.575034161141f, 0.287517080571f,
    0.0597820810118f, -0.209850403295f },
  { 0.323760764047f, 0.647521528093f, 0.323760764047f,
    0.00683284945350f, -0.301875905640f },
  { 0.376619877826f, 0.753239755652f, 0.376619877826f,
    -0.0737443166404f, -0.432735194663f },
  { 0.462103908184f, 0.924207816368f, 0.462103908184f,
    -0.210985475568f, -0.637430157169f }
};

/* More open version of the same 20th-order Bessel-Thomson prototype. Its
 * analog corner is 90 percent of the source-Nyquist-normalized design,
 * placing -3 dB at 20.04/21.81 kHz for the 44.1/48 kHz families. The wider
 * transition deliberately permits more ultrasonic imaging. A separate
 * -2.25 dB input gain keeps the resulting four-phase peak below full scale. */
static const float spdif_bessel_open_coefficients
    [SPDIF_BESSEL_UPSAMPLER_STAGES][5] =
{
  { 0.245077015713f, 0.490154031425f, 0.245077015713f,
    0.0208201363834f, -0.00112819923343f },
  { 0.248341815843f, 0.496683631686f, 0.248341815843f,
    0.0159557145024f, -0.00932297787417f },
  { 0.255081251204f, 0.510162502409f, 0.255081251204f,
    0.00582870258999f, -0.0261537074078f },
  { 0.265718398945f, 0.531436797890f, 0.265718398945f,
    -0.0102853385113f, -0.0525882572680f },
  { 0.281010507674f, 0.562021015348f, 0.281010507674f,
    -0.0337864824342f, -0.0902555482619f },
  { 0.302154435720f, 0.604308871441f, 0.302154435720f,
    -0.0668444998934f, -0.141773242988f },
  { 0.331117277310f, 0.662234554620f, 0.331117277310f,
    -0.113110575670f, -0.211358533570f },
  { 0.371273517173f, 0.742547034346f, 0.371273517173f,
    -0.178926308906f, -0.306167759787f },
  { 0.429136331630f, 0.858272663260f, 0.429136331630f,
    -0.276728396233f, -0.439816930288f },
  { 0.520931611076f, 1.041863222150f, 0.520931611076f,
    -0.437956038862f, -0.645770405442f }
};

/* Fifth-order minimum-integrated-noise NTFs. The unit-circle zero angles are
 * the five-point Gauss-Legendre nodes scaled across 0..20 kHz. Coefficient
 * zero is implicit (one); these are the five previous-error feedback taps.
 * Separate clock-family sets keep the acoustic optimization band fixed. */
static const float spdif_noise_shaper5_coefficients_176k4
    [SPDIF_OPT_NOISE_SHAPER_ORDER] =
{
  -4.45219631032f, 8.41508536432f, -8.41508536432f,
  4.45219631032f, -1.0f
};

static const float spdif_noise_shaper5_coefficients_192k
    [SPDIF_OPT_NOISE_SHAPER_ORDER] =
{
  -4.53550663875f, 8.64850715592f, -8.64850715592f,
  4.53550663875f, -1.0f
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

/* SOFT PHASE equalizers for the Butterworth alternatives. Starting from a
 * 25-output-sample pure delay, weighted least squares applies 65 percent of
 * the correction toward a 36-sample linear-phase target. Fit weight is one
 * through 12 kHz, tapers to 0.7 at 16 kHz and 0.35 at 18 kHz, with 1e-6
 * Tikhonov regularization. Both sets are unity at DC and avoid ultrasonic
 * FIR gain while retaining less aggressive Butterworth phase behaviour. */
static const float spdif_butterworth_phase_coefficients_176k4
    [SPDIF_HYBRID_PHASE_TAPS] =
{
  -0.034405022499f, 0.075659266018f, 0.0048470643064f, -0.061590663124f,
  -0.049757283921f, 0.014433173290f, 0.065551110276f, 0.059788772665f,
  0.0032850648520f, -0.059550089419f, -0.081371944958f, -0.044756205579f,
  0.027429303267f, 0.088061351385f, 0.096178117257f, 0.042499649305f,
  -0.044819895326f, -0.11660286830f, -0.13067996948f, -0.074889845815f,
  0.027836039879f, 0.13414930226f, 0.20490843009f, 0.22452370507f,
  0.20381421893f, 0.16621938667f, 0.12849317748f, 0.091085993828f,
  0.046621169259f, 0.0000096299071549f, -0.020595345081f, 0.013625207497f
};

static const float spdif_butterworth_phase_coefficients_192k
    [SPDIF_HYBRID_PHASE_TAPS] =
{
  -0.014984849789f, 0.030633088842f, 0.0062772518006f, -0.023230702048f,
  -0.025227240912f, -0.0019911386526f, 0.024252551848f, 0.031906242164f,
  0.014663992186f, -0.015873976406f, -0.038791447076f, -0.037394080822f,
  -0.0097280865645f, 0.029587975420f, 0.057034910777f, 0.053235209830f,
  0.014511550683f, -0.043187806049f, -0.090419418414f, -0.097888604525f,
  -0.051208350434f, 0.040513399062f, 0.14693346944f, 0.22921818726f,
  0.25770277772f, 0.22599685259f, 0.15407995270f, 0.077782996146f,
  0.028879444548f, 0.015807839869f, 0.016871374693f, -0.0059633658854f
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

static int16_t SPDIF_HybridUpsampler4x_QuantizeNoiseShaped2(
    float value, uint32_t *dither_state, float error[2])
{
  /* With error defined as y[n]-v[n], subtracting 2e[n-1]-e[n-2]
   * produces y=x+(1-z^-1)^2e. Include the non-subtractive TPDF in this
   * error so its energy is shaped together with the rounding residue. */
  const float shaped = value - 2.0f * error[0] + error[1];
  const float quantizer_input =
      shaped + SPDIF_IirUpsampler4x_Tpdf(dither_state);
  int32_t rounded;

  /* HYBRID already has -1 dB headroom. Any rail hit is therefore treated as
   * overload: saturate and discard the error history to prevent wind-up and
   * the multi-sample burst that would otherwise follow a clipped transient. */
  if (quantizer_input >= 32767.0f)
  {
    error[0] = 0.0f;
    error[1] = 0.0f;
    return 32767;
  }
  if (quantizer_input <= -32768.0f)
  {
    error[0] = 0.0f;
    error[1] = 0.0f;
    return -32768;
  }

  rounded = (quantizer_input >= 0.0f) ?
      (int32_t)(quantizer_input + 0.5f) :
      (int32_t)(quantizer_input - 0.5f);
  error[1] = error[0];
  error[0] = (float)rounded - shaped;
  return (int16_t)rounded;
}

static int16_t SPDIF_HybridUpsampler4x_QuantizeNoiseShaped5(
    float value, uint32_t *dither_state,
    float error[SPDIF_OPT_NOISE_SHAPER_ORDER],
    const float coefficients[SPDIF_OPT_NOISE_SHAPER_ORDER])
{
  float shaped = value;
  int32_t rounded;

  /* y=x+N(z)e with N[0]=1. Feeding the remaining NTF coefficients back
   * around the quantizer shapes both TPDF and rounding residue. This is an
   * FIR error path, so bounded errors cannot create recursive instability. */
  for (uint32_t tap = 0U; tap < SPDIF_OPT_NOISE_SHAPER_ORDER; ++tap)
  {
    shaped += coefficients[tap] * error[tap];
  }
  const float quantizer_input =
      shaped + SPDIF_IirUpsampler4x_Tpdf(dither_state);

  if (quantizer_input >= 32767.0f)
  {
    memset(error, 0, sizeof(float) * SPDIF_OPT_NOISE_SHAPER_ORDER);
    return 32767;
  }
  if (quantizer_input <= -32768.0f)
  {
    memset(error, 0, sizeof(float) * SPDIF_OPT_NOISE_SHAPER_ORDER);
    return -32768;
  }

  rounded = (quantizer_input >= 0.0f) ?
      (int32_t)(quantizer_input + 0.5f) :
      (int32_t)(quantizer_input - 0.5f);
  for (uint32_t tap = SPDIF_OPT_NOISE_SHAPER_ORDER - 1U; tap > 0U; --tap)
  {
    error[tap] = error[tap - 1U];
  }
  error[0] = (float)rounded - shaped;
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
  state->input_gain = SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB;
  state->stage_count = SPDIF_IIR_UPSAMPLER_STAGES;
  SPDIF_IirUpsampler4x_Reset(state);
}

static void SPDIF_IirUpsampler4x_InitButterworth(
    SPDIF_IirUpsampler4x *state, uint32_t source_rate)
{
  if (state == NULL) return;
  state->coefficients =
      (source_rate == 44100U) ? spdif_butterworth_coefficients_176k4 :
                                spdif_butterworth_coefficients_192k;
  state->input_gain = SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB;
  state->stage_count = SPDIF_IIR_UPSAMPLER_STAGES;
  SPDIF_IirUpsampler4x_Reset(state);
}

static void SPDIF_IirUpsampler4x_InitBessel(
    SPDIF_IirUpsampler4x *state)
{
  if (state == NULL) return;
  state->coefficients = spdif_bessel_coefficients;
  state->input_gain = SPDIF_IIR_INPUT_GAIN_4X_MINUS_1DB;
  state->stage_count = SPDIF_BESSEL_UPSAMPLER_STAGES;
  SPDIF_IirUpsampler4x_Reset(state);
}

static void SPDIF_IirUpsampler4x_InitBesselOpen(
    SPDIF_IirUpsampler4x *state)
{
  if (state == NULL) return;
  state->coefficients = spdif_bessel_open_coefficients;
  state->input_gain = SPDIF_BESSEL_OPEN_GAIN_4X_MINUS_2P25DB;
  state->stage_count = SPDIF_BESSEL_UPSAMPLER_STAGES;
  SPDIF_IirUpsampler4x_Reset(state);
}

static void SPDIF_IirUpsampler4x_ProcessFloat(
    SPDIF_IirUpsampler4x *state, int16_t left, int16_t right,
    float output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    float left_value = (phase == 0U) ?
        (float)left * state->input_gain : 0.0f;
    float right_value = (phase == 0U) ?
        (float)right * state->input_gain : 0.0f;

    for (uint32_t stage = 0U; stage < state->stage_count; ++stage)
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
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
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
  state->noise_shaping_coefficients = NULL;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->write_index = 0U;
}

void SPDIF_HybridUpsampler4x_InitButterworth(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate)
{
  if (state == NULL) return;
  SPDIF_IirUpsampler4x_InitButterworth(&state->iir, source_rate);
  state->phase_coefficients =
      (source_rate == 44100U) ?
          spdif_butterworth_phase_coefficients_176k4 :
          spdif_butterworth_phase_coefficients_192k;
  state->noise_shaping_coefficients = NULL;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->write_index = 0U;
}

void SPDIF_MinimumPhaseUpsampler4x_InitBessel(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate)
{
  (void)source_rate;
  if (state == NULL) return;
  SPDIF_IirUpsampler4x_InitBessel(&state->iir);
  state->phase_coefficients = NULL;
  state->noise_shaping_coefficients = NULL;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->write_index = 0U;
}

void SPDIF_MinimumPhaseUpsampler4x_InitBesselOpen(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate)
{
  (void)source_rate;
  if (state == NULL) return;
  SPDIF_IirUpsampler4x_InitBesselOpen(&state->iir);
  state->phase_coefficients = NULL;
  state->noise_shaping_coefficients = NULL;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->write_index = 0U;
}

void SPDIF_MinimumPhaseUpsampler4x_InitBesselNoiseShaped5(
    SPDIF_HybridUpsampler4x *state, uint32_t source_rate)
{
  SPDIF_MinimumPhaseUpsampler4x_InitBessel(state, source_rate);
  if (state == NULL) return;
  state->noise_shaping_coefficients =
      (source_rate == 44100U) ?
          spdif_noise_shaper5_coefficients_176k4 :
          spdif_noise_shaper5_coefficients_192k;
}

static void SPDIF_HybridUpsampler4x_ProcessInternal(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U], uint8_t noise_shaping)
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

    if (noise_shaping != 0U)
    {
      output[phase * 2U] =
          SPDIF_HybridUpsampler4x_QuantizeNoiseShaped2(
              left_accumulator, &state->iir.dither_left,
              state->noise_error_left);
      output[phase * 2U + 1U] =
          SPDIF_HybridUpsampler4x_QuantizeNoiseShaped2(
              right_accumulator, &state->iir.dither_right,
              state->noise_error_right);
    }
    else
    {
      output[phase * 2U] = SPDIF_IirUpsampler4x_Quantize(
          left_accumulator, &state->iir.dither_left);
      output[phase * 2U + 1U] = SPDIF_IirUpsampler4x_Quantize(
          right_accumulator, &state->iir.dither_right);
    }
    state->write_index = (uint8_t)((write + 1U) &
                                   (SPDIF_HYBRID_PHASE_TAPS - 1U));
  }
}

void SPDIF_HybridUpsampler4x_Process(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  SPDIF_HybridUpsampler4x_ProcessInternal(state, left, right, output, 0U);
}

void SPDIF_HybridUpsampler4x_ProcessNoiseShaped2(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  SPDIF_HybridUpsampler4x_ProcessInternal(state, left, right, output, 1U);
}

void SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped2(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  float filtered[SPDIF_UPSAMPLER_FACTOR * 2U];
  SPDIF_IirUpsampler4x_ProcessFloat(&state->iir, left, right, filtered);
  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    output[phase * 2U] =
        SPDIF_HybridUpsampler4x_QuantizeNoiseShaped2(
            filtered[phase * 2U], &state->iir.dither_left,
            state->noise_error_left);
    output[phase * 2U + 1U] =
        SPDIF_HybridUpsampler4x_QuantizeNoiseShaped2(
            filtered[phase * 2U + 1U], &state->iir.dither_right,
            state->noise_error_right);
  }
}

void SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped5(
    SPDIF_HybridUpsampler4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_UPSAMPLER_FACTOR * 2U])
{
  float filtered[SPDIF_UPSAMPLER_FACTOR * 2U];
  SPDIF_IirUpsampler4x_ProcessFloat(&state->iir, left, right, filtered);
  for (uint32_t phase = 0U; phase < SPDIF_UPSAMPLER_FACTOR; ++phase)
  {
    output[phase * 2U] =
        SPDIF_HybridUpsampler4x_QuantizeNoiseShaped5(
            filtered[phase * 2U], &state->iir.dither_left,
            state->noise_error_left, state->noise_shaping_coefficients);
    output[phase * 2U + 1U] =
        SPDIF_HybridUpsampler4x_QuantizeNoiseShaped5(
            filtered[phase * 2U + 1U], &state->iir.dither_right,
            state->noise_error_right, state->noise_shaping_coefficients);
  }
}
