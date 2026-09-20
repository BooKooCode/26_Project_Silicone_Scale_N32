target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/app_freertos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/app_error_trace.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/rtc.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${PROJECT_SOURCE_DIR}/App/app_rtos/inc
    ${PROJECT_SOURCE_DIR}/Common/drv_platform/inc
    ${PROJECT_SOURCE_DIR}/Service/timer_tool/inc
    ${PROJECT_SOURCE_DIR}/CustomDrivers/buzzer/Inc
)