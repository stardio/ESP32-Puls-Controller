/* Maps sdkconfig (Kconfig) values to always-defined 0/1 macros. ESP-IDF's sdkconfig.h does NOT define a bool option
 * that is "n"; CONFIG_ bool names may therefore appear only in #if / #ifdef lines (test-fw review N-01). Int options
 * are always defined and may be used directly. */
#ifndef FW_CFG_H
#define FW_CFG_H
#include "sdkconfig.h"      /* must come first */

#if defined(CONFIG_FW_MODEL_RTU) && CONFIG_FW_MODEL_RTU
#define FW_B_MODEL_RTU 1
#else
#define FW_B_MODEL_RTU 0
#endif
#if defined(CONFIG_FW_STOP_ACTIVE_LOW) && CONFIG_FW_STOP_ACTIVE_LOW
#define FW_B_STOP_ACTIVE_LOW 1
#else
#define FW_B_STOP_ACTIVE_LOW 0
#endif
#if defined(CONFIG_FW_START_ACTIVE_LOW) && CONFIG_FW_START_ACTIVE_LOW
#define FW_B_START_ACTIVE_LOW 1
#else
#define FW_B_START_ACTIVE_LOW 0
#endif
#if defined(CONFIG_FW_OE_ACTIVE_LOW) && CONFIG_FW_OE_ACTIVE_LOW
#define FW_B_OE_ACTIVE_LOW 1
#else
#define FW_B_OE_ACTIVE_LOW 0
#endif
#if defined(CONFIG_PCNT_ISR_IRAM_SAFE) && CONFIG_PCNT_ISR_IRAM_SAFE
#define FW_B_PCNT_ISR_IRAM_SAFE 1
#else
#define FW_B_PCNT_ISR_IRAM_SAFE 0
#endif

#if defined(CONFIG_FW_PERIOD_IMMEDIATE) && CONFIG_FW_PERIOD_IMMEDIATE
#define FW_B_PERIOD_IMMEDIATE 1
#else
#define FW_B_PERIOD_IMMEDIATE 0
#endif

#define FW_AXIS_COUNT CONFIG_FW_AXIS_COUNT          /* int, always defined */
#if FW_AXIS_COUNT < 1 || FW_AXIS_COUNT > 2
#error "CONFIG_FW_AXIS_COUNT must be 1 or 2"
#endif
#endif
