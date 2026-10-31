# Pure logic: no ESP-IDF headers. Built with gcc on the host for the unit tests (tests/firmware).
idf_component_register(
    SRCS "fw_profile.c" "fw_stop.c" "fw_params.c" "fw_regmap.c"
    INCLUDE_DIRS "include"
)
# -fno-math-errno: __builtin_sqrtf must not pull in a flash-resident errno path (test-fw review m-07)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-math-errno)
