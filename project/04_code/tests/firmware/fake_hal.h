/* Fake fw_hal for the host tests: pulse engine counted by hand, STOP/START through the real fw_core state machines. */
#pragma once
#include "fw_hal.h"
#include "fw_stop.h"

typedef struct {
    bool ready, active, finished, ended;
    uint32_t count, target, freq, first_hz, starts, stops, ends;
    bool dir_high; bool fail_set_freq;
} fake_pulse_t;
extern fake_pulse_t FP[2];
extern bool FAKE_SERVO[2];
extern uint16_t FAKE_USER_DO;
extern bool FAKE_OE, FAKE_IO_READY;
extern bool FAKE_STOP_IN[2];
extern fw_es_t FAKE_ES[2];
extern int FAKE_START_CB_N[2];
extern uint32_t FAKE_NOW;
extern bool FAKE_RELEASE_FAIL, FAKE_NO_ISR, FAKE_FAULT_BOUND[2];   /* recover_from_fault fails / edge ISR missed / MCPWM fault bound */
extern int FAKE_RELEASES;
void fake_reset(void);
void fake_pulses(uint8_t axis, uint32_t n);               /* the engine emitted n pulses (finishes at the target) */
void fake_stop_isr(uint8_t axis);                         /* STOP input opens: what the ISR does */
void fake_io_ms(uint8_t axis, unsigned ms, bool stop_in, bool start_raw);   /* run the 1 ms watch task */
