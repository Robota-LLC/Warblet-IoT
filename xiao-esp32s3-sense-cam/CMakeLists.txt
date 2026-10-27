# Seeed Studio XIAO ESP32-S3 Sense — Warblet photo doorbell, MQTTS
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# IDF names the app image after the project: build/xiao-esp32s3-sense-cam-update.bin is the
# image for a board that is set up already. `idf.py merge-bin -o
# xiao-esp32s3-sense-cam-full.bin` makes the one for a blank board.
project(xiao-esp32s3-sense-cam-update)
