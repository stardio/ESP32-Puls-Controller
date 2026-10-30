/* TFW test register window (TFW-01 sec.3, TFW-03 sec.3, TFW-04 sec.3).
 * Holding registers, 32-bit values = low word first.
 * This header is also parsed by the host test (tests/test-fw/test_regs_sync.py)
 * so the Python tool and the firmware cannot drift apart. */
#pragma once

#define TFW_WIN_BASE          0x7000
#define TFW_WIN_END           0x70FF   /* inclusive */

/* inputs (R/W) */
#define TFW_R_CMD             0x7000   /* U16, write executes, read 0 */
#define TFW_R_AXIS_MASK       0x7001   /* U16, b0 ax1, b1 ax2 */
#define TFW_R_FREQ            0x7002   /* U32 Hz 100..1000000 */
#define TFW_R_COUNT           0x7004   /* U32 pulses 1..4294967295 */
#define TFW_R_ACCEL_MS        0x7006   /* U16 10..60000 */
#define TFW_R_DECEL_MS        0x7007   /* U16 10..60000 */
#define TFW_R_REPEAT          0x7008   /* U32 1..1000000 */
#define TFW_R_SEED            0x700A   /* U32 */
#define TFW_R_LOAD_NVS_MS     0x700C   /* U16 0 or 10..10000 */
#define TFW_R_LOAD_CPU_PCT    0x700D   /* U16 0..90 */
#define TFW_R_CFG_UPD_US      0x700E   /* U16 500..10000 (applied at next boot) */
#define TFW_R_CFG_RES_HZ      0x7010   /* U32 2M/10M/20M/40M (applied at next boot) */
#define TFW_R_CFG_FLAGS       0x7012   /* BITS16 */

/* outputs (R) */
#define TFW_R_SEQ_DONE        0x7020   /* U32 */
#define TFW_R_ERR_AX1         0x7022   /* U32 */
#define TFW_R_ERR_AX2         0x7024   /* U32 */
#define TFW_R_OVS_AX1         0x7026   /* U32 */
#define TFW_R_OVS_AX2         0x7028   /* U32 */
#define TFW_R_UPD_LATE        0x702A   /* U32 */
#define TFW_R_UPD_MAX_US      0x702C   /* U32 */
#define TFW_R_ISR_LAT_AX1     0x702E   /* U32 ns */
#define TFW_R_ISR_LAT_AX2     0x7030   /* U32 ns */
#define TFW_R_NVS_WRITES      0x7032   /* U32 */
#define TFW_R_NVS_MAX_MS      0x7034   /* U32 */
#define TFW_R_CHIP_REV        0x7036   /* U16 */
#define TFW_R_CPU_MHZ         0x7037   /* U16 */
#define TFW_R_STATUS          0x7038   /* U16 */
#define TFW_R_LAST_TGT_AX1    0x7040   /* U32 */
#define TFW_R_LAST_CNT_AX1    0x7042   /* U32 */
#define TFW_R_LAST_TGT_AX2    0x7044   /* U32 */
#define TFW_R_LAST_CNT_AX2    0x7046   /* U32 */
/* TFW-03 */
#define TFW_R_MB_CLIENTS      0x7050   /* U16 */
#define TFW_R_MB_LINK_DOWN    0x7051   /* U16 */
#define TFW_R_MB_REQ_COUNT    0x7052   /* U32 */
#define TFW_R_MB_PROC_MAX_US  0x7054   /* U32 */
#define TFW_R_MB_HIST         0x7056   /* U16 x8, 0x7056..0x705D */
#define TFW_R_MB_MAX_CLIENTS  0x705E   /* U16 R: build option TFW_MAX_CLIENTS (C-03, added in review round 1) */
/* TFW-04 */
#define TFW_R_ES_PATH_SEL     0x7060   /* U16 W: 1 A, 2 B, 3 A+B */
#define TFW_R_ES_RELEASE      0x7061   /* U16 W: 1 = release */
#define TFW_R_ES_STATE        0x7062   /* U16 R */
#define TFW_R_ES_TRIP_COUNT   0x7063   /* U16 R */
#define TFW_R_ES_ISR_LAT      0x7064   /* U32 R ns, 0xFFFFFFFF = not measured internally */
#define TFW_R_ES_PULSES_AX1   0x7066   /* U32 R */
#define TFW_R_ES_PULSES_AX2   0x7068   /* U32 R */
#define TFW_R_ES_BIND_OK      0x706A   /* U16 R */
#define TFW_R_ES_BOOT_LEVEL   0x706B   /* U16 R */
#define TFW_R_ES_LAST_ERR     0x706C   /* U16 R */

/* tst_cmd values */
#define TFW_CMD_RUN_FIXED     1
#define TFW_CMD_RUN_MOVE      2
#define TFW_CMD_RUN_SEQ       3
#define TFW_CMD_STOP_ALL      4
#define TFW_CMD_CLEAR_STATS   5
#define TFW_CMD_SET_LOAD      6

/* tst_status values */
#define TFW_ST_IDLE           0
#define TFW_ST_RUNNING        1
#define TFW_ST_DONE           2
#define TFW_ST_REJECT_BUSY    0x10
#define TFW_ST_BAD_VALUE      0x11
#define TFW_ST_NO_RESOURCE    0x12

/* cfg_flags bits */
#define TFW_CFG_PCNT_IRAM     (1u << 0)  /* read-only: build option */
#define TFW_CFG_CORE_B        (1u << 1)  /* 0 = layout A, 1 = layout B */
#define TFW_CFG_BRAKE_USE     (1u << 2)
#define TFW_CFG_ETM_EXP       (1u << 3)
#define TFW_CFG_WRITABLE_MASK (TFW_CFG_CORE_B | TFW_CFG_BRAKE_USE | TFW_CFG_ETM_EXP)
