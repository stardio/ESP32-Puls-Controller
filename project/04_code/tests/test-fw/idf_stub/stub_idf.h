/* Minimal stand-ins for the ESP-IDF API used by tfw_main.c and tfw_estop_io.c, for `gcc -fsyntax-only` ONLY.
 * Signatures are from memory of ESP-IDF 5.x and are NOT checked against the real headers. What this proves: the files
 * compile for each sdkconfig combination of bool options (N-01); it proves nothing about API correctness. */
#ifndef STUB_IDF_H
#define STUB_IDF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define IRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x111
const char *esp_err_to_name(esp_err_t e);
#define ESP_ERROR_CHECK(x) do { esp_err_t _x = (x); (void)_x; } while (0)
#define ESP_LOGE(tag, ...) printf(__VA_ARGS__)
#define ESP_LOGW(tag, ...) printf(__VA_ARGS__)
#define ESP_LOGI(tag, ...) printf(__VA_ARGS__)
uint32_t esp_cpu_get_cycle_count(void);
int esp_cpu_get_core_id(void);
int64_t esp_timer_get_time(void);
/* chip / clock */
typedef struct { int model; int cores; uint16_t revision; } esp_chip_info_t;
void esp_chip_info(esp_chip_info_t *i);
#define SOC_MOD_CLK_CPU 1
#define ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED 0
esp_err_t esp_clk_tree_src_get_freq_hz(int src, int prec, uint32_t *hz);
/* gpio */
typedef int gpio_num_t;
typedef struct { uint64_t pin_bit_mask; int mode; int pull_up_en, pull_down_en, intr_type; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define ESP_INTR_FLAG_LEVEL1 2
#define ESP_INTR_FLAG_LEVEL2 4
#define ESP_INTR_FLAG_LEVEL3 8
#define ESP_INTR_FLAG_IRAM 0x400
esp_err_t gpio_config(const gpio_config_t *c);
int gpio_get_level(gpio_num_t g);
esp_err_t gpio_set_level(gpio_num_t g, uint32_t v);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t g, void (*fn)(void *), void *arg);
/* mcpwm (types only as far as the two files use them) */
typedef struct mcpwm_oper_t *mcpwm_oper_handle_t;
typedef struct mcpwm_gen_t *mcpwm_gen_handle_t;
typedef struct mcpwm_fault_t *mcpwm_fault_handle_t;
typedef struct { int group_id; int gpio_num; int intr_priority; struct { unsigned active_level:1, pull_up:1, pull_down:1; } flags; } mcpwm_gpio_fault_config_t;
#define MCPWM_TIMER_DIRECTION_UP 0
#define MCPWM_OPER_BRAKE_MODE_OST 1
#define MCPWM_GEN_ACTION_LOW 0
#define MCPWM_GEN_ACTION_KEEP 1
typedef struct { int direction, brake_mode, action; } mcpwm_gen_brake_event_action_t;
#define MCPWM_GEN_BRAKE_EVENT_ACTION(dir, mode, act) ((mcpwm_gen_brake_event_action_t){dir, mode, act})
typedef struct { int group_id; int intr_priority; } mcpwm_operator_config_t;
typedef struct { mcpwm_fault_handle_t fault; int brake_mode; } mcpwm_brake_config_t;
esp_err_t mcpwm_new_gpio_fault(const mcpwm_gpio_fault_config_t *c, mcpwm_fault_handle_t *f);
esp_err_t mcpwm_operator_set_brake_on_fault(mcpwm_oper_handle_t o, const mcpwm_brake_config_t *b);
esp_err_t mcpwm_operator_recover_from_fault(mcpwm_oper_handle_t o, mcpwm_fault_handle_t f);
esp_err_t mcpwm_generator_set_action_on_brake_event(mcpwm_gen_handle_t g, mcpwm_gen_brake_event_action_t a);
/* nvs */
typedef uint32_t nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *v, size_t *n);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t n);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
/* FreeRTOS */
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFu
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
BaseType_t xTaskCreate(void (*fn)(void *), const char *n, uint32_t st, void *a, int prio, TaskHandle_t *h);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *n, uint32_t st, void *a, int prio, TaskHandle_t *h, int core);
void vTaskDelete(TaskHandle_t h);
void vTaskDelay(TickType_t t);
void vTaskDelayUntil(TickType_t *last, TickType_t t);
TickType_t xTaskGetTickCount(void);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t t);
BaseType_t xTaskNotifyGive(TaskHandle_t h);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);

/* T-15 (D-070) */
#define RTC_NOINIT_ATTR
typedef enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON = 1, ESP_RST_INT_WDT = 6, ESP_RST_TASK_WDT = 7 } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason(void);
#define portDISABLE_INTERRUPTS() do { } while (0)
#define configMAX_PRIORITIES 25
#endif
