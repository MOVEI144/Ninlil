#ifndef NINLIL_POWER_POLICY_H
#define NINLIL_POWER_POLICY_H
#include "ninlil_link_metrics.h"
/* Optional policy v2, independent of the legacy rolling-window policy.
 * The caller must start the radio at the approved maximum before open.
 * A callback OK must mean the actual driver applied the requested setting.
 * Any ambiguous driver error poisons this owner; read/reinitialize the physical
 * radio before reopening. No old measurement is accepted after a setting change.
 */
typedef int (*ninlil_power_apply)(void *ctx, int8_t power_dbm);
typedef struct ninlil_power_policy {
    ninlil_link_context context;
    ninlil_link_window last;
    uint64_t now_ms, changed_ms, supported_ms;
    int8_t minimum_dbm, maximum_dbm;
    uint8_t good, opened, poisoned;
} ninlil_power_policy;
int ninlil_power_policy_open(ninlil_power_policy *policy,
                              const ninlil_link_context *context,
                              int8_t minimum_dbm, int8_t maximum_dbm,
                              uint64_t now_ms);
/* NULL window means no new closed evidence, not a lost RF attempt. An exact
 * duplicate never counts twice. Three disjoint perfect windows allow -3 dB;
 * <=6/8 or evidence absent for 120 s restores the approved maximum. */
int ninlil_power_policy_step(ninlil_power_policy *policy,
                              const ninlil_link_window *window,
                              uint64_t now_ms, ninlil_power_apply apply,
                              void *apply_ctx);
/* Pure proposal for staged drivers. No driver call, generation publication or
 * mutation of policy. out must not alias policy and is unchanged on error.
 * The owner may publish this proposal only after actual setting confirmation;
 * discarded proposals never count another successful observation window. */
int ninlil_power_policy_plan(const ninlil_power_policy *policy,
                              const ninlil_link_window *window, uint64_t now_ms,
                              ninlil_power_policy *out);
#endif
