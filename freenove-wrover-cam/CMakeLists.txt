# Freenove ESP32-WROVER CAM — Warblet reference firmware
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(freenove-wrover-cam-demo)

# The catalogue publishes the app image as freenove-wrover-cam-update.bin (the full
# image is freenove-wrover-cam-full.bin, from `idf.py merge-bin -o`). The names say
# what each file is for; IDF names the app after the project, so copy it.
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
            ${CMAKE_BINARY_DIR}/freenove-wrover-cam-update.bin
    VERBATIM)
