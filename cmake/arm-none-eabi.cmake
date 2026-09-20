set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_GCC_BIN "" CACHE PATH "Directory containing arm-none-eabi-gcc")

find_program(ARM_GCC
    NAMES arm-none-eabi-gcc
    HINTS "${ARM_GCC_BIN}"
    REQUIRED
)

find_program(ARM_OBJCOPY
    NAMES arm-none-eabi-objcopy
    HINTS "${ARM_GCC_BIN}"
    REQUIRED
)

find_program(ARM_SIZE
    NAMES arm-none-eabi-size
    HINTS "${ARM_GCC_BIN}"
    REQUIRED
)

set(CMAKE_C_COMPILER "${ARM_GCC}")
set(CMAKE_ASM_COMPILER "${ARM_GCC}")
set(CMAKE_OBJCOPY "${ARM_OBJCOPY}")
set(CMAKE_SIZE "${ARM_SIZE}")

set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
)

set(CMAKE_ASM_FLAGS_INIT
    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
)