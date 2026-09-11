#ifndef NINLIL_FEEDBACK_PUMP_H
#define NINLIL_FEEDBACK_PUMP_H
#include "ninlil_network_pump.h"
/* Internal port helpers; caller is the existing exclusive network pump. */
int ninlil_esp_feedback_prepare(ninlil_esp_network_pump *pump,
                                const ninlil_airtime_job *job, uint64_t now_us);
int ninlil_esp_feedback_complete(ninlil_esp_network_pump *pump,
                                 const ninlil_airtime_job *job,
                                 int driver_result, uint64_t now_ms);
#endif
