cmake_minimum_required(VERSION 3.20)

onion_add_plugin(
    NAME daemon_sample
    TITLE_ID ONIO00002
    VERSION 1.00
    SOURCES source/main.c
    LIBRARIES SceLibcInternal kernel_sys)
