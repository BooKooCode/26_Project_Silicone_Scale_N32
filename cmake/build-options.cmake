include_guard(GLOBAL)

set(CMAKE_C_FLAGS_DEBUG "-Og -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g0")

set(N32_RELEASE_SIZE_OPTIMIZED_TARGETS
    ext_store
    sfud
    sfud_mgnt
    log
    rtt
    rtt_mass_telemetry
    VersionScript
)

add_compile_options(
    "$<$<AND:$<CONFIG:Release>,$<IN_LIST:$<TARGET_PROPERTY:NAME>,${N32_RELEASE_SIZE_OPTIMIZED_TARGETS}>>:-Os>"
)