#ifndef ML_FEATURES_H
#define ML_FEATURES_H

#include "ppg/sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ML_BATCH_SIZE 1

// Call once at startup before any ml_extract_features call.
void ml_features_init(void);

// Extract 17 features from one 8-second PPG slice.
// Feature order (must match extract_features in get_dataset.py):
//   [0..6]  BVP: mean, std, min, max, range, rms, mean-abs-diff
//   [7..13] ACC: mean, std, min, max, range, rms, mean-abs-diff
//   [14]    BVP zero-crossing rate
//   [15]    BVP dominant frequency (Hz)
//   [16]    BVP HR-band power ratio (0.7–3.5 Hz)
void ml_extract_features(const struct ppg_slice *slice, float *features);

// Z-score normalize features into out_features using the stats from norm_params.h,
// leaving the raw features untouched (those get echoed to the phone unchanged).
// Requires ml_norm_params.h to have been generated and placed in this directory.
void ml_normalize_features(const float *features, float *out_features);

#ifdef __cplusplus
}
#endif

#endif // ML_FEATURES_H
