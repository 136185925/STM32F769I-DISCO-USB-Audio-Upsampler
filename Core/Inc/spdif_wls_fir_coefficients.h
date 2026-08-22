#ifndef SPDIF_WLS_FIR_COEFFICIENTS_H
#define SPDIF_WLS_FIR_COEFFICIENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Returns phase-major h[p + 4k] coefficients for the requested source
 * family and writes the number of taps in each of the four phases. */
const float *SPDIF_WlsFir4x_GetCoefficients(uint32_t source_rate,
                                            uint8_t *taps_per_phase);
const float *SPDIF_WlsLinearFir4x_GetCoefficients(uint32_t source_rate,
                                                  uint8_t *taps_per_phase);

#ifdef __cplusplus
}
#endif

#endif /* SPDIF_WLS_FIR_COEFFICIENTS_H */
