target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/app_freertos.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/rtc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/test_low_power.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/test_battery.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/test_cal_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/test_display.c
    ${PROJECT_SOURCE_DIR}/Service/timer_tool/src/timer_tools.c
    ${PROJECT_SOURCE_DIR}/CustomDrivers/buzzer/Src/dev_buzzer.c
    ${PROJECT_SOURCE_DIR}/CustomDrivers/cs1237/Src/dev_cs1237.c
    ${PROJECT_SOURCE_DIR}/CustomDrivers/tm1640/Src/dev_tm1640b.c
    ${PROJECT_SOURCE_DIR}/App/run/Src/sleep_probe.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${PROJECT_SOURCE_DIR}/App/app_rtos/inc
    ${PROJECT_SOURCE_DIR}/Common/drv_platform/inc
    ${PROJECT_SOURCE_DIR}/Service/timer_tool/inc
    ${PROJECT_SOURCE_DIR}/CustomDrivers/buzzer/Inc
    ${PROJECT_SOURCE_DIR}/CustomDrivers/cs1237/Inc
    ${PROJECT_SOURCE_DIR}/CustomDrivers/tm1640/Inc
    ${PROJECT_SOURCE_DIR}/App/log/inc
    ${PROJECT_SOURCE_DIR}/App/run/Inc
)
