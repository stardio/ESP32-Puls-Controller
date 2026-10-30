idf_component_register(
    SRCS "tfw_pulse2.c"
    INCLUDE_DIRS "include"
    REQUIRES tfw_logic driver esp_driver_mcpwm esp_driver_pcnt esp_driver_gptimer esp_driver_gpio esp_timer esp_hw_support freertos
)
