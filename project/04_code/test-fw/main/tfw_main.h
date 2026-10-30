/* TFW-01 tfw_main (G3 pre-test firmware, D-025). */
#pragma once
#include <stdint.h>
#include "tfw_logic.h"

void     app_main(void);
uint16_t tfw_handle_cmd(uint16_t cmd);          /* called from TFW-03 register write (via on_effect hook) */
void     tfw_get_results(tfw_results_t *out);   /* snapshot for TFW-03 reads */
