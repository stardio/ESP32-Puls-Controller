# HAL: the interface header (fw_hal.h) is hardware independent; the implementation lives in port_esp32p4/.
idf_component_register(
    SRCS "port_esp32p4/hal_pulse.c" "port_esp32p4/hal_io.c" "port_esp32p4/hal_sys.c" "port_esp32p4/hal_store.c"
    INCLUDE_DIRS "include" "port_esp32p4"
    REQUIRES fw_core driver esp_driver_mcpwm esp_driver_pcnt esp_driver_gpio esp_timer esp_hw_support esp_system
             esp_rom freertos nvs_flash
)
