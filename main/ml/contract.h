#ifndef ML_CONTRACT_H
#define ML_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// see shared/docs/versioning.md
#define ML_CONTRACT_VERSION 1

// Whether the payload's contract version is the one this firmware implements.
bool ml_contract_supported(uint16_t contract_version);

// Byte length of the client-supplied norm params fixed by ML_CONTRACT_VERSION.
size_t ml_contract_norm_len(void);

// Whether the model's input tensor shape matches the contract.
bool ml_contract_shape_valid(int batch_size, int n_features);

#ifdef __cplusplus
}
#endif

#endif // ML_CONTRACT_H
