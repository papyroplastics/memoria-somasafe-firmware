cmake_minimum_required(VERSION 3.20)

set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared" 
  CACHE PATH "Path to shared repository")
set(GENERATED_DIR "${SHARED_DIR}/gen")

set(DATASET_DIR "${GENERATED_DIR}/datasets" 
  CACHE PATH "Path to datasets directory")

set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)
project(somasafe-firmware)
