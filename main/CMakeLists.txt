file(GLOB_RECURSE srcs "srcs/*.c")

idf_component_register(
  SRCS 
    "main.c"
    "${srcs}"
  PRIV_REQUIRES 
    bt nvs_flash
  INCLUDE_DIRS
    "include"
)
