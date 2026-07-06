#include "ml/contract.h"
#include "ml/features.h"

bool ml_contract_supported(uint16_t contract_version) {
  return contract_version == ML_CONTRACT_VERSION;
}

size_t ml_contract_norm_len(void) {
  return 2 * ML_N_FEATURES * sizeof(float);
}

bool ml_contract_shape_valid(int batch_size, int n_features) {
  return batch_size == ML_BATCH_SIZE && n_features == ML_N_FEATURES;
}
