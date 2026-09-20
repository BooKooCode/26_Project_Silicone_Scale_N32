cmake_minimum_required(VERSION 3.22)

find_package(Git REQUIRED)
find_program(VERSION_POWERSHELL NAMES pwsh powershell REQUIRED)

set(VERSION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/auto_version.c")

add_custom_target(version_metadata
    COMMAND "${VERSION_POWERSHELL}" -NoProfile -NonInteractive
        -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/generate-version.ps1"
        -WorkspaceRoot "${PROJECT_SOURCE_DIR}"
        -OutputFile "${VERSION_SOURCE}"
        -GitExe "${GIT_EXECUTABLE}"
    BYPRODUCTS "${VERSION_SOURCE}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/generate-version.ps1"
        "${CMAKE_CURRENT_SOURCE_DIR}/auto_version.h"
    VERBATIM
)

add_library(VersionScript STATIC "${VERSION_SOURCE}")
add_dependencies(VersionScript version_metadata)
target_include_directories(VersionScript PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(VersionScript PRIVATE n32_platform)