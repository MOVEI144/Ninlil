#ifndef NINLIL_LEASE_CLOCK_H
#define NINLIL_LEASE_CLOCK_H

#include "ninlil_network.h"
#include "ninlil_security_state.h"

#define NINLIL_LEASE_MAX_MS NINLIL_NETWORK_LEASE_MAX_MS
#define NINLIL_LEASE_REBOOT_WAIT_MS 61000u
#define NINLIL_LEASE_SYNC_MAX_AGE_MS 30000u
#define NINLIL_LEASE_SYNC_MAX_RTT_MS 10000u
#define NINLIL_LEASE_SYNC_ERROR_BOUND_MS                                       \
    (NINLIL_LEASE_SYNC_MAX_RTT_MS +                                            \
     (NINLIL_LEASE_SYNC_MAX_AGE_MS + NINLIL_LEASE_SYNC_MAX_RTT_MS) * 3u /      \
         1000u +                                                               \
     2u)

typedef struct ninlil_lease_clock {
    uint64_t began_ms;
    uint64_t stamp;
    uint64_t request_ms;
    uint64_t anchor_ms;
    uint64_t challenge;
    uint64_t last_ms;
    uint64_t last_local_ms;
    uint8_t root;
    uint8_t synchronized;
} ninlil_lease_clock;

/* A dedicated durable counter reserves the coordinator's monotonically
 * increasing boot era before any plan can become effective. Never reset this
 * counter for a deployed network. Its next value must be < UINT32_MAX - 1.
 * Root waits 61 seconds before granting leases of at most 60 seconds. This
 * profile requires awake monotonic clocks within 1000 ppm of real time;
 * suspension/deep sleep invalidates synchronization. It is NOT a UTC clock.
 * The 10-second exchange / 30-second refresh age accommodates bounded LoRa
 * multi-hop queues. The full exchange delay is added to the upper clock
 * estimate; delayed synchronization can shorten leases, never extend them.
 * At 49-day era exhaustion, reopen with the same counter and wait again. */
int ninlil_lease_root_open(ninlil_lease_clock *clock,
                           ninlil_counter_store *eras, uint64_t monotonic_ms);
void ninlil_lease_peer_open(ninlil_lease_clock *clock, uint64_t monotonic_ms);
/* Nonzero challenge comes from the authenticated owner's cryptographic RNG.
 * Only one outstanding exchange. A retry keeps its original challenge/time;
 * replacing it explicitly invalidates any older reply. */
int ninlil_lease_request(ninlil_lease_clock *clock, uint64_t challenge,
                         uint64_t monotonic_ms);
/* Call only for an authenticated response from the configured coordinator in
 * the current EDHOC session. Delayed replies advance time; never extend leases.
 * Invalidate on session replacement or loss of the awake clock. */
int ninlil_lease_accept(ninlil_lease_clock *clock, uint64_t challenge,
                        uint64_t root_stamp, uint64_t monotonic_ms);
void ninlil_lease_invalidate(ninlil_lease_clock *clock);
int ninlil_lease_now(ninlil_lease_clock *clock, uint64_t monotonic_ms,
                     uint64_t *lease_ms);

#endif
