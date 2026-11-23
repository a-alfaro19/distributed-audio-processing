// ==============================================
// Audio Configuration Loader & Validator Header
// ==============================================

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include <stddef.h>

#define AUDIO_FFT_WINDOW_SIZE 2048
#define AUDIO_NUM_BANDS 5
#define AUDIO_FORMAT_NAME_MAX 32

/**
 * @brief Supported audio format and DSP parameters loaded from audio.config.
 */
typedef struct
{
    char audio_format[AUDIO_FORMAT_NAME_MAX];  /**< Supported format identifier (e.g., "PCM"). */
    int bits_per_sample;                       /**< Sample bit depth (16 supported). */
    int channels;                              /**< Number of channels (1 = mono supported). */
    double sample_rate;                        /**< Audio sampling rate in Hz. */
    size_t fft_window_size;                    /**< FFT window length in samples. */
    double overlap_ratio;                      /**< Window overlap ratio in [0.0, 0.9). */
    double band_edges_hz[AUDIO_NUM_BANDS + 1]; /**< Frequency band edges in Hz (6 values for 5 bands). */
    double band_rms_ceiling;                   /**< RMS ceiling for [0..5] LED scaling. */
    double min_beat_interval_sec;              /**< Minimum interval (s) between beat onsets. */
    double onset_flux_threshold;               /**< Spectral flux threshold for onset detection. */
    size_t max_onset_history;                  /**< Maximum onset timestamps kept for BPM estimation. */
    float default_bpm;                         /**< Initial and fallback tempo estimate in BPM. */
} AudioFormatConfig;

/**
 * @brief Loads and validates supported audio parameters from a .config file.
 *
 * @param out_config Output configuration structure.
 * @return int 0 on success, or -1 on error.
 */
int load_audio_config(AudioFormatConfig *out_config);

#endif /* AUDIO_CONFIG_H */
