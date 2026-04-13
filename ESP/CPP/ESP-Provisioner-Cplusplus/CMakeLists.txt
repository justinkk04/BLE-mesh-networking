# ESP-Provisioner-Cplusplus — Top-level CMake file
#
# Standard ESP-IDF project boilerplate.
# Build with:
#   idf.py set-target esp32c6
#   idf.py build
#   idf.py flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP-Provisioner-Cplusplus)
