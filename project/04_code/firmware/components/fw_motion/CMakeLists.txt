idf_component_register(
    SRCS "fw_axis.c" "fw_motion.c"
    INCLUDE_DIRS "include"
    REQUIRES fw_core fw_hal freertos esp_timer log
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-math-errno)
