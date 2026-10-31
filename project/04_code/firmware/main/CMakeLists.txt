idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES fw_core fw_hal fw_motion fw_modbus freertos log
)
