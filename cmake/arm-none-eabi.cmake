set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_GCC_BIN "" CACHE PATH "Directory containing arm-none-eabi-gcc")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_GCC_BIN)

if(NOT ARM_GCC_BIN)
    message(FATAL_ERROR "ARM_GCC_BIN must point to the GNU Arm toolchain bin directory.")
endif()

file(TO_CMAKE_PATH "${ARM_GCC_BIN}" ARM_GCC_BIN)
set(_ARM_EXE_SUFFIX "")
if(CMAKE_HOST_WIN32)
    set(_ARM_EXE_SUFFIX ".exe")
endif()

set(ARM_GCC "${ARM_GCC_BIN}/arm-none-eabi-gcc${_ARM_EXE_SUFFIX}")
set(ARM_OBJCOPY "${ARM_GCC_BIN}/arm-none-eabi-objcopy${_ARM_EXE_SUFFIX}")
set(ARM_SIZE "${ARM_GCC_BIN}/arm-none-eabi-size${_ARM_EXE_SUFFIX}")

foreach(_ARM_TOOL IN ITEMS ARM_GCC ARM_OBJCOPY ARM_SIZE)
    if(NOT EXISTS "${${_ARM_TOOL}}")
        message(FATAL_ERROR "GNU Arm tool not found: ${${_ARM_TOOL}}")
    endif()
endforeach()

set(CMAKE_C_COMPILER "${ARM_GCC}" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_ASM_COMPILER "${ARM_GCC}" CACHE FILEPATH "ASM compiler" FORCE)
set(CMAKE_OBJCOPY "${ARM_OBJCOPY}" CACHE FILEPATH "Object copy tool" FORCE)
set(CMAKE_SIZE "${ARM_SIZE}" CACHE FILEPATH "Size tool" FORCE)

set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
)

set(CMAKE_ASM_FLAGS_INIT
    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
)
