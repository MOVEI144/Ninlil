#ifndef NINLIL_ENROLLMENT_H
#define NINLIL_ENROLLMENT_H
#include "ninlil_node.h"

/* Volatile diagnostic counters after control decryption, before/after handling.
 * No payloads, keys or delivery claims. Counters saturate and reset on open. */
typedef struct ninlil_control_counter {
    uint16_t received, accepted;
    int last_result;
} ninlil_control_counter;
int ninlil_node_control_inspect(const ninlil_node *node, uint8_t kind,
                                ninlil_control_counter *out);

#define NINLIL_MEMBER_RECORD_MAX 224u
#define NINLIL_ADMISSION_MAX (NINLIL_MEMBER_RECORD_MAX + 75u)
/* Canonical public configuration, never a proof of identity or authority. */
size_t ninlil_member_encode(const ninlil_node_member *member, uint8_t *out,
                            size_t capacity);
int ninlil_member_decode(const uint8_t *data, size_t length,
                         ninlil_node_member *member);
/* Exclusive owner call. The embedding application must authorize the change
 * through its trusted local management boundary, or verify a signed credential.
 * Never call directly with an unauthenticated RF/USB request. Persists before
 * use; an exact duplicate is read-only. Existing identity/address are stable;
 * replacement requires a newer membership epoch and nondecreasing binding.
 * Cannot replace self/Root; never grants a live session or bypasses Join.
 * Capacity is finite and removal/revocation does not silently erase history. */
int ninlil_node_enroll(ninlil_node *node, const ninlil_node_member *member);
/* Migration checkpoint before replacing a compiled roster with local settings.
 * Retains peer trust and all custody through the existing collection path. */
int ninlil_node_preserve_members(ninlil_node *node);
/* A snapshot of public configuration; it does not prove current membership. */
int ninlil_node_member_inspect(ninlil_node *node, uint16_t address,
                               ninlil_node_member *member);
/* COSE_Sign1/ES256 credential profile. Signed NM1 binds public key, identity,
 * authority, epochs and permissions. No bearer secret or expiry claim; a
 * credential establishes a candidate, never fresh membership or a live route.
 * Issuer persists local authorization before publishing. Verification uses
 * only the explicitly configured Root key, followed by ordinary EDHOC/Join. */
int ninlil_node_authorize(ninlil_node *node, const ninlil_node_member *member,
                          uint8_t *credential, size_t capacity, size_t *length);
int ninlil_node_admit(ninlil_node *node, const uint8_t *credential,
                      size_t length);
int ninlil_admission_verify(const uint8_t root_key[65],
                            const uint8_t *credential, size_t length,
                            ninlil_node_member *member);
/* Copies this node's signed credential for bounded radio announcements.
 * Caller persists/reloads the deployment settings; normal Join still applies.
 */
int ninlil_node_advertise(ninlil_node *node, const uint8_t *credential,
                          size_t length);
#endif
