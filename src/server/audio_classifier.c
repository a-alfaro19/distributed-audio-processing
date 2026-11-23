#include <math.h>
#include "audio_classifier.h"

const char *audio_class_to_string(AudioContentClass cls)
{
    switch (cls)
    {
    case AUDIO_CLASS_SILENCE:
        return "SILENCE";
    case AUDIO_CLASS_VOICE:
        return "VOICE";
    case AUDIO_CLASS_MUSIC:
        return "MUSIC";
    case AUDIO_CLASS_NOISE:
        return "NOISE";
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Classifies consolidated audio metrics into silence, voice, music, or noise.
 *
 * @param stats Pointer to consolidated global audio statistics and frame counts.
 * @param config Validated audio configuration (may be NULL to skip default BPM comparison).
 * @return AudioContentClass Detected classification category (silence, voice, music, or noise).
 */
static AudioContentClass classify_audio_metrics(const GlobalAudioStats *stats,
                                                const AudioFormatConfig *config)
{
    const double silence_rms_threshold = 0.015;
    const double high_centroid_noise_hz = 2500.0;
    const double voice_centroid_max_hz = 2300.0;

    // 1. Silence when no frames or RMS energy is near zero
    if (stats->total_frames_processed == 0 || stats->weighted_overall_rms < silence_rms_threshold)
    {
        return AUDIO_CLASS_SILENCE;
    }

    int has_valid_bpm = (stats->global_bpm >= 50.0f && stats->global_bpm <= 200.0f);
    int bpm_shifted_from_default = (config != NULL) &&
                                   (fabsf(stats->global_bpm - config->default_bpm) >= 3.0f);

    // 2. Music when rhythmic beat tracking is active or low-band musical energy dominates
    if ((has_valid_bpm && (bpm_shifted_from_default || stats->weighted_low_to_high_ratio >= 1.2)) ||
        (stats->weighted_low_to_high_ratio >= 3.0 && stats->weighted_spectral_centroid_hz < high_centroid_noise_hz))
    {
        return AUDIO_CLASS_MUSIC;
    }

    // 3. Noise when high-frequency brightness is dominant and low-to-high energy is weak
    if (stats->weighted_spectral_centroid_hz >= high_centroid_noise_hz &&
        stats->weighted_low_to_high_ratio < 0.8)
    {
        return AUDIO_CLASS_NOISE;
    }

    // 4. Voice for mid-band vocal formants without strong musical beat lock
    if (stats->weighted_spectral_centroid_hz <= voice_centroid_max_hz)
    {
        return AUDIO_CLASS_VOICE;
    }

    return AUDIO_CLASS_NOISE;
}

/**
 * @brief Computes display refresh interval (ms) and per-step attenuation (decay)
 *        from consolidated BPM and audio class.
 *
 * @param stats Pointer to GlobalAudioStats updated in place with refresh_interval_ms
 *              and decay_per_step.
 */
static void compute_refresh_and_decay(GlobalAudioStats *stats)
{
    // 1. Clamp BPM to a valid tempo range or fall back to 120 BPM
    float bpm = (stats->global_bpm >= 50.0f && stats->global_bpm <= 200.0f)
                    ? stats->global_bpm
                    : 120.0f;

    // 2. Compute beat period and derive a ~4-step-per-beat refresh interval clamped to [16 ms .. 100 ms]
    double beat_period_ms = 60000.0 / (double)bpm;
    double target_refresh_ms = beat_period_ms / 4.0;
    if (target_refresh_ms < 16.0)
    {
        target_refresh_ms = 16.0;
    }
    if (target_refresh_ms > 100.0)
    {
        target_refresh_ms = 100.0;
    }

    // 3. Tune refresh cadence and per-tick decay factor according to the detected audio class
    double decay = 0.18;
    switch (stats->content_class)
    {
    case AUDIO_CLASS_SILENCE:
        target_refresh_ms = 60.0;
        decay = 0.35; // Drop quickly to zero when silent
        break;
    case AUDIO_CLASS_MUSIC:
        decay = 0.14; // Smoother tail so beats/bars breathe with tempo
        break;
    case AUDIO_CLASS_VOICE:
        decay = 0.22; // Snappier response for syllables/pauses
        break;
    case AUDIO_CLASS_NOISE:
        decay = 0.25; // Fast attenuation to avoid static sticking
        break;
    }

    // 4. Write the calculated refresh interval and decay rate back to stats
    stats->refresh_interval_ms = target_refresh_ms;
    stats->decay_per_step = decay;
}

int consolidate_audio_stats(const AudioFrameResult *results,
                            int cluster_size,
                            const AudioFormatConfig *config,
                            GlobalAudioStats *out_stats)
{
    // 1. Validate input pointers and cluster size
    if (!results || cluster_size <= 0 || !out_stats)
    {
        return -1;
    }

    // 2. Zero-initialize output statistics structure and weighted accumulators
    *out_stats = (GlobalAudioStats){0};

    size_t total_frames = 0;
    double rms_weighted_sum = 0.0;
    double centroid_weighted_sum = 0.0;
    double ratio_weighted_sum = 0.0;
    double band_weighted_sum[AUDIO_NUM_BANDS] = {0.0};

    double bpm_weighted_sum = 0.0;
    size_t bpm_weight_frames = 0;

    // 3. Accumulate frame-weighted metrics across all cluster ranks
    for (int r = 0; r < cluster_size; r++)
    {
        size_t f = results[r].frames_processed;
        if (f == 0)
        {
            continue;
        }

        double wf = (double)f;
        total_frames += f;
        rms_weighted_sum += results[r].overall_rms * wf;
        centroid_weighted_sum += results[r].spectral_centroid_hz * wf;
        ratio_weighted_sum += results[r].low_to_high_ratio * wf;

        for (int b = 0; b < AUDIO_NUM_BANDS; b++)
        {
            band_weighted_sum[b] += results[r].band_rms[b] * wf;
        }

        if (results[r].bpm >= 50.0f && results[r].bpm <= 200.0f)
        {
            bpm_weighted_sum += (double)results[r].bpm * wf;
            bpm_weight_frames += f;
        }
    }

    // 4. Store total processed frames and normalize weighted averages
    out_stats->total_frames_processed = total_frames;

    if (total_frames > 0)
    {
        double inv_total = 1.0 / (double)total_frames;
        out_stats->weighted_overall_rms = rms_weighted_sum * inv_total;
        out_stats->weighted_spectral_centroid_hz = centroid_weighted_sum * inv_total;
        out_stats->weighted_low_to_high_ratio = ratio_weighted_sum * inv_total;

        for (int b = 0; b < AUDIO_NUM_BANDS; b++)
        {
            out_stats->weighted_band_rms[b] = band_weighted_sum[b] * inv_total;
        }
    }

    // 5. Compute consolidated global BPM with fallback to configured or default tempo
    if (bpm_weight_frames > 0)
    {
        out_stats->global_bpm = (float)(bpm_weighted_sum / (double)bpm_weight_frames);
    }
    else if (config != NULL)
    {
        out_stats->global_bpm = config->default_bpm;
    }
    else
    {
        out_stats->global_bpm = 120.0f;
    }

    // 6. Classify audio content, attach string label, and compute refresh/decay parameters
    out_stats->content_class = classify_audio_metrics(out_stats, config);
    out_stats->label = audio_class_to_string(out_stats->content_class);
    compute_refresh_and_decay(out_stats);

    return 0;
}
