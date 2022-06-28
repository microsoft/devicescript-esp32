# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=uart_hal_write_txfifo" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=panic_restart" APPEND)

project(espjd)
