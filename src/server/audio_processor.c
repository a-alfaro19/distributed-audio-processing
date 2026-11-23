#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fftw3.h>
#include "audio_processor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Frequency band boundary edges in Hz for the 5 LED columns:
 *        Sub-bass (20-60 Hz), Mid-bass (60-250 Hz), Mid (250-2000 Hz),
 *        Upper-mid (2000-4000 Hz), and High (4000-20000 Hz).
 */
static const double BAND_EDGES_HZ[AUDIO_NUM_BANDS + 1] = {
    20.0, 60.0, 250.0, 2000.0, 4000.0, 20000.0};

static const double BAND_RMS_CEILING = 0.25;      // RMS amplitude ceiling
static const double MIN_BEAT_INTERVAL_SEC = 0.28; // Minimum time interval (in seconds)
static const double ONSET_FLUX_THRESHOLD = 0.035; // Positive spectral flux threshold
static const size_t MAX_ONSET_HISTORY = 32;       // Maximum number of recent beat onset timestamps

/**
 * @brief Maintains temporal state across sliding audio windows for beat onset
 *        detection and real-time tempo (BPM) estimation.
 */
typedef struct
{
    double prev_rms[AUDIO_NUM_BANDS]; /**< Previous frame RMS amplitude per frequency band. */
    int has_prev_frame;               /**< Non-zero once at least one frame has been processed. */
    double *onset_times_sec;          /**< Ring/history buffer of detected beat onset timestamps (s). */
    size_t onset_count;               /**< Current number of stored onset timestamps. */
    size_t onset_capacity;            /**< Allocated capacity of onset_times_sec. */
    double last_onset_time_sec;       /**< Timestamp (s) of the most recent accepted beat onset. */
    float current_bpm;                /**< Smoothed tempo estimate in beats per minute (BPM). */
} TempoTracker;

/**
 * @brief Allocates FFTW3 aligned memory and creates the Real-to-Complex (r2c) execution plan.
 *
 * @param window_size Size of the FFT window.
 * @param out_in Pointer to store the allocated input buffer.
 * @param out_out Pointer to store the allocated output complex buffer.
 * @param out_plan Pointer to store the created FFTW plan.
 * @return int 0 on success, or -1 on failure.
 */
static int init_fft_resources(size_t window_size, double **out_in, fftw_complex **out_out, fftw_plan *out_plan)
{
    // 1. Allocate SIMD-aligned memory buffers for FFTW input and output
    double *in = (double *)fftw_malloc(sizeof(double) * window_size);
    fftw_complex *out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (window_size / 2 + 1));

    if (!in || !out)
    {
        fprintf(stderr, "[AudioProcessor Error] Failed to allocate SIMD-aligned FFTW buffers.\n");
        if (in)
            fftw_free(in);
        if (out)
            fftw_free(out);
        return -1;
    }

    // 2. Create the Real-to-Complex (r2c) FFT plan using FFTW_ESTIMATE for rapid initialization
    fftw_plan plan = fftw_plan_dft_r2c_1d((int)window_size, in, out, FFTW_ESTIMATE);
    if (!plan)
    {
        fprintf(stderr, "[AudioProcessor Error] Failed to create FFTW r2c plan.\n");
        fftw_free(in);
        fftw_free(out);
        return -1;
    }

    *out_in = in;
    *out_out = out;
    *out_plan = plan;
    return 0;
}

/**
 * @brief Releases FFTW execution plan and aligned input/output buffers safely.
 *
 * @param in Pointer to the real input buffer allocated with fftw_malloc (may be NULL).
 * @param out Pointer to the complex output buffer allocated with fftw_malloc (may be NULL).
 * @param plan FFTW execution plan to destroy (may be NULL).
 */
static void free_fft_resources(double *in, fftw_complex *out, fftw_plan plan)
{
    // 1. Destroy the FFTW plan if it was created
    if (plan)
    {
        fftw_destroy_plan(plan);
    }

    // 2. Release aligned input and output buffers if allocated
    if (in)
    {
        fftw_free(in);
    }
    if (out)
    {
        fftw_free(out);
    }
}

/**
 * @brief Allocates and pre-computes the Hann window coefficients to minimize spectral leakage.
 *
 * @param window_size Size of the FFT window.
 * @return double* Dynamically allocated array of Hann window coefficients, or NULL on failure.
 */
