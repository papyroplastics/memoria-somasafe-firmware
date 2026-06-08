cmake_minimum_required(VERSION 3.20)

set(DATASET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/datasets" CACHE PATH "Path to datasets directory")
set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(MINIMAL_BUILD ON)
project(somasafe-firmware)
