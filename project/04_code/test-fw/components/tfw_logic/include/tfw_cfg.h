/* N-01: ESP-IDF sdkconfig.h does NOT define a bool option that is "n". Using CONFIG_X inside a C expression then fails
 * to compile. Only these helper macros (always defined, 0/1) may be used in C expressions; CONFIG_ bool names are
 * allowed only in #if / #ifdef lines. Int options are always defined and may be used directly. */
#ifndef TFW_CFG_H
#define TFW_CFG_H

#include "sdkconfig.h"   /* must come first: the macros below are evaluated where they are defined */

#if defined(CONFIG_TFW_ESTOP_ACTIVE_LOW) && CONFIG_TFW_ESTOP_ACTIVE_LOW
#define TFW_B_ESTOP_ACTIVE_LOW 1
#else
#define TFW_B_ESTOP_ACTIVE_LOW 0
#endif

#if defined(CONFIG_TFW_START_ACTIVE_LOW) && CONFIG_TFW_START_ACTIVE_LOW
#define TFW_B_START_ACTIVE_LOW 1
#else
#define TFW_B_START_ACTIVE_LOW 0
#endif

#if defined(CONFIG_TFW_PULSE_OE_ACTIVE_LOW) && CONFIG_TFW_PULSE_OE_ACTIVE_LOW
#define TFW_B_PULSE_OE_ACTIVE_LOW 1
#else
#define TFW_B_PULSE_OE_ACTIVE_LOW 0
#endif

#if defined(CONFIG_TFW_AX2_IO_PRESENT) && CONFIG_TFW_AX2_IO_PRESENT
#define TFW_B_AX2_IO_PRESENT 1
#else
#define TFW_B_AX2_IO_PRESENT 0
#endif

#if defined(CONFIG_TFW_UPD_MODE_HWTIMER) && CONFIG_TFW_UPD_MODE_HWTIMER
#define TFW_B_UPD_MODE_HWTIMER 1
#else
#define TFW_B_UPD_MODE_HWTIMER 0
#endif

#if defined(CONFIG_TFW_LEGACY_NVS_ON_WRITE) && CONFIG_TFW_LEGACY_NVS_ON_WRITE
#define TFW_B_LEGACY_NVS_ON_WRITE 1
#else
#define TFW_B_LEGACY_NVS_ON_WRITE 0
#endif

#if defined(CONFIG_PCNT_ISR_IRAM_SAFE) && CONFIG_PCNT_ISR_IRAM_SAFE
#define TFW_B_PCNT_ISR_IRAM_SAFE 1
#else
#define TFW_B_PCNT_ISR_IRAM_SAFE 0
#endif

#endif