static double *create_hann_window(size_t window_size)
{
    // 1. Allocate memory for the Hann window coefficient buffer
    double *hann_window = (double *)malloc(sizeof(double) * window_size);
    if (!hann_window)
    {
        fprintf(stderr, "[AudioProcessorError] Failed to allocate Hann window buffer.\n");
        return NULL;
    }

    // 2. Compute the Hann window weighting coefficients to minimize spectral leakage
    for (size_t i = 0; i < window_size; i++)
    {
        hann_window[i] = 0.5 * (1.0 - cos((2.0 * M_PI * i) / (window_size - 1)));
    }

    return hann_window;
}

/**
 * @brief Initializes a TempoTracker instance and allocates its onset timestamp buffer.
 *
 * @param tracker Pointer to the TempoTracker structure to initialize.
 * @param capacity Maximum number of beat onset timestamps to store.
 * @return int 0 on success, or -1 on invalid arguments or allocation failure.
 */
static int init_tempo_tracker(TempoTracker *tracker, size_t capacity)
{
    // 1. Validate inputs
    if (!tracker || capacity < 2)
    {
        return -1;
    }

    // 2. Allocate memory for the onset timestamp history
    tracker->onset_times_sec = (double *)malloc(sizeof(double) * capacity);
    if (!tracker->onset_times_sec)
    {
        fprintf(stderr, "[AudioProcessor Error] Failed to allocate BPM onset buffer.\n");
        return -1;
    }

    // 3. Zero-initialize previous band RMS values
    for (int i = 0; i < AUDIO_NUM_BANDS; i++)
    {
        tracker->prev_rms[i] = 0.0;
    }

    // 4. Initialize tracking state and default tempo
    tracker->has_prev_frame = 0;
    tracker->onset_count = 0;
    tracker->onset_capacity = capacity;
    tracker->last_onset_time_sec = -10.0;
    tracker->current_bpm = 120.0f;
    return 0;
}

/**
 * @brief Releases resources allocated by a TempoTracker instance and resets its state fields.
 *
 * @param tracker Pointer to the TempoTracker structure to clean up.
 */
static void free_tempo_tracker(TempoTracker *tracker)
{
    // 1. Safely handle a NULL pointer
    if (!tracker)
    {
        return;
    }

    // 2. Free the onset history buffer and clear the pointer to avoid dangling references
    free(tracker->onset_times_sec);
    tracker->onset_times_sec = NULL;

    // 3. Reset capacity and counter state
    tracker->onset_count = 0;
    tracker->onset_capacity = 0;
    tracker->has_prev_frame = 0;
}

/**
 * @brief Computes per-band RMS amplitudes from an FFT spectrum and maps them
 *        to normalized LED bar heights in the range [0, 5].
 *
 * @param fft_out Complex frequency bins produced by an FFTW real-to-complex transform.
 * @param num_bins Total number of bins in fft_out (window_size / 2 + 1).
 * @param window_size FFT window length in samples used for frequency and amplitude scaling.
 * @param sample_rate Audio sampling rate in Hz.
 * @param out_rms Output array receiving the RMS amplitude for each frequency band.
 * @param out_leds Output array receiving the clamped LED height [0..5] per band.
 */
static void compute_band_rms_and_leds(const fftw_complex *fft_out, size_t num_bins,
                                      size_t window_size, double sample_rate,
                                      double out_rms[AUDIO_NUM_BANDS],
                                      int out_leds[AUDIO_NUM_BANDS])
{
    // 1. Initialize per-band squared-magnitude accumulators, bin counters, and amplitude scale
    double energy_sum[AUDIO_NUM_BANDS] = {0.0};
    size_t bin_counts[AUDIO_NUM_BANDS] = {0};
    const double norm_factor = 2.0 / (double)window_size;

    // 2. Accumulate squared bin magnitudes into their corresponding frequency bands (skipping DC at k = 0)
    for (size_t k = 1; k < num_bins; k++)
    {
        double freq_hz = ((double)k * sample_rate) / (double)window_size;
        if (freq_hz < BAND_EDGES_HZ[0] || freq_hz >= BAND_EDGES_HZ[AUDIO_NUM_BANDS])
        {
            continue;
        }

        double re = fft_out[k][0] * norm_factor;
        double im = fft_out[k][1] * norm_factor;
        double mag_sq = (re * re) + (im * im);

        for (int b = 0; b < AUDIO_NUM_BANDS; b++)
        {
            if (freq_hz >= BAND_EDGES_HZ[b] && freq_hz < BAND_EDGES_HZ[b + 1])
            {
                energy_sum[b] += mag_sq;
                bin_counts[b]++;
                break;
            }
        }
    }

    // 3. Calculate band RMS, store raw band levels, and map to rounded [0..5] LED heights
    for (int b = 0; b < AUDIO_NUM_BANDS; b++)
    {
        double rms = (bin_counts[b] > 0)
                         ? sqrt(energy_sum[b] / (double)bin_counts[b])
                         : 0.0;
        out_rms[b] = rms;

        double scaled = (rms / BAND_RMS_CEILING) * 5.0;
        if (scaled < 0.0)
            scaled = 0.0;
        if (scaled > 5.0)
            scaled = 5.0;
        out_leds[b] = (int)llround(scaled);
    }

    fprintf(stderr, "[Debug RMS] B0=%.5f B1=%.5f B2=%.5f B3=%.5f B4=%.5f | LEDs=[%d,%d,%d,%d,%d]\n",
            out_rms[0], out_rms[1], out_rms[2], out_rms[3], out_rms[4],
            out_leds[0], out_leds[1], out_leds[2], out_leds[3], out_leds[4]);
}

