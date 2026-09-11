#ifndef NINLIL_BINDING_H
#define NINLIL_BINDING_H
#include "ninlil.h"

#define NINLIL_BINDING_API_VERSION 1u

int ninlil_delivery_binding_valid(const ninlil_delivery_binding *binding);
int ninlil_delivery_binding_equal(const ninlil_delivery_binding *left,
                                  const ninlil_delivery_binding *right);
/* Same-owner, no allocation, journal mutation or effects. Checks the current
 * authenticated lookup and the Core's persisted source identity. It never
 * replaces a missing identity with a matching address. */
int ninlil_binding_current(ninlil_runtime *runtime, uint16_t peer,
                           const ninlil_delivery_binding *expected);
/* Durable-only, uses the original submission contract and message wire format.
 * Requires a source-bound Core store. Saves a typed binding before the create
 * record; only the latter admits ownership. An interrupted prefix cannot send.
 * Exact duplicate key+contract+binding returns the same ID, including offline
 * recovery. Legacy submission of a bound key is a conflict, not a downgrade.
 * No RF or Link call. No per-packet allocation. All consumers must recompile.
 * Persistent addition: types 11/12, bound OUT_CREATE v6. Legacy v5 stays v5;
 * older binaries reject bound journals and must not open them after rollback.
 */
int ninlil_submit_bound(ninlil_runtime *runtime,
                        const ninlil_submission *submission,
                        const ninlil_delivery_binding *binding,
                        ninlil_id *message_id);
/* Reads and validates the saved binding, not the current peer. Historical
 * terminal results remain queryable after revocation. NOT_FOUND for an unbound
 * message. Outputs unchanged on error. Corruption poisons the Core owner. */
int ninlil_query_binding(ninlil_runtime *runtime, const ninlil_id *message_id,
                         ninlil_delivery_binding *binding);
/* Explicit handoff of terminal deduplication retention, not cancellation.
 * Call ONLY after the external owner durably saved this exact terminal result
 * and will never resubmit its key. Until then the bound archive cannot be
 * evicted or retired. OK durably permits later reclamation; does not delete
 * the payload now. Active, inbound and unbound records are rejected.
 * A crash before OK may be retried after replay. No external effects. */
int ninlil_release_bound(ninlil_runtime *runtime, const ninlil_id *message_id);
#endif
