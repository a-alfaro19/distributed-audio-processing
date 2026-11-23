#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stddef.h>
#include <stdint.h>
#include "audio_config.h"

/**
 * @brief Summary output produced after analyzing a PCM audio chunk.
 */
typedef struct
{
    int led_heights[AUDIO_NUM_BANDS]; /**< Normalized LED heights [0..5] across the 5 frequency bands. */
    double band_rms[AUDIO_NUM_BANDS]; /**< Raw RMS amplitude per band for classification/heuristics. */
    float bpm;                        /**< Estimated tempo in beats per minute (BPM). */
    double overall_rms;               /**< Global RMS energy across the analyzed audio. */
    double spectral_centroid_hz;      /**< Weighted average frequency (Hz) indicating brightness. */
    double low_to_high_ratio;         /**< Ratio of low-band energy to high-band energy. */
    size_t frames_processed;          /**< Total sliding windows analyzed in this chunk. */
} AudioFrameResult;

/**
 * @brief Orchestrates windowing, FFTW3 spectral analysis, 5-band RMS LED mapping,
 *        and tempo (BPM) estimation using parameters from audio.config.
 *
 * @param pcm_buffer Pointer to decrypted PCM audio samples.
 * @param num_bytes Size of pcm_buffer in bytes.
 * @param config Loaded and validated audio configuration.
 * @param out_results Optional pointer to receive the analysis summary (may be NULL).
 * @return int 0 on success, or -1 on error.
 */
int process_audio_chunk(const uint8_t *pcm_buffer, size_t num_bytes,
                        const AudioFormatConfig *config,
                        AudioFrameResult *out_results);

#endif /* AUDIO_PROCESSOR_H */
