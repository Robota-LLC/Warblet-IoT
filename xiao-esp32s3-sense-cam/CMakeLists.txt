# Seeed Studio XIAO ESP32-S3 Sense — Warblet photo doorbell, MQTTS
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiao-esp32s3-sense-cam-demo)

# The catalogue publishes the app image as xiao-esp32s3-sense-cam-update.bin (the full
# image is xiao-esp32s3-sense-cam-full.bin, from `idf.py merge-bin -o`). The names say
# what each file is for; IDF names the app after the project, so copy it.
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
            ${CMAKE_BINARY_DIR}/xiao-esp32s3-sense-cam-update.bin
    VERBATIM)
