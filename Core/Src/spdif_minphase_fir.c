#include "spdif_minphase_fir.h"

#include "spdif_wls_fir_coefficients.h"

#include <string.h>

#define SPDIF_MINPHASE_FIR_KAISER_INPUT_GAIN 3.36558057f
#define SPDIF_MINPHASE_FIR_WLS_MIN_INPUT_GAIN 3.56500375f
#define SPDIF_MINPHASE_FIR_WLS_LINEAR_INPUT_GAIN 3.86420352f
#define SPDIF_MINPHASE_FIR_DITHER_LEFT_SEED  0x9E3779B9U
#define SPDIF_MINPHASE_FIR_DITHER_RIGHT_SEED 0x243F6A88U

/* Phase-major h[p + 4k], generated from a 256-tap Kaiser beta=8.9
 * windowed-sinc prototype at 176.4 kHz. Passband is 0..20 kHz, stopband
 * starts at 24.1 kHz, and real-cepstrum spectral factorization preserves
 * the magnitude response while moving the FIR to minimum phase. */
static const float spdif_minphase_fir_176k4[4U * 64U] =
{
  2.4211430302e-05f, 6.9972207432e-03f, 8.2212409265e-02f, 1.8762437583e-01f,
  7.0068272248e-03f, -7.6273449097e-02f, 7.5952243993e-02f, -5.5346128822e-02f,
  3.3570579937e-02f, -1.6074507490e-02f, 3.4587149295e-03f, 5.0801254823e-03f,
  -1.0557564099e-02f, 1.3847143942e-02f, -1.5615389020e-02f, 1.6345174529e-02f,
  -1.6377678007e-02f, 1.5950995357e-02f, -1.5230312702e-02f, 1.4329950061e-02f,
  -1.3328956080e-02f, 1.2281954675e-02f, -1.1226629025e-02f, 1.0188871210e-02f,
  -9.1863157937e-03f, 8.2307726783e-03f, -7.3299019470e-03f, 6.4883723320e-03f,
  -5.7086652114e-03f, 4.9916431265e-03f, -4.3369508572e-03f, 3.7433074160e-03f,
  -3.2087199550e-03f, 2.7306428570e-03f, -2.3063624780e-03f, 1.9311400039e-03f,
  -1.6053483587e-03f, 1.3211960272e-03f, -1.0759848947e-03f, 8.6689092559e-04f,
  -6.9051465322e-04f, 5.4322679582e-04f, -4.2145217077e-04f, 3.2182363583e-04f,
  -2.4124739939e-04f, 1.7691583855e-04f, -1.2630827973e-04f, 8.7177094094e-05f,
  -5.7531024563e-05f, 3.5618768913e-05f, -1.9912723859e-05f, 9.0955030543e-06f,
  -2.0447191268e-06f, -2.1836188162e-06f, 4.3683296065e-06f, -5.1401532298e-06f,
  4.9959917023e-06f, -4.3168268299e-06f, 3.3796319773e-06f, -2.3718951290e-06f,
  1.3992754712e-06f, -5.0889385736e-07f, -2.7295480526e-07f, 2.2507226390e-07f,

  1.9009896315e-04f, 1.5519967640e-02f, 1.1703826143e-01f, 1.7397118713e-01f,
  -5.4764998162e-02f, -2.8278624726e-02f, 5.7609031539e-02f, -5.9451204527e-02f,
  5.0740698600e-02f, -3.9443493182e-02f, 2.8781404471e-02f, -1.9810652376e-02f,
  1.2681688531e-02f, -7.2081001761e-03f, 3.1101861046e-03f, -1.1200385895e-04f,
  -2.0254402035e-03f, 3.4975838620e-03f, -4.4598027125e-03f, 5.0339354334e-03f,
  -5.3149731547e-03f, 5.3768823116e-03f, -5.2773404878e-03f, 5.0614394004e-03f,
  -4.7645263865e-03f, 4.4143626759e-03f, -4.0327526554e-03f, 3.6367729853e-03f,
  -3.2397090194e-03f, 2.8517695482e-03f, -2.4806432013e-03f, 2.1319325011e-03f,
  -1.8095078256e-03f, 1.5157732842e-03f, -1.2523699592e-03f, 1.0175091305e-03f,
  -8.1491500623e-04f, 6.3978712796e-04f, -4.9059852072e-04f, 3.6599458202e-04f,
  -2.6406747770e-04f, 1.8246518335e-04f, -1.1862578313e-04f, 6.9971645393e-05f,
  -3.4031152832e-05f, 8.5171725612e-06f, 8.6305681443e-06f, -1.9227881952e-05f,
  2.4845388232e-05f, -2.6813728685e-05f, 2.6236765673e-05f, -2.4007146204e-05f,
  2.0828595680e-05f, -1.7237955188e-05f, 1.3624615619e-05f, -1.0255469258e-05f,
  7.2945195337e-06f, -4.8243002382e-06f, 2.8616839462e-06f, -1.3765151553e-06f,
  3.1105835571e-07f, 3.8802273294e-07f, -5.4907250457e-07f, -1.8544214368e-07f,

  8.3543241522e-04f, 3.0190879767e-02f, 1.5153997968e-01f, 1.3501561540e-01f,
  -9.4292463814e-02f, 2.4849602415e-02f, 1.8042256372e-02f, -3.7708121580e-02f,
  4.3657878550e-02f, -4.2641492947e-02f, 3.8540958149e-02f, -3.3399823638e-02f,
  2.8224074110e-02f, -2.3465897899e-02f, 1.9292826875e-02f, -1.5732103029e-02f,
  1.2746388982e-02f, -1.0272608883e-02f, 8.2412451449e-03f, -6.5854032721e-03f,
  5.2446108412e-03f, -4.1659739268e-03f, 3.3040717990e-03f, -2.6203158187e-03f,
  2.0821243584e-03f, -1.6620969824e-03f, 1.3372586557e-03f, -1.0884019580e-03f,
  8.9952318895e-04f, -7.5735343012e-04f, 6.5096066142e-04f, -5.7141703426e-04f,
  5.1150977008e-04f, -4.6555985693e-04f, 4.2841855855e-04f, -3.9932071841e-04f,
  3.7123496064e-04f, -3.4520895760e-04f, 3.2053680470e-04f, -2.9616562853e-04f,
  2.7142050412e-04f, -2.4610398175e-04f, 2.2038239991e-04f, -1.9464229004e-04f,
  1.6937117764e-04f, -1.4506373218e-04f, 1.2216613206e-04f, -1.0104286751e-04f,
  8.1958946979e-05f, -6.5076940276e-05f, 5.0459478257e-05f, -3.8081883957e-05f,
  2.7846651700e-05f, -1.9597387993e-05f, 1.3136587372e-05f, -8.2398424888e-06f,
  4.6716567714e-06f, -2.1953676014e-06f, 5.8545873209e-07f, 3.5910548635e-07f,
  -8.0032900573e-07f, 7.8353423566e-07f, -2.2481767325e-07f, -5.1819635430e-08f,

  2.6871103681e-03f, 5.2437858538e-02f, 1.7792049902e-01f, 7.5525901160e-02f,
  -1.0161081617e-01f, 6.4095039942e-02f, -2.5652448748e-02f, -1.0313716161e-03f,
  1.6891922937e-02f, -2.5180830405e-02f, 2.8688555938e-02f, -2.9350985672e-02f,
  2.8408279841e-02f, -2.6623891997e-02f, 2.4455556832e-02f, -2.2170939621e-02f,
  1.9921544963e-02f, -1.7788743208e-02f, 1.5812069070e-02f, -1.4006499096e-02f,
  1.2372951457e-02f, -1.0904618000e-02f, 9.5907611729e-03f, -8.4189553708e-03f,
  7.3763831148e-03f, -6.4505601765e-03f, 5.6297166753e-03f, -4.9029697569e-03f,
  4.2603819146e-03f, -3.6929491385e-03f, 3.1925588614e-03f, -2.7519238476e-03f,
  2.3645100183e-03f, -2.0246000448e-03f, 1.7259476153e-03f, -1.4671616253e-03f,
  1.2395809057e-03f, -1.0413827812e-03f, 8.7002995140e-04f, -7.2249968183e-04f,
  5.9585305605e-04f, -4.8747358891e-04f, 3.9510261907e-04f, -3.1680116264e-04f,
  2.5088413200e-04f, -1.9586327075e-04f, 1.5040447284e-04f, -1.1329659739e-04f,
  8.3429428223e-05f, -5.9780848285e-05f, 4.1410889419e-05f, -2.7461199871e-05f,
  1.7154002408e-05f, -9.7906076675e-06f, 4.7550543078e-06f, -1.5113409899e-06f,
  -3.9637080215e-07f, 1.3481964507e-06f, -1.6465911026e-06f, 1.5202065655e-06f,
  -1.1012513782e-06f, 4.4407482288e-07f, 3.8172861347e-07f, 3.7998195222e-08f
};