/**
 * @brief Updates beat onset history from low-band spectral flux and estimates tempo in BPM.
 *
 * @param tracker Pointer to the initialized TempoTracker state.
 * @param current_rms Current frame RMS amplitude per frequency band.
 * @param frame_time_sec Timestamp of the current audio window in seconds.
 * @return float Smoothed tempo estimate in beats per minute (BPM).
 */
static float estimate_bpm(TempoTracker *tracker, const double current_rms[AUDIO_NUM_BANDS],
                          double frame_time_sec)
{
    // 1. Compute positive spectral flux across Sub-bass (0) and Mid-bass (1) bands
    double flux = 0.0;
    if (tracker->has_prev_frame)
    {
        for (int b = 0; b <= 1; b++)
        {
            double diff = current_rms[b] - tracker->prev_rms[b];
            if (diff > 0.0)
            {
                flux += diff;
            }
        }
    }

    // 2. Save current RMS frame as the reference for the next window
    for (int b = 0; b < AUDIO_NUM_BANDS; b++)
    {
        tracker->prev_rms[b] = current_rms[b];
    }
    tracker->has_prev_frame = 1;

    // 3. Register a new beat onset if flux exceeds threshold and refractory period has passed
    if (flux > ONSET_FLUX_THRESHOLD &&
        (frame_time_sec - tracker->last_onset_time_sec) >= MIN_BEAT_INTERVAL_SEC)
    {
        tracker->last_onset_time_sec = frame_time_sec;

        if (tracker->onset_count < tracker->onset_capacity)
        {
            tracker->onset_times_sec[tracker->onset_count++] = frame_time_sec;
        }
        else
        {
            for (size_t i = 1; i < tracker->onset_capacity; i++)
            {
                tracker->onset_times_sec[i - 1] = tracker->onset_times_sec[i];
            }
            tracker->onset_times_sec[tracker->onset_capacity - 1] = frame_time_sec;
        }
    }

    // 4. Average valid inter-onset intervals (50-200 BPM) and smooth the BPM estimate
    if (tracker->onset_count >= 2)
    {
        double dt_sum = 0.0;
        size_t valid_intervals = 0;

        for (size_t i = 1; i < tracker->onset_count; i++)
        {
            double dt = tracker->onset_times_sec[i] - tracker->onset_times_sec[i - 1];
            if (dt >= 0.30 && dt <= 1.20) // ~50 to 200 BPM
            {
                dt_sum += dt;
                valid_intervals++;
            }
        }

        if (valid_intervals > 0)
        {
            double avg_beat_sec = dt_sum / (double)valid_intervals;
            float detected_bpm = (float)(60.0 / avg_beat_sec);
            tracker->current_bpm = 0.7f * tracker->current_bpm + 0.3f * detected_bpm;
        }
    }

    // 5. Return the updated smoothed BPM
    return tracker->current_bpm;
}

/**
 * @brief Iterates over overlapping PCM audio windows, executes the FFTW transform,
 *        and updates LED band levels and BPM estimates for each frame.
 *
 * @param samples Pointer to 16-bit signed PCM audio samples.
 * @param total_samples Total number of available PCM samples in samples.
 * @param window_size FFT window length in samples.
 * @param hop_size Step size in samples between consecutive sliding windows.
 * @param sample_rate Audio sampling rate in Hz.
 * @param hann_window Precomputed Hann window coefficients of length window_size.
 * @param in FFTW real input buffer of length window_size.
 * @param out FFTW complex output buffer of length (window_size / 2 + 1).
 * @param plan Pre-configured FFTW real-to-complex execution plan.
 * @param tracker Pointer to initialized TempoTracker state.
 * @param last_leds Output array storing the latest [0..5] LED heights per band.
 * @param out_bpm Output pointer receiving the latest smoothed BPM estimate.
 * @return size_t Number of sliding window frames processed.
 */
