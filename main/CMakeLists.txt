file(GLOB_RECURSE ble_src "ble/*.c")
file(GLOB_RECURSE utils_src "utils/*.c")
file(GLOB_RECURSE ml_src  "ml/*.c" "ml/*.cc")
file(GLOB_RECURSE ppg_src "ppg/*.c")
file(GLOB_RECURSE device_src "device/*.c")

idf_component_register(
  SRCS
    "main.c"
    "${ble_src}"
    "${utils_src}"
    "${ppg_src}"
    "${ml_src}"
    "${device_src}"

  PRIV_REQUIRES
    bt
    nvs_flash
    mbedtls
    esp-tls
    esp_hw_support
    esp_driver_uart
    esp_driver_gpio
    espressif__esp-dsp

  INCLUDE_DIRS
    "."
)

set(_GEN_H_SCRIPT "${SCRIPTS_DIR}/gen_norm_params_h.py")
set(_FEAT_STATS_NPY "${DATASET_DIR}/anomalous-features/feature_stats.npy")
set(_NORM_PARAMS_H "${COMPONENT_DIR}/ml/norm_params.h")

add_custom_command(
  OUTPUT 
    "${_NORM_PARAMS_H}"
  COMMAND 
    uv run "${_GEN_H_SCRIPT}" "${_FEAT_STATS_NPY}" "${_NORM_PARAMS_H}"
  DEPENDS 
    "${_GEN_H_SCRIPT}"
    "${_FEAT_STATS_NPY}"
  COMMENT 
    "Generating ml/norm_params.h"
  VERBATIM
)

add_custom_target(norm_params_h DEPENDS "${_NORM_PARAMS_H}")
add_dependencies(${COMPONENT_LIB} norm_params_h)

set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${_NORM_PARAMS_H}")

# Factory NVS image: serial + device/server ECDSA keys for the factory_data
# partition. If no provisioning CSV exists yet, generate a throwaway test
# identity. Run scripts/gen_factory_nvs.py manually with real keys to provision.
set(_FACTORY_NVS_SCRIPT "${SCRIPTS_DIR}/gen_factory_nvs.py")
get_filename_component(_FACTORY_NVS_CSV "${COMPONENT_DIR}/../factory_nvs.csv" ABSOLUTE)

add_custom_command(
  OUTPUT
    "${_FACTORY_NVS_CSV}"
  COMMAND
    uv run "${_FACTORY_NVS_SCRIPT}" --out "${_FACTORY_NVS_CSV}"
  DEPENDS
    "${_FACTORY_NVS_SCRIPT}"
  COMMENT
    "Generating factory_nvs.csv (test identity)"
  VERBATIM
)

nvs_create_partition_image(factory_data "${_FACTORY_NVS_CSV}" FLASH_IN_PROJECT)

