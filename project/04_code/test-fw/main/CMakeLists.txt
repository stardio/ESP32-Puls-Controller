idf_component_register(
    SRCS "tfw_main.c"
    INCLUDE_DIRS "."
    REQUIRES tfw_logic tfw_pulse2 tfw_modbus tfw_estop_io nvs_flash esp_system esp_timer esp_hw_support esp_driver_gpio freertos
)
