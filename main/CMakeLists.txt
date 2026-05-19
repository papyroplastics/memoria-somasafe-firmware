file(GLOB_RECURSE ble_src "ble/*.c")
file(GLOB_RECURSE ml_src  "ml/*.c" "ml/*.cc")
file(GLOB_RECURSE ppg_src "ppg/*.c")

idf_component_register(
  SRCS 
    "main.c"
    "${ble_src}"
    "${ppg_src}"
    "${ml_src}"

  PRIV_REQUIRES 
    bt 
    nvs_flash
    mbedtls
    esp-tls

  INCLUDE_DIRS
    "."
)
