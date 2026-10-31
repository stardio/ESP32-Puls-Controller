idf_component_register(
    SRCS "fw_modbus.c" "fw_net.c" "fw_mb_tcp.c" "fw_mb_rtu.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES fw_core fw_hal fw_motion esp_eth esp_netif esp_event esp_driver_uart esp_driver_gpio lwip freertos log
)
