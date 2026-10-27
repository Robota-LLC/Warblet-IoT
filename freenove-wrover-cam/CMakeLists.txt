# Freenove ESP32-WROVER CAM — Warblet reference firmware
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# IDF names the app image after the project: build/freenove-wrover-cam-update.bin is the
# image for a board that is set up already. `idf.py merge-bin -o
# freenove-wrover-cam-full.bin` makes the one for a blank board.
project(freenove-wrover-cam-update)
