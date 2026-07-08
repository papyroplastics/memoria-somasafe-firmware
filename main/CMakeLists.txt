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
    app_update
    esp_app_format
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

set(NVS_SCRIPT "${SCRIPTS_DIR}/gen_factory_nvs.py")
get_filename_component(FACTORY_NVS_CSV "${GENERATED_DIR}/factory_nvs.csv" ABSOLUTE)

add_custom_command(
  OUTPUT
    "${FACTORY_NVS_CSV}"
  COMMAND
    uv run "${NVS_SCRIPT}" "${SERVER_KEY}" --out "${FACTORY_NVS_CSV}"
  DEPENDS
    "${NVS_SCRIPT}"
    "${SERVER_KEY}"
  COMMENT
    "Generating factory_nvs.csv (test identity)"
  VERBATIM
)

nvs_create_partition_image(factory_data "${FACTORY_NVS_CSV}" FLASH_IN_PROJECT)

