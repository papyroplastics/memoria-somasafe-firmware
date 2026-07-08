cmake_minimum_required(VERSION 3.20)

set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared")
set(GENERATED_DIR "${SHARED_DIR}/gen")

set(SERVER_KEY "${GENERATED_DIR}/server-public-key.pem" 
  CACHE PATH "Path to server's public key")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)
project(somasafe-firmware)
