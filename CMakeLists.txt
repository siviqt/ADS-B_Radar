cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET esp32p4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(adsb_bringup VERSION 0.3.2)
add_custom_target(appfs_image ALL
    COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/check_image.py
        ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    DEPENDS gen_project_binary VERBATIM)
