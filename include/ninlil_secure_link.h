#ifndef NINLIL_SECURE_LINK_H
#define NINLIL_SECURE_LINK_H

#include "ninlil_secure.h"

#define NINLIL_SECURE_PEERS_MAX 512u
typedef struct ninlil_secure_peer {
    uint16_t node;
    ninlil_secure_session *session;
} ninlil_secure_peer;

typedef struct ninlil_secure_mux {
    ninlil_link bearer;
    ninlil_secure_peer *peers;
    uint16_t capacity;
    uint16_t local;
    ninlil_policy_lookup policy;
    void *policy_ctx;
    uint32_t rejected;
} ninlil_secure_mux;

/* Caller owns peer/session arrays. This direct multi-peer adapter is used as
 * the existing Core's Link; it does not duplicate Core custody or retry.
 * A successful send means bearer admission only. Every retry uses a new
 * reserved counter; the destination Core handles logical-message duplicates.
 * One execution owner; plaintext temporaries are always wiped. */
int ninlil_secure_mux_open(ninlil_secure_mux *mux, ninlil_link bearer,
                           ninlil_secure_peer *peers, uint16_t capacity,
                           uint16_t local, ninlil_policy_lookup policy,
                           void *policy_ctx, ninlil_link *link);
int ninlil_secure_mux_add(ninlil_secure_mux *mux, uint16_t node,
                          ninlil_secure_session *session);
void ninlil_secure_mux_remove(ninlil_secure_mux *mux, uint16_t node);

#endif
