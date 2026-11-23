// ======================================================
// Audio Configuration Loader & Validator Implementation
// ======================================================

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_config.h"

#define CONFIG_LINE_MAX 256
#define CONFIG_PATH_MAX 512

/**
 * @brief Trims leading and trailing whitespace in-place from a mutable string.
 *
 * @param str Null-terminated string to trim.
 * @return char* Pointer to the trimmed string start.
 */
static char *trim_whitespace(char *str)
{
    // 1. Advance pointer past leading whitespace
    while (*str && isspace((unsigned char)*str))
    {
        str++;
    }

    // 2. Trim trailing whitespace from the end of the string
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1]))
    {
        str[--len] = '\0';
    }

    return str;
}

/**
 * @brief Parses a comma-separated list of band edge frequencies in Hz into an array.
 *
 * @param val Mutable string containing comma-separated numeric values.
 * @param out_edges Output array receiving AUDIO_NUM_BANDS + 1 frequency edges.
 * @return int 0 if all expected edges were parsed, or -1 otherwise.
 */
static int parse_band_edges(char *val, double out_edges[AUDIO_NUM_BANDS + 1])
{
    // 1. Validate input buffer and output array
    if (!val || !out_edges)
    {
        return -1;
    }

    // 2. Tokenize comma-separated frequency values and convert to double
    int count = 0;
    char *token = strtok(val, ",");
    while (token != NULL && count <= AUDIO_NUM_BANDS)
    {
        char *trimmed_token = trim_whitespace(token);
        if (*trimmed_token == '\0')
        {
            return -1;
        }

        char *endptr = NULL;
        double edge = strtod(trimmed_token, &endptr);
        if (endptr == trimmed_token)
        {
            return -1;
        }

        out_edges[count++] = edge;
        token = strtok(NULL, ",");
    }

    // 3. Ensure exact edge count and no trailing extra tokens
    if (count != AUDIO_NUM_BANDS + 1 || token != NULL)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief Opens the audio configuration file using the path defined in AUDIO_CONFIG_PATH.
 *
 * @param resolved_path Buffer to store the opened path (may be NULL).
 * @param resolved_size Size of resolved_path buffer in bytes.
 * @return FILE* Open file stream on success, or NULL if env var is missing or file cannot be opened.
 */
static FILE *open_audio_config_file(char *resolved_path, size_t resolved_size)
{
    // 1. Verify that AUDIO_CONFIG_PATH environment variable is set
    const char *env_path = getenv("AUDIO_CONFIG_PATH");
    if (env_path == NULL || env_path[0] == '\0')
    {
        fprintf(stderr, "[AudioConfig Error] AUDIO_CONFIG_PATH environment variable is not set.\n");
        return NULL;
    }

    // 2. Open the configuration file at the specified path
    FILE *fp = fopen(env_path, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "[AudioConfig Error] Failed to open '%s': %s\n",
                env_path, strerror(errno));
        return NULL;
    }

    if (resolved_path != NULL && resolved_size > 0)
    {
        snprintf(resolved_path, resolved_size, "%s", env_path);
    }

    return fp;
}

/**
 * @brief Applies a single parsed key = value entry into an AudioFormatConfig struct.
 *
 * @param key Configuration key name.
 * @param val Configuration value string.
 * @param filepath Source file path used for diagnostic messages.
 * @param out_config Target configuration struct to update.
 * @return int 0 on success, or -1 if a value is syntactically invalid.
 */
static int apply_config_kv(const char *key, char *val, const char *filepath,
                           AudioFormatConfig *out_config)
{
    // 1. Match known configuration keys and convert values
    if (strcmp(key, "audio_format") == 0)
    {
        snprintf(out_config->audio_format, AUDIO_FORMAT_NAME_MAX, "%s", val);
    }
    else if (strcmp(key, "bits_per_sample") == 0)
    {
        out_config->bits_per_sample = atoi(val);
    }
    else if (strcmp(key, "channels") == 0)
    {
        out_config->channels = atoi(val);
    }
    else if (strcmp(key, "sample_rate") == 0)
    {
        out_config->sample_rate = atof(val);
    }
    else if (strcmp(key, "fft_window_size") == 0)
    {
        out_config->fft_window_size = (size_t)strtoull(val, NULL, 10);
    }
    else if (strcmp(key, "overlap_ratio") == 0)
    {
        out_config->overlap_ratio = atof(val);
    }
    else if (strcmp(key, "band_edges_hz") == 0)
    {
        if (parse_band_edges(val, out_config->band_edges_hz) < 0)
        {
            fprintf(stderr, "[AudioConfig Error] Invalid band_edges_hz format in '%s'.\n", filepath);
            return -1;
        }
    }
    else if (strcmp(key, "band_rms_ceiling") == 0)
    {
        out_config->band_rms_ceiling = atof(val);
    }
    else if (strcmp(key, "min_beat_interval_sec") == 0)
    {
        out_config->min_beat_interval_sec = atof(val);
    }
    else if (strcmp(key, "onset_flux_threshold") == 0)
    {
        out_config->onset_flux_threshold = atof(val);
    }
    else if (strcmp(key, "max_onset_history") == 0)
    {
        out_config->max_onset_history = (size_t)strtoull(val, NULL, 10);
    }
    else if (strcmp(key, "default_bpm") == 0)
    {
        out_config->default_bpm = strtof(val, NULL);
    }
    else
    {
        fprintf(stderr, "[AudioConfig Warning] Unknown configuration key '%s' ignored.\n", key);
    }

    return 0;
}

/**
 * @brief Initializes an AudioFormatConfig struct with fallback default values.
 *
 * @param config Pointer to configuration struct to initialize.
 */
static void init_default_audio_config(AudioFormatConfig *config)
{
    // 1. Safely ignore NULL pointer
    if (!config)
    {
        return;
    }

    // 2. Populate default supported audio and FFT parameters
    snprintf(config->audio_format, AUDIO_FORMAT_NAME_MAX, "PCM");
    config->bits_per_sample = 16;
    config->channels = 1;
    config->sample_rate = 44100.0;
    config->fft_window_size = AUDIO_FFT_WINDOW_SIZE;
    config->overlap_ratio = 0.50;

    static const double default_edges[AUDIO_NUM_BANDS + 1] = {
        20.0, 60.0, 250.0, 2000.0, 4000.0, 20000.0};
    for (int i = 0; i <= AUDIO_NUM_BANDS; i++)
    {
        config->band_edges_hz[i] = default_edges[i];
    }

    config->band_rms_ceiling = 0.25;
    config->min_beat_interval_sec = 0.28;
    config->onset_flux_threshold = 0.035;
    config->max_onset_history = 32;
    config->default_bpm = 120.0f;
}

/**
 * @brief Validates that an AudioFormatConfig contains supported audio and DSP parameters.
 *
 * @param config Configuration structure to validate.
 * @return int 0 if valid and supported, or -1 otherwise.
 */
static int validate_audio_config(const AudioFormatConfig *config)
{
    // 1. Check struct pointer and supported format string
    if (!config)
    {
        return -1;
    }

    if (strcmp(config->audio_format, "PCM") != 0)
    {
        fprintf(stderr, "[AudioConfig Error] Unsupported audio_format '%s' (only 'PCM' is supported).\n",
                config->audio_format);
        return -1;
    }

    // 2. Validate sample width and channel count supported by the DSP pipeline
    if (config->bits_per_sample != 16)
    {
        fprintf(stderr, "[AudioConfig Error] Unsupported bits_per_sample %d (only 16-bit is supported).\n",
                config->bits_per_sample);
        return -1;
    }

    if (config->channels != 1)
    {
        fprintf(stderr, "[AudioConfig Error] Unsupported channels %d (only mono=1 is supported).\n",
                config->channels);
        return -1;
    }

    // 3. Validate sampling rate, FFT window size, and overlap bounds
    if (config->sample_rate <= 0.0 || config->fft_window_size < 64 ||
        config->overlap_ratio < 0.0 || config->overlap_ratio >= 0.9)
    {
        fprintf(stderr, "[AudioConfig Error] Invalid DSP parameters in configuration.\n");
        return -1;
    }

    // 4. Validate frequency band edges are positive and strictly increasing
    for (int i = 0; i < AUDIO_NUM_BANDS; i++)
    {
        if (config->band_edges_hz[i] <= 0.0 ||
            config->band_edges_hz[i] >= config->band_edges_hz[i + 1])
        {
            fprintf(stderr, "[AudioConfig Error] Invalid band_edges_hz ordering at index %d.\n", i);
            return -1;
        }
    }

    // 5. Validate LED scaling ceiling and tempo tracker parameters
    if (config->band_rms_ceiling <= 0.0 ||
        config->min_beat_interval_sec <= 0.0 ||
        config->onset_flux_threshold < 0.0 ||
        config->max_onset_history < 2 ||
        config->default_bpm <= 0.0f)
    {
        fprintf(stderr, "[AudioConfig Error] Invalid LED or BPM tracking thresholds in configuration.\n");
        return -1;
    }

    return 0;
}

int load_audio_config(AudioFormatConfig *out_config)
{
    // 1. Validate output pointer
    if (!out_config)
    {
        return -1;
    }

    // 2. Start configuration from known defaults
    init_default_audio_config(out_config);

    // 3. Open the .config file
    char used_path[CONFIG_PATH_MAX] = {0};
    FILE *fp = open_audio_config_file(used_path, sizeof(used_path));
    if (!fp)
    {
        return -1;
    }

    // 4. Parse loaded configuration values
    char line[CONFIG_LINE_MAX];
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *comment = strchr(line, '#');
        if (comment)
        {
            *comment = '\0';
        }

        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0')
        {
            continue;
        }

        char *eq = strchr(trimmed, '=');
        if (!eq)
        {
            fprintf(stderr, "[AudioConfig Warning] Ignoring malformed line: %s\n", trimmed);
            continue;
        }

        *eq = '\0';
        char *key = trim_whitespace(trimmed);
        char *val = trim_whitespace(eq + 1);

        if (apply_config_kv(key, val, used_path, out_config) < 0)
        {
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    // 4. Validate parsed configuration
    return validate_audio_config(out_config);
}
