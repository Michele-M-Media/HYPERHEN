cmake_minimum_required(VERSION 3.20)

onion_add_plugin(
    NAME dynamic_ui
    TITLE_ID ONIO00003
    VERSION 1.00
    SOURCES source/main.c
    LIBRARIES SceLibcInternal kernel_sys)
