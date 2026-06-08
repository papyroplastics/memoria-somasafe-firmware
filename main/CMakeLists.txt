file(GLOB_RECURSE ble_src "ble/*.c")
file(GLOB_RECURSE utils_src "utils/*.c")
file(GLOB_RECURSE ml_src  "ml/*.c" "ml/*.cc")
file(GLOB_RECURSE ppg_src "ppg/*.c")

idf_component_register(
  SRCS
    "main.c"
    "${ble_src}"
    "${utils_src}"
    "${ppg_src}"
    "${ml_src}"

  PRIV_REQUIRES
    bt
    nvs_flash
    mbedtls
    esp-tls
    esp_driver_uart
    esp_driver_gpio
    espressif__esp-dsp

  INCLUDE_DIRS
    "."
)

set(_GEN_H_SCRIPT "${SCRIPTS_DIR}/gen_norm_params_h.py")
set(_FEAT_STATS_NPY "${DATASET_DIR}/feature-anomaly/feature_stats.npy")
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

