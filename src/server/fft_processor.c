#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fftw3.h>
#include "fft_processor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        fprintf(stderr, "[FFT Error] Failed to allocate FFTW aligned memory.\n");
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
        fprintf(stderr, "[FFT Error] Failed to create FFTW plan.\n");
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
        fprintf(stderr, "[FFT Error] Failed to allocate Hann window buffer.\n");
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
 * @brief Executes the sliding window loop over the PCM samples, applying the Hann window,
 *        running the FFT, and processing the frequency bins.
 *
 * @param samples Pointer to the input PCM audio samples.
 * @param total_samples Total number of available samples.
 * @param window_size Size of each FFT window.
 * @param hop_size Step size for the sliding window (overlap).
 * @param hann_window Pre-computed Hann window coefficients.
 * @param in Input buffer for FFTW.
 * @param out Output complex buffer for FFTW.
 * @param plan FFTW execution plan.
 * @param num_bins Number of frequency bins (window_size / 2 + 1).
 * @param out_frames_processed Pointer to store the total number of frames successfully processed.
 * @return int 0 on success, or -1 on failure.
 */
static int execute_sliding_fft(const int16_t *samples, size_t total_samples,
                               size_t window_size, size_t hop_size,
                               const double *hann_window, double *in,
                               fftw_complex *out, fftw_plan plan,
                               size_t num_bins, size_t *out_frames_processed)
{
    // 1. Verify valid arguments
    if (!samples || !hann_window || !in || !out || !plan)
    {
        fprintf(stderr, "[FFT Error] Invalid arguments passed to execute_sliding_fft.\n");
        return -1;
    }

    size_t frames_processed = 0;

    // 2. Execute sliding window loop over the PCM samples
    for (size_t i = 0; i + window_size <= total_samples; i += hop_size)
    {
        // 2.1 Load raw samples, normalize to [-1.0, 1.0] range, and apply Hann window
        for (size_t j = 0; j < window_size; j++)
        {
            in[j] = ((double)samples[i + j] / 32768.0) * hann_window[j];
        }

        // 2.2 Execute the Fast Fourier Transform for the current window
        fftw_execute(plan);

        // 2.3 Extract features and map to LED bands / BPM
        // TODO:

        frames_processed++;
    }

    *out_frames_processed = frames_processed;
    return 0;
}

int process_audio_fft(const uint8_t *pcm_buffer, size_t num_bytes)
{
    // 1. Ensure the buffer is not null or empty
    if (!pcm_buffer || num_bytes == 0)
    {
        fprintf(stderr, "[FFT Error] Invalid PCM buffer passed for processing.\n");
        return -1;
    }

    // 2. Calculate number of audio samples
    size_t total_samples = num_bytes / sizeof(int16_t); // standard 16-bit signed PCM audio (2 bytes per sample)
    const int16_t *samples = (const int16_t *)pcm_buffer;

    // 3. Calculate hop size
    size_t window_size = FFT_WINDOW_SIZE;
    size_t hop_size = window_size / 2; // 50% overlap

    // 4. Ensure the buffer contains enough samples to fill at least one FFT window
    if (total_samples < window_size)
    {
        fprintf(stderr, "[FFT Warning] Buffer size (%zu samples) is smaller than the FFT window (%zu).\n",
                total_samples, window_size);
        return 0;
    }

    // 5. Initialize FFTW memory buffers and execution plan
    double *in = NULL;
    fftw_complex *out = NULL;
    fftw_plan plan = NULL;
    if (init_fft_resources(window_size, &in, &out, &plan) < 0)
    {
        return -1;
    }

    // 6. Create and pre-compute Hann window coefficients
    double *hann_window = create_hann_window(window_size);
    if (!hann_window)
    {
        fftw_destroy_plan(plan);
        fftw_free(in);
        fftw_free(out);
        return -1;
    }

    size_t frames_processed = 0;
    size_t num_bins = window_size / 2 + 1;

    // 7. Execute sliding window loop over the PCM samples
    if (execute_sliding_fft(samples, total_samples, window_size, hop_size,
                            hann_window, in, out, plan, num_bins, &frames_processed) < 0)
    {
        fprintf(stderr, "[FFT Error] Failed during sliding window FFT execution.\n");
        free(hann_window);
        fftw_destroy_plan(plan);
        fftw_free(in);
        fftw_free(out);
        return -1;
    }

    printf("[FFT Processor] Successfully processed %zu frames (Window: %zu, Hop: %zu) using FFTW3.\n",
           frames_processed, window_size, hop_size);

    // 9. Clean up resources
    free(hann_window);
    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);

    return 0;
}
