cmake_minimum_required(VERSION 3.22)

get_filename_component(MODULE_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)

file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME} PRIVATE ${MODULE_SOURCES})

target_include_directories(${MODULE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)


target_link_libraries(${MODULE_NAME} PRIVATE
    n32_platform
)