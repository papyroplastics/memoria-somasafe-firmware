#ifndef FACTORY_DATA_H
#define FACTORY_DATA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Accessors for the factory-provisioned identity NVS partition ("factory_data"
// partition, "factory" namespace — see the README's provisioning section).
// `len` follows nvs_get_blob/nvs_get_str semantics: in = capacity, out =
// stored length (including the NUL terminator for strings). Return esp_err_t.
int factory_data_get_blob(const char *key, void *buf, size_t *len);
int factory_data_get_str(const char *key, char *buf, size_t *len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif // FACTORY_DATA_H
