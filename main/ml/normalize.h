#ifndef ML_NORMALIZE_H
#define ML_NORMALIZE_H

#include "ml/features.h"

#ifdef __cplusplus
extern "C" {
#endif

// Z-score normalize features in-place using the stats from norm_params.h.
// Requires ml_norm_params.h to have been generated and placed in this directory.
void ml_normalize_features(float *features);

#ifdef __cplusplus
}
#endif

#endif  // ML_NORMALIZE_H