static size_t run_window_analysis_loop(const int16_t *samples, size_t total_samples,
                                       size_t window_size, size_t hop_size,
                                       double sample_rate, const double *hann_window,
                                       double *in, fftw_complex *out, fftw_plan plan,
                                       TempoTracker *tracker, int last_leds[AUDIO_NUM_BANDS],
                                       float *out_bpm)
{
    // 1. Initialize output bin count, per-band RMS scratch buffer, and frame counter
    const size_t num_bins = window_size / 2 + 1;
    double band_rms[AUDIO_NUM_BANDS] = {0.0};
    size_t frames = 0;

    // 2. Slide across the PCM sample stream in hop_size increments
    for (size_t i = 0; i + window_size <= total_samples; i += hop_size)
    {
        // 3. Normalize 16-bit PCM samples to [-1.0, 1.0] and apply the Hann window
        for (size_t j = 0; j < window_size; j++)
        {
            in[j] = ((double)samples[i + j] / 32768.0) * hann_window[j];
        }

        // 4. Execute the forward real-to-complex FFT transform
        fftw_execute(plan);

        // 5. Compute frame timestamp, update LED band heights, and refine BPM estimate
        double frame_time_sec = (double)i / sample_rate;
        compute_band_rms_and_leds(out, num_bins, window_size, sample_rate,
                                  band_rms, last_leds);
        *out_bpm = estimate_bpm(tracker, band_rms, frame_time_sec);

        frames++;
    }

    // 6. Return total processed window count
    return frames;
}

int process_audio_chunk(const uint8_t *pcm_buffer, size_t num_bytes,
                        double sample_rate, AudioAnalysisSummary *out_summary)
{
    // 1. Ensure the PCM buffer and sampling rate are valid and non-empty
    if (!pcm_buffer || num_bytes == 0 || sample_rate <= 0.0)
    {
        fprintf(stderr, "[AudioProcessor Error] Invalid PCM buffer passed for processing.\n");
        return -1;
    }

    // 2. Calculate total 16-bit signed PCM sample count and cast the byte buffer
    size_t total_samples = num_bytes / sizeof(int16_t); // standard 16-bit signed PCM audio (2 bytes per sample)
    const int16_t *samples = (const int16_t *)pcm_buffer;

    // 3. Configure FFT window size and 50% hop size
    size_t window_size = AUDIO_FFT_WINDOW_SIZE;
    size_t hop_size = window_size / 2; // 50% overlap

    // 4. Ensure the buffer contains enough samples to fill at least one FFT window
    if (total_samples < window_size)
    {
        fprintf(stderr, "[FFT Warning] Buffer size (%zu samples) is smaller than the FFT window (%zu).\n",
                total_samples, window_size);

        // Clean out summary
        if (out_summary)
        {
            for (int b = 0; b < AUDIO_NUM_BANDS; b++)
                out_summary->led_heights[b] = 0;
            out_summary->bpm = 120.0f;
            out_summary->frames_processed = 0;
        }
        return 0;
    }

    // 5. Initialize FFTW memory buffers, window pointers, tempo state, and execution plan
    double *in = NULL;
    fftw_complex *out = NULL;
    fftw_plan plan = NULL;
    double *hann_window = NULL;
    TempoTracker tracker = {0};

    if (init_fft_resources(window_size, &in, &out, &plan) < 0)
    {
        return -1;
    }

    // 6. Create and pre-compute Hann window coefficients
    hann_window = create_hann_window(window_size);
    if (!hann_window)
    {
        free_fft_resources(in, out, plan);
        return -1;
    }

    // 7. Initialize temporal onset history and BPM tracking state
    if (init_tempo_tracker(&tracker, MAX_ONSET_HISTORY) < 0)
    {
        free(hann_window);
        free_fft_resources(in, out, plan);
        return -1;
    }

    // 8. Execute sliding-window FFT analysis, LED mapping, BPM estimation, and populate summary
    int leds[AUDIO_NUM_BANDS] = {0};
    float bpm = 120.0f;
    size_t frames = run_window_analysis_loop(samples, total_samples, window_size, hop_size,
                                             sample_rate, hann_window, in, out, plan,
                                             &tracker, leds, &bpm);

    // 9. Save results on summary
    if (out_summary)
    {
        for (int b = 0; b < AUDIO_NUM_BANDS; b++)
        {
            out_summary->led_heights[b] = leds[b];
        }
        out_summary->bpm = bpm;
        out_summary->frames_processed = frames;
    }

    // 10. Clean up tempo tracker, Hann window, and FFTW resources
    free_tempo_tracker(&tracker);
    free(hann_window);
    free_fft_resources(in, out, plan);

    return 0;
}
