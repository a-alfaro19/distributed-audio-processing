// ==========================================
// Audio Classification & Global Stats Header
// ==========================================

#ifndef AUDIO_CLASSIFIER_H
#define AUDIO_CLASSIFIER_H

#include <stddef.h>
#include "audio_config.h"
#include "audio_processor.h"

typedef enum
{
    AUDIO_CLASS_SILENCE = 0,
    AUDIO_CLASS_VOICE,
    AUDIO_CLASS_MUSIC,
    AUDIO_CLASS_NOISE
} AudioContentClass;

typedef struct
{
    AudioContentClass content_class;           /**< Detected audio category (silence, voice, music, noise). */
    const char *label;                         /**< Human-readable label for logging/reporting. */
    double weighted_overall_rms;               /**< Frame-weighted global RMS across all ranks. */
    double weighted_spectral_centroid_hz;      /**< Frame-weighted spectral centroid in Hz. */
    double weighted_low_to_high_ratio;         /**< Frame-weighted low-to-high energy ratio. */
    double weighted_band_rms[AUDIO_NUM_BANDS]; /**< Frame-weighted RMS per frequency band. */
    float global_bpm;                          /**< Consolidated BPM across active/valid chunks. */
    size_t total_frames_processed;             /**< Sum of frames_processed across all ranks. */
    double refresh_interval_ms;                /**< Suggested display refresh interval in milliseconds. */
    double decay_per_step;                     /**< Suggested per-tick attenuation/decay factor [0.0..1.0]. */
} GlobalAudioStats;

/**
 * @brief Converts an AudioContentClass enum value into a human-readable string label.
 *
 * @param cls Audio classification category to convert.
 * @return const char* Constant string representation ("SILENCE", "VOICE", "MUSIC", "NOISE", or "UNKNOWN").
 */
const char *audio_class_to_string(AudioContentClass cls);

/**
 * @brief Consolidates per-rank AudioFrameResult entries into global weighted statistics,
 *        classifies the audio content, computes global BPM, and derives display refresh
 *        and attenuation (decay) parameters.
 *
 * @param results Array of AudioFrameResult structures gathered across the cluster in rank order.
 * @param cluster_size Total number of MPI ranks/nodes in the cluster (length of results).
 * @param config Validated audio configuration (may be NULL to fall back to default BPM).
 * @param out_stats Output pointer receiving the consolidated GlobalAudioStats.
 * @return int 0 on success, or -1 on invalid arguments.
 */
int consolidate_audio_stats(const AudioFrameResult *results,
                            int cluster_size,
                            const AudioFormatConfig *config,
                            GlobalAudioStats *out_stats);

#endif /* AUDIO_CLASSIFIER_H */
