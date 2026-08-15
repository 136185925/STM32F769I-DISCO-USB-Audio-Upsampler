#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern volatile uint16_t audio_codec_id;
extern volatile uint8_t audio_codec_ready;
extern volatile uint32_t audio_codec_i2c_errors;

void AudioCodec_BootInit(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_CODEC_H */