/* 160-tap companion for 192 kHz: 0..20 kHz passband and stopband beginning
 * at 28 kHz, stored in the same four phase-major groups of 40 taps. */
static const float spdif_minphase_fir_192k[4U * 40U] =
{
  4.0299599417e-05f, 1.0549188169e-02f, 1.0758811728e-01f, 1.8563661311e-01f,
  -5.1515962711e-02f, -2.5941290168e-02f, 5.1929692761e-02f, -5.4753954750e-02f,
  4.9144288303e-02f, -4.1343107000e-02f, 3.3740790180e-02f, -2.7120559548e-02f,
  2.1628918824e-02f, -1.7179954075e-02f, 1.3619328315e-02f, -1.0787786783e-02f,
  8.5432168501e-03f, -6.7661104463e-03f, 5.3588103021e-03f, -4.2427748270e-03f,
  3.3555187604e-03f, -2.6478106134e-03f, 2.0809132025e-03f, -1.6266756605e-03f,
  1.2581248447e-03f, -9.6306188242e-04f, 7.2640621632e-04f, -5.3678282530e-04f,
  3.8621176276e-04f, -2.6872610473e-04f, 1.7935640819e-04f, -1.1359170130e-04f,
  6.7172687604e-05f, -3.6081331335e-05f, 1.6628100387e-05f, -5.5583260513e-06f,
  1.3740950965e-07f, 1.7784725175e-06f, -1.6781607888e-06f, -3.9373444524e-09f,

  3.0849145002e-04f, 2.2725734885e-02f, 1.4574966598e-01f, 1.4909849643e-01f,
  -9.5236074964e-02f, 2.9255088511e-02f, 8.7251123634e-03f, -2.6332690543e-02f,
  3.2657459609e-02f, -3.3285837462e-02f, 3.1231274719e-02f, -2.8049844245e-02f,
  2.4527646030e-02f, -2.1052787288e-02f, 1.7808972394e-02f, -1.4875106378e-02f,
  1.2276425920e-02f, -1.0010753869e-02f, 8.0619883930e-03f, -6.4070614441e-03f,
  5.0195805547e-03f, -3.8718458460e-03f, 2.9354587639e-03f, -2.1852326537e-03f,
  1.5901991081e-03f, -1.1304136850e-03f, 7.8237266683e-04f, -5.2426931887e-04f,
  3.3740951434e-04f, -2.0609857505e-04f, 1.1724242982e-04f, -5.9988832506e-05f,
  2.5461114606e-05f, -6.5554179039e-06f, -2.2358019877e-06f, 5.0107981499e-06f,
  -4.6438535988e-06f, 2.9720480100e-06f, -9.1846143752e-07f, -4.0882198047e-07f,

  1.3246648322e-03f, 4.2784798992e-02f, 1.7785222968e-01f, 8.8384232524e-02f,
  -1.0391142817e-01f, 6.7676965839e-02f, -3.4361784565e-02f, 1.1899730628e-02f,
  1.6346935707e-03f, -9.1451071689e-03f, 1.2845235676e-02f, -1.4203832815e-02f,
  1.4155648398e-02f, -1.3290662439e-02f, 1.1983183630e-02f, -1.0472877917e-02f,
  8.9141339039e-03f, -7.4059368020e-03f, 6.0100841530e-03f, -4.7625539922e-03f,
  3.6808798604e-03f, -2.7692939565e-03f, 2.0215314648e-03f, -1.4281301263e-03f,
  9.6726790928e-04f, -6.2381464044e-04f, 3.7809756850e-04f, -2.0983228039e-04f,
  1.0050170646e-04f, -3.4257888548e-05f, -1.8854876844e-06f, 1.8171498390e-05f,
  -2.2357726740e-05f, 2.0034688663e-05f, -1.4983718741e-05f, 9.5389286150e-06f,
  -4.9277845642e-06f, 1.5803375073e-06f, 4.2132440023e-07f, -1.6348337606e-09f,

  4.1590523925e-03f, 7.1606000285e-02f, 1.9400138702e-01f, 1.5629381992e-02f,
  -7.6971219249e-02f, 7.5575782098e-02f, -5.8668683714e-02f, 4.1510163368e-02f,
  -2.7826928566e-02f, 1.7823033668e-02f, -1.0825241808e-02f, 6.0831391704e-03f,
  -2.9727371700e-03f, 1.0189679527e-03f, 1.2865661158e-04f, -7.2608146874e-04f,
  9.5964911754e-04f, -9.6456651634e-04f, 8.3806284748e-04f, -6.4873385551e-04f,
  4.4330820327e-04f, -2.5190272325e-04f, 9.0573248269e-05f, 2.9536425840e-05f,
  -1.1292149263e-04f, 1.6034028944e-04f, -1.7729005720e-04f, 1.7175645524e-04f,
  -1.5190116758e-04f, 1.2478262124e-04f, -9.5835546235e-05f, 6.8793036669e-05f,
  -4.5846734975e-05f, 2.7931400871e-05f, -1.5057104093e-05f, 6.6263062570e-06f,
  -1.7198075039e-06f, -6.1929136884e-07f, 1.0080328226e-06f, 5.8812640389e-08f
};

