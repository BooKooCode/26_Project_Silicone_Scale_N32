cmake_minimum_required(VERSION 3.22)

project(N32L406_Template LANGUAGES C ASM)

include(cmake/build-options.cmake)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMSIS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS")
set(PERIPH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/N32L40x_StdPeriph")
set(LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/Linker/n32l406cb_flash.ld"
)

file(GLOB PERIPH_SOURCES CONFIGURE_DEPENDS
    "${PERIPH_DIR}/src/*.c"
)

add_executable(${PROJECT_NAME}
    Startup/startup_n32l40x_gcc.s
    "${CMSIS_DIR}/device/system_n32l40x.c"
    ${PERIPH_SOURCES}
)

add_subdirectory(Core)

set(FREERTOS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS"
)

add_library(FreeRTOS OBJECT
    "${FREERTOS_DIR}/tasks.c"
    "${FREERTOS_DIR}/queue.c"
    "${FREERTOS_DIR}/list.c"
    "${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c"
)

add_library(n32_platform INTERFACE)

target_include_directories(n32_platform INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/Config"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/drv_platform/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Common/system_error/inc"
    "${FREERTOS_DIR}/include"
    "${FREERTOS_DIR}/portable/GCC/ARM_CM4F"
    "${FREERTOS_DIR}/CMSIS_RTOS_V2"
)

set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(n32_platform INTERFACE
    "${CMSIS_DIR}/core"
    "${CMSIS_DIR}/device"
    "${PERIPH_DIR}/inc"
)

target_compile_definitions(n32_platform INTERFACE
    N32L40X
    USE_STDPERIPH_DRIVER
    SYSCLK_SRC=1
    SYSCLK_FREQ=16000000
    USE_RTOS_MEMORY=1
    STATIC_DEBUG=1
    TEST_FIRMWARE=1
    "$<$<CONFIG:Debug>:DEBUG>"
)

target_compile_options(n32_platform INTERFACE
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
)

target_link_libraries(FreeRTOS PUBLIC n32_platform)
target_link_libraries(${PROJECT_NAME} PRIVATE n32_platform FreeRTOS)

add_subdirectory(VersionScript)
target_link_libraries(${PROJECT_NAME} PRIVATE VersionScript m)

target_link_options(${PROJECT_NAME} PRIVATE
    "-T${LINKER_SCRIPT}"
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map"
)

set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
    LINK_DEPENDS "${LINKER_SCRIPT}"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin"
    COMMAND "${CMAKE_OBJCOPY}" -O ihex
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${PROJECT_NAME}>"
    VERBATIM
)
