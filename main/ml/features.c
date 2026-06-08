#include <math.h>

#include "esp_dsp.h"

#include "ml/features.h"

#define FFT_N           PPG_SLICE_PPG_COUNT  // 512
#define HR_BAND_LO_BIN  6                    // 0.75 Hz  (first bin ≥ 0.7 Hz at 0.125 Hz/bin)
#define HR_BAND_HI_BIN  28                   // 3.50 Hz

static __attribute__((aligned(16))) float s_hann[FFT_N];
static __attribute__((aligned(16))) float s_fft[FFT_N * 2];  // interleaved Re/Im

void ml_features_init(void) {
    dsps_fft2r_init_fc32(NULL, FFT_N);
    dsps_wind_hann_f32(s_hann, FFT_N);
}

// Single-pass stats for one channel: mean, std, min, max, rms, mean-abs-diff.
static void channel_stats(const float *x, int n,
                           float *out_mean, float *out_std,
                           float *out_min,  float *out_max,
                           float *out_rms,  float *out_mad) {
    float mn = x[0], mx = x[0], sum = 0.0f, sum_sq = 0.0f;
    for (int i = 0; i < n; i++) {
        float v = x[i];
        sum    += v;
        sum_sq += v * v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    float m   = sum / (float)n;
    float var = sum_sq / (float)n - m * m;

    *out_mean = m;
    *out_std  = sqrtf(var < 0.0f ? 0.0f : var);
    *out_min  = mn;
    *out_max  = mx;
    *out_rms  = sqrtf(sum_sq / (float)n);

    float diff_sum = 0.0f;
    for (int i = 1; i < n; i++) {
        float d = x[i] - x[i - 1];
        diff_sum += d < 0.0f ? -d : d;
    }
    *out_mad = diff_sum / (float)(n - 1);
}

void ml_extract_features(const struct ppg_slice *slice, float features[ML_N_FEATURES]) {
    float mean, std, mn, mx, rms, mad;

    // Features 0–6: BVP time-domain
    channel_stats(slice->ppg, PPG_SLICE_PPG_COUNT, &mean, &std, &mn, &mx, &rms, &mad);
    float bvp_mean = mean;
    features[0] = mean;
    features[1] = std;
    features[2] = mn;
    features[3] = mx;
    features[4] = mx - mn;
    features[5] = rms;
    features[6] = mad;

    // Features 7–13: ACC time-domain
    channel_stats(slice->acc, PPG_SLICE_ACC_COUNT, &mean, &std, &mn, &mx, &rms, &mad);
    features[7]  = mean;
    features[8]  = std;
    features[9]  = mn;
    features[10] = mx;
    features[11] = mx - mn;
    features[12] = rms;
    features[13] = mad;

    // Feature 14: BVP zero-crossing rate (mean-centred signal)
    int zcr = 0;
    for (int i = 1; i < PPG_SLICE_PPG_COUNT; i++) {
        float prev = slice->ppg[i - 1] - bvp_mean;
        float curr = slice->ppg[i]     - bvp_mean;
        if ((prev < 0.0f) != (curr < 0.0f)) zcr++;
    }
    features[14] = (float)zcr / (float)(PPG_SLICE_PPG_COUNT - 1);

    // Features 15–16: BVP spectral (Hann-windowed FFT)
    for (int i = 0; i < FFT_N; i++) {
        s_fft[2 * i]     = (slice->ppg[i] - bvp_mean) * s_hann[i];
        s_fft[2 * i + 1] = 0.0f;
    }
    dsps_fft2r_fc32(s_fft, FFT_N);
    dsps_bit_rev_fc32(s_fft, FFT_N);

    float total_power = 0.0f, band_power = 0.0f;
    int   peak_bin    = 1;
    float peak_power  = 0.0f;
    for (int k = 0; k <= FFT_N / 2; k++) {
        float re = s_fft[2 * k];
        float im = s_fft[2 * k + 1];
        float p  = re * re + im * im;
        total_power += p;
        if (k >= HR_BAND_LO_BIN && k <= HR_BAND_HI_BIN) band_power += p;
        if (k > 0 && p > peak_power) { peak_power = p; peak_bin = k; }
    }

    features[15] = (float)peak_bin * ((float)PPG_SAMPLE_RATE / (float)FFT_N);
    features[16] = band_power / (total_power + 1e-8f);
}