void SPDIF_MinimumPhaseFir4x_Reset(SPDIF_MinimumPhaseFir4x *state)
{
  if (state == NULL) return;
  memset(state->left_history, 0, sizeof(state->left_history));
  memset(state->right_history, 0, sizeof(state->right_history));
  memset(state->noise_error_left, 0, sizeof(state->noise_error_left));
  memset(state->noise_error_right, 0, sizeof(state->noise_error_right));
  state->dither_left = SPDIF_MINPHASE_FIR_DITHER_LEFT_SEED;
  state->dither_right = SPDIF_MINPHASE_FIR_DITHER_RIGHT_SEED;
  state->write_index = 0U;
}

void SPDIF_MinimumPhaseFir4x_Init(SPDIF_MinimumPhaseFir4x *state,
                                  uint32_t source_rate)
{
  if (state == NULL) return;
  if (source_rate == 44100U)
  {
    state->coefficients = spdif_minphase_fir_176k4;
    state->taps_per_phase = 64U;
  }
  else
  {
    state->coefficients = spdif_minphase_fir_192k;
    state->taps_per_phase = 40U;
  }
  state->noise_shaping_coefficients =
      SPDIF_NoiseShaper5_GetCoefficients(source_rate * 4U);
  state->input_gain = SPDIF_MINPHASE_FIR_KAISER_INPUT_GAIN;
  SPDIF_MinimumPhaseFir4x_Reset(state);
}

