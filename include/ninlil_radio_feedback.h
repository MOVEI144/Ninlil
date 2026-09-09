#ifndef NINLIL_RADIO_FEEDBACK_H
#define NINLIL_RADIO_FEEDBACK_H
#include "ninlil_power_policy.h"

#define NINLIL_FEEDBACK_PEERS 16u

typedef struct ninlil_feedback_peer {
    ninlil_link_metrics metrics;
    ninlil_power_policy policy;
    uint64_t generation;
    uint16_t address;
    uint8_t confirmed;
} ninlil_feedback_peer;

typedef struct ninlil_radio_feedback {
    ninlil_feedback_peer peers[NINLIL_FEEDBACK_PEERS];
    ninlil_power_policy proposed;
    uint64_t now_ms, began_ms;
    int8_t minimum_dbm, maximum_dbm;
    uint8_t opened, pending, slot;
    int fault;
} ninlil_radio_feedback;

/* One radio/execution owner. No allocation, IO, radio calls, persistence or
 * delivery outcomes. A slot is not reused for another address during this open.
 * Contexts contain public fingerprints only. Bounds come from an approved RF
 * profile; opening this owner does not authorize transmission. */
int ninlil_radio_feedback_open(ninlil_radio_feedback *owner,
                                int8_t minimum_dbm, int8_t maximum_dbm);
/* Called AFTER current session/route/frame authorization, immediately before
 * staging the physical setting. The caller must not change profile within this
 * lifetime; changed profiles/sessions invalidate peer measurements.
 * Only a proposal: OK does NOT mean that the driver applied the output setting.
 * There is exactly one pending radio job. No low-power selection for bootstrap
 * floods: the adapter sends those at its approved maximum without begin(). */
int ninlil_radio_feedback_begin(ninlil_radio_feedback *owner, uint16_t peer,
                                 uint32_t profile, const uint8_t session[16],
                                 uint64_t now_ms, ninlil_link_context *proposed);
/* Finish once for the corresponding synchronous driver call. BUSY aborts the
 * proposal with no RF trial. IO/TIMEOUT or a wrong applied output poison this
 * owner. Reinitialize the radio and reopen before continuing.
 * Only TX_DONE plus matching applied_power can confirm a setting generation.
 * A nonzero token is an authenticated outgoing full-size neighbor probe;
 * zero marks ordinary traffic, which never becomes a probe observation.
 * queue_us measures enqueue-to-driver wait, not CCA/flash/total latency.
 * UINT32_MAX means unavailable/out-of-range queue evidence: do not sample it. */
int ninlil_radio_feedback_finish(ninlil_radio_feedback *owner, int driver_result,
                                  int8_t applied_power_dbm, uint64_t now_ms,
                                  uint64_t probe_token, uint32_t airtime_us,
                                  uint32_t queue_us);
/* The node invokes this only AFTER current neighbor authentication and exact
 * challenge validation. Wrong/late/old-session replies never affect policy. */
int ninlil_radio_feedback_reply(ninlil_radio_feedback *owner, uint16_t peer,
                                 const uint8_t session[16], uint64_t token,
                                 uint64_t now_ms);
/* Sleep/receiver suspension cancels unfinished probes without RF loss. Zero
 * peer invalidates all slots. No physical effect; next direct TX starts at max.
 * Retains sequence/generation fences within this owner; never refunds custody. */
int ninlil_radio_feedback_invalidate(ninlil_radio_feedback *owner,
                                      uint16_t peer, uint64_t now_ms);
/* Read-only finalized window; no window means EMPTY, not zero loss. */
int ninlil_radio_feedback_read(const ninlil_radio_feedback *owner,
                                uint16_t peer, uint64_t now_ms,
                                ninlil_link_window *window);
#endif
