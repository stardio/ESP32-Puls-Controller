idf_component_register(
    SRCS "tfw_estop_io.c"
    INCLUDE_DIRS "include"
    REQUIRES tfw_logic tfw_pulse2 driver esp_driver_gpio esp_driver_mcpwm esp_timer freertos
)
