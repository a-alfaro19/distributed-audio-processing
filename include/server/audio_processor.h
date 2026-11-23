#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_FFT_WINDOW_SIZE 2048
#define AUDIO_NUM_BANDS 5

typedef struct
{
    int led_heights[AUDIO_NUM_BANDS]; // Normalized heights [0..5] for Sub-bass, Mid-bass, Mid, Upper-mid, High
    float bpm;                        // Estimated tempo in BPM
    size_t frames_processed;          // Total sliding windows analyzed in this chunk
} AudioAnalysisSummary;

/**
 * @brief Orchestrates windowing, FFTW3 spectral analysis, 5-band RMS LED mapping,
 *        and tempo (BPM) estimation over a decrypted PCM audio chunk.
 *
 * @param pcm_buffer Pointer to decrypted 16-bit signed PCM audio samples.
 * @param num_bytes Size of pcm_buffer in bytes.
 * @param sample_rate Sampling rate in Hz (e.g., 44100.0).
 * @param out_summary Optional pointer to receive the final analysis summary (may be NULL).
 * @return int 0 on success, or -1 on error.
 */
int process_audio_chunk(const uint8_t *pcm_buffer, size_t num_bytes,
                        double sample_rate, AudioAnalysisSummary *out_summary);

#endif /* AUDIO_PROCESSOR_H */
