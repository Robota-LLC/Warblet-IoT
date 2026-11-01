# Freenove ESP32-WROVER CAM — Warblet reference firmware, MQTTS
cmake_minimum_required(VERSION 3.16)

# The release in which this demo's bytes last changed. The board announces it
# as fw=, and demo.json's firmware.fw says the same.
set(PROJECT_VER "v1.6.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# IDF names the app image after the project: build/freenove-wrover-cam-mqtts-update.bin is the
# image for a board that is set up already. `idf.py merge-bin -o
# freenove-wrover-cam-mqtts-full.bin` makes the one for a blank board.
project(freenove-wrover-cam-mqtts-update)
