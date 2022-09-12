cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=uart_hal_write_txfifo" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=panic_restart" APPEND)
# force linking of ctrl.c
idf_build_set_property(LINK_OPTIONS "-u app_get_device_class" APPEND)

project(espjd)

# this passes the esp-idf compile options down to jacdac-c build
idf_build_get_property(compile_options COMPILE_OPTIONS GENERATOR_EXPRESSION)
idf_build_get_property(c_compile_options C_COMPILE_OPTIONS GENERATOR_EXPRESSION)
add_compile_options("${compile_options}")
add_c_compile_options("${c_compile_options}")

# location of jd_user_config.h
set(JACDAC_USER_CONFIG_DIR "../main")
add_subdirectory(jacdac-c)