void SPDIF_WeightedMinimumPhaseFir4x_Init(SPDIF_MinimumPhaseFir4x *state,
                                          uint32_t source_rate)
{
  if (state == NULL) return;
  state->coefficients =
      SPDIF_WlsFir4x_GetCoefficients(source_rate, &state->taps_per_phase);
  state->noise_shaping_coefficients =
      SPDIF_NoiseShaper5_GetCoefficients(source_rate * 4U);
  state->input_gain = SPDIF_MINPHASE_FIR_WLS_MIN_INPUT_GAIN;
  SPDIF_MinimumPhaseFir4x_Reset(state);
}

void SPDIF_WeightedLinearPhaseFir4x_Init(SPDIF_MinimumPhaseFir4x *state,
                                         uint32_t source_rate)
{
  if (state == NULL) return;
  state->coefficients =
      SPDIF_WlsLinearFir4x_GetCoefficients(source_rate,
                                           &state->taps_per_phase);
  state->noise_shaping_coefficients =
      SPDIF_NoiseShaper5_GetCoefficients(source_rate * 4U);
  state->input_gain = SPDIF_MINPHASE_FIR_WLS_LINEAR_INPUT_GAIN;
  SPDIF_MinimumPhaseFir4x_Reset(state);
}

void SPDIF_MinimumPhaseFir4x_Process(
    SPDIF_MinimumPhaseFir4x *state, int16_t left, int16_t right,
    int16_t output[SPDIF_MINPHASE_FIR_FACTOR * 2U])
{
  const uint32_t taps = state->taps_per_phase;
  const uint32_t write = state->write_index;
  const float left_input = (float)left * state->input_gain;
  const float right_input = (float)right * state->input_gain;

  state->left_history[write] = left_input;
  state->left_history[write + taps] = left_input;
  state->right_history[write] = right_input;
  state->right_history[write + taps] = right_input;

  for (uint32_t phase = 0U; phase < SPDIF_MINPHASE_FIR_FACTOR; ++phase)
  {
    const float *coefficients = &state->coefficients[phase * taps];
    const float *left_current = &state->left_history[write + taps];
    const float *right_current = &state->right_history[write + taps];
    float left_accumulator = 0.0f;
    float right_accumulator = 0.0f;

    for (uint32_t tap = 0U; tap < taps; tap += 4U)
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

    output[phase * 2U] = SPDIF_NoiseShaper5_Quantize(
        left_accumulator, &state->dither_left, state->noise_error_left,
        state->noise_shaping_coefficients);
    output[phase * 2U + 1U] = SPDIF_NoiseShaper5_Quantize(
        right_accumulator, &state->dither_right, state->noise_error_right,
        state->noise_shaping_coefficients);
  }

  state->write_index = (uint8_t)((write + 1U == taps) ? 0U : write + 1U);
}
