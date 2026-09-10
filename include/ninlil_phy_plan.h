#ifndef NINLIL_PHY_PLAN_H
#define NINLIL_PHY_PLAN_H
#include "ninlil.h"
#define NINLIL_PHY_PROFILES 8u
#define NINLIL_PHY_SLOTS 128u
#define NINLIL_PHY_FRAGMENT_HEADER 96u
#define NINLIL_PHY_FRAGMENT_PAYLOAD 88u
#define NINLIL_PHY_FRAGMENTS 16u
#define NINLIL_PHY_PLAN_BYTES 1408u
/* Reserved only by this experimental codec. The current node dispatcher does
 * NOT enable it; no new format is sent or applied implicitly. */
#define NINLIL_PHY_PLAN_DISPATCH 32u

typedef struct ninlil_phy_profile {
    uint32_t id, version, max_airtime_us, post_tx_pause_us, retune_us;
    uint16_t mtu;
    uint8_t approved;
} ninlil_phy_profile;
typedef struct ninlil_phy_slot {
    uint64_t start_us;
    uint32_t duration_us, airtime_us, guard_us, commit_us, wake_us, profile;
    uint16_t tx, rx, conflict_domain;
    uint8_t tx_radio, rx_radio, recovery;
} ninlil_phy_slot;
/* A zero conflict domain is uncalibrated and conflicts with every domain.
 * Different nonzero domains assert measured independent resources; different
 * SF/channel alone is NOT sufficient. Recovery slots reserve the entire shared
 * schedule. Profiles must already be approved externally; the driver remains
 * the final legal-RF gate. This validator never touches a radio or grants a
 * lease. */
int ninlil_phy_schedule_validate(const ninlil_phy_profile *profiles,
                                 size_t profile_count,
                                 const ninlil_phy_slot *slots, size_t count,
                                 uint64_t period_us, uint32_t clock_error_us,
                                 uint32_t recovery_profile);
/* Lease-clock times, never the application's UTC deadline. The owner must
 * validate Root authority, epochs and clock bounds separately before
 * activation. Encoder/decoder buffers are borrowed for the call; reassembly
 * input must not alias its mutable state. Successful decoding or assembly
 * grants no TX right. */
typedef struct ninlil_phy_fragment {
    uint8_t operation[16], digest[32];
    uint64_t authority_epoch, plan_epoch, activate_ms, expires_ms;
    uint32_t profile, profile_version;
    uint8_t index, count;
    uint16_t length;
    uint8_t payload[NINLIL_PHY_FRAGMENT_PAYLOAD];
} ninlil_phy_fragment;
int ninlil_phy_fragment_encode(const ninlil_phy_fragment *fragment,
                               uint8_t *output, size_t capacity,
                               size_t *written);
int ninlil_phy_fragment_decode(const uint8_t *input, size_t length,
                               ninlil_phy_fragment *fragment);
typedef int (*ninlil_phy_digest_verify)(void *ctx, const uint8_t *data,
                                        size_t length,
                                        const uint8_t digest[32]);
typedef struct ninlil_phy_reassembly {
    ninlil_phy_fragment binding;
    uint8_t bytes[NINLIL_PHY_PLAN_BYTES];
    uint64_t deadline_ms, now_ms;
    uint16_t peer, mask, length;
    uint8_t active, ready, poisoned;
} ninlil_phy_reassembly;
/* One caller-owned, authenticated-peer slot. The owner must enforce the global
 * limit (four slots) and airtime quotas before calling. Not for EDHOC
 * bootstrap. Digest callback verifies canonical SHA-256 with a reviewed crypto
 * backend. Full reassembly is only a proposal; authority/lease/apply proofs
 * remain gates. Deadline is fixed on first fragment and never renewed by
 * duplicates. */
void ninlil_phy_reassembly_open(ninlil_phy_reassembly *state);
int ninlil_phy_reassembly_push(ninlil_phy_reassembly *state,
                               uint16_t verified_peer, const uint8_t *frame,
                               size_t length, uint64_t now_ms,
                               ninlil_time_quality time_quality,
                               ninlil_phy_digest_verify verify,
                               void *verify_ctx);
#endif
