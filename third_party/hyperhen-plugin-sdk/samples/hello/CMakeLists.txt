cmake_minimum_required(VERSION 3.20)

onion_add_plugin(
    NAME hello
    TITLE_ID ONIO00001
    VERSION 1.00
    SOURCES source/main.c
    LIBRARIES SceLibcInternal kernel_sys)
