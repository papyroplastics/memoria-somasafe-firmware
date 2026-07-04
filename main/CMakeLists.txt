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

# Factory NVS image: serial + device/server ECDSA keys for the factory_data
# partition. If no provisioning CSV exists yet, generate a throwaway test
# identity. Run scripts/gen_factory_nvs.py manually with real keys to provision.
set(_FACTORY_NVS_SCRIPT "${SCRIPTS_DIR}/gen_factory_nvs.py")

get_filename_component(_FACTORY_NVS_CSV "${GENERATED_DIR}/factory_nvs.csv" ABSOLUTE)

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

