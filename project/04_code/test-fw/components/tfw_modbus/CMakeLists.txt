idf_component_register(
    SRCS "tfw_modbus.c"
    INCLUDE_DIRS "include"
    REQUIRES tfw_logic tfw_pulse2 tfw_estop_io driver esp_driver_gpio esp_eth esp_netif esp_event esp_timer lwip nvs_flash freertos
)
