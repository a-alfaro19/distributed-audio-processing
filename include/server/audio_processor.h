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
    int led_heights[AUDIO_NUM_BANDS]; // Normalized heights [0..5] for Sub-bass, Mid-bass, Mid, Upper-mid, High
    float bpm;                        // Estimated tempo in BPM
    size_t frames_processed;          // Total sliding windows analyzed in this chunk
} AudioAnalysisSummary;

/**
 * @brief Orchestrates windowing, FFTW3 spectral analysis, 5-band RMS LED mapping,
 *        and tempo (BPM) estimation using parameters from audio.config.
 *
 * @param pcm_buffer Pointer to decrypted PCM audio samples.
 * @param num_bytes Size of pcm_buffer in bytes.
 * @param config Loaded and validated audio configuration.
 * @param out_summary Optional pointer to receive the analysis summary (may be NULL).
 * @return int 0 on success, or -1 on error.
 */
int process_audio_chunk(const uint8_t *pcm_buffer, size_t num_bytes,
                        const AudioFormatConfig *config,
                        AudioAnalysisSummary *out_summary);

#endif /* AUDIO_PROCESSOR_H */
