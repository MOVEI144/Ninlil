#include "ninlil_lease_clock.h"
#include <string.h>

int ninlil_lease_root_open(ninlil_lease_clock *c, ninlil_counter_store *eras,
                           uint64_t now)
{
    uint64_t era;
    int rc;
    if (!c || !eras)
        return NINLIL_ERR_INVALID;
    memset(c, 0, sizeof(*c));
    rc = ninlil_counter_next(eras, &era);
    if (rc != NINLIL_OK)
        return rc;
    if (era >= UINT32_MAX - 1u)
        return NINLIL_ERR_CAPACITY;
    c->root = 1u;
    c->stamp = (era + 1u) << 32;
    c->began_ms = c->last_local_ms = now;
    return NINLIL_OK;
}

void ninlil_lease_peer_open(ninlil_lease_clock *c, uint64_t now)
{
    if (c) {
        memset(c, 0, sizeof(*c));
        c->began_ms = c->last_local_ms = now;
    }
}

void ninlil_lease_invalidate(ninlil_lease_clock *c)
{
    if (c && !c->root) {
        c->synchronized = 0u;
        c->challenge = 0u;
    }
}

int ninlil_lease_request(ninlil_lease_clock *c, uint64_t challenge,
                         uint64_t now)
{
    if (!c || c->root || !challenge || now < c->last_local_ms)
        return NINLIL_ERR_STATE;
    if (c->challenge == challenge)
        return NINLIL_OK;
    c->challenge = challenge;
    c->request_ms = c->last_local_ms = now;
    return NINLIL_OK;
}

static int peer_time(const ninlil_lease_clock *c, uint64_t now, uint64_t *out)
{
    uint64_t age, elapsed, margin;
    if (now < c->began_ms || now < c->anchor_ms)
        return NINLIL_ERR_STATE;
    age = now - c->began_ms;
    elapsed = now - c->anchor_ms;
    if (age > NINLIL_LEASE_SYNC_MAX_AGE_MS ||
        elapsed > NINLIL_LEASE_SYNC_MAX_AGE_MS + NINLIL_LEASE_SYNC_MAX_RTT_MS)
        return NINLIL_ERR_STATE;
    /* Root/local rate ratio <= 1.001/0.999 < 1.003. Round conservatively. */
    margin = (elapsed * 3u + 999u) / 1000u + 1u;
    if (c->stamp > UINT64_MAX - elapsed - margin)
        return NINLIL_ERR_STATE;
    *out = c->stamp + elapsed + margin;
    return NINLIL_OK;
}

int ninlil_lease_accept(ninlil_lease_clock *c, uint64_t challenge,
                        uint64_t stamp, uint64_t now)
{
    ninlil_lease_clock candidate;
    uint64_t value;
    int rc;
    if (!c || c->root || !challenge || c->challenge != challenge ||
        now < c->last_local_ms || now < c->request_ms ||
        now - c->request_ms > NINLIL_LEASE_SYNC_MAX_RTT_MS ||
        (stamp >> 32) == 0u || (stamp >> 32) < (c->last_ms >> 32))
        return NINLIL_ERR_UNAUTHORIZED;
    candidate = *c;
    candidate.stamp = stamp;
    candidate.anchor_ms = c->request_ms;
    candidate.began_ms = now;
    rc = peer_time(&candidate, now, &value);
    if (rc != NINLIL_OK)
        return rc;
    candidate.last_ms = value > c->last_ms ? value : c->last_ms;
    candidate.last_local_ms = now;
    candidate.synchronized = 1u;
    candidate.challenge = 0u;
    *c = candidate;
    return NINLIL_OK;
}

int ninlil_lease_now(ninlil_lease_clock *c, uint64_t now, uint64_t *out)
{
    uint64_t value, elapsed;
    int rc;
    if (!c || !out)
        return NINLIL_ERR_STATE;
    if (now < c->last_local_ms) {
        ninlil_lease_invalidate(c);
        if (c->root)
            c->stamp = 0u;
        return NINLIL_ERR_STATE;
    }
    c->last_local_ms = now;
    if (c->root) {
        elapsed = now - c->began_ms;
        if (!c->stamp || elapsed < NINLIL_LEASE_REBOOT_WAIT_MS ||
            elapsed > UINT32_MAX - NINLIL_LEASE_MAX_MS)
            return NINLIL_ERR_STATE;
        value = c->stamp + elapsed;
    } else {
        if (!c->synchronized)
            return NINLIL_ERR_STATE;
        rc = peer_time(c, now, &value);
        if (rc != NINLIL_OK) {
            ninlil_lease_invalidate(c);
            return rc;
        }
    }
    c->last_ms = value > c->last_ms ? value : c->last_ms;
    *out = c->last_ms;
    return NINLIL_OK;
}
