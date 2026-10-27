# FireBeetle 2 ESP32-C5 — Warblet reference firmware
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# IDF names the app image after the project: build/firebeetle-c5-update.bin is the
# image for a board that is set up already. `idf.py merge-bin -o
# firebeetle-c5-full.bin` makes the one for a blank board.
project(firebeetle-c5-update)
