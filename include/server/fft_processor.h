#ifndef FFT_PROCESSOR_H
#define FFT_PROCESSOR_H

#include <stddef.h>
#include <stdint.h>

#define FFT_WINDOW_SIZE 2048 // Can be 1024 or 2048

/**
 * @brief Processes an audio buffer by segmenting it into frames and applying
 *        the Fast Fourier Transform (FFT) on each window.
 *
 * @param pcm_buffer Pointer to the decrypted PCM audio samples.
 * @param num_bytes Total number of bytes in the buffer.
 * @return int 0 on success, or -1 on failure.
 */
int process_audio_fft(const uint8_t *pcm_buffer, size_t num_bytes);

#endif /* FFT_PROCESSOR_H */
