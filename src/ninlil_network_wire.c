#include "ninlil_network.h"

#include <string.h>

static void put(uint8_t *p, uint64_t n, size_t count)
{
    while (count) {
        p[--count] = (uint8_t)n;
        n >>= 8;
    }
}
static uint64_t get(const uint8_t *p, size_t count)
{
    uint64_t n = 0u;
    size_t i;
    for (i = 0u; i < count; i++)
        n = (n << 8) | p[i];
    return n;
}

int ninlil_network_plan_valid(const ninlil_network_plan *p)
{
    uint8_t mask;
    unsigned int i;
    if (!p || !ninlil_network_path_valid(&p->path) || p->epoch == 0u ||
        p->valid_until_ms == 0u || p->profile == 0u || p->rto_ms < 100u ||
        p->rto_ms > 30000u || p->path.cost_us == 0u ||
        p->path.cost_us > UINT64_C(104976000000) ||
        p->phase < NINLIL_PLAN_STAGED || p->phase > NINLIL_PLAN_RETIRED)
        return 0;
    if (p->prepare_until_ms && (p->valid_until_ms < p->prepare_until_ms ||
                                p->valid_until_ms - p->prepare_until_ms >
                                    NINLIL_NETWORK_LEASE_MAX_MS ||
                                (p->phase == NINLIL_PLAN_STAGED &&
                                 p->valid_until_ms != p->prepare_until_ms)))
        return 0;
    mask = (uint8_t)((1u << p->path.count) - 1u);
    if ((p->prepared & (uint8_t)~mask) || (p->applied & (uint8_t)~mask) ||
        (p->applied & p->prepared) != p->applied ||
        (p->phase == NINLIL_PLAN_STAGED && p->applied != 0u) ||
        ((p->phase == NINLIL_PLAN_COMMITTED ||
          p->phase == NINLIL_PLAN_EFFECTIVE ||
          p->phase == NINLIL_PLAN_RETIRED) &&
         p->prepared != mask) ||
        ((p->phase == NINLIL_PLAN_EFFECTIVE ||
          p->phase == NINLIL_PLAN_RETIRED) &&
         p->applied != mask))
        return 0;
    for (i = 0u; i < p->path.count; i++)
        if (!p->path.membership_epochs[i])
            return 0;
    return 1;
}

size_t ninlil_network_plan_encode(const ninlil_network_plan *p, uint8_t *out,
                                  size_t capacity)
{
    uint8_t bytes[NINLIL_NETWORK_PLAN_MAX];
    unsigned int i;
    if (!out || capacity < sizeof(bytes) || !ninlil_network_plan_valid(p))
        return 0u;
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "NP\001", 3u);
    if (p->prepare_until_ms)
        bytes[2] = 2u;
    bytes[3] = (uint8_t)p->phase;
    bytes[4] = p->path.count;
    bytes[5] = p->prepared;
    bytes[6] = p->applied;
    put(bytes + 8, p->epoch, 8u);
    put(bytes + 16, p->valid_until_ms, 8u);
    put(bytes + 24, p->profile, 4u);
    put(bytes + 28, p->rto_ms, 4u);
    put(bytes + 32, p->path.cost_us, 8u);
    put(bytes + 40, p->prepare_until_ms, 8u);
    for (i = 0u; i < p->path.count; i++) {
        put(bytes + 48u + i * 10u, p->path.nodes[i], 2u);
        put(bytes + 50u + i * 10u, p->path.membership_epochs[i], 8u);
    }
    memcpy(out, bytes, sizeof(bytes));
    return sizeof(bytes);
}

int ninlil_network_plan_decode(const uint8_t *b, size_t length,
                               ninlil_network_plan *out)
{
    ninlil_network_plan p;
    unsigned int i;
    if (!b || !out || length != NINLIL_NETWORK_PLAN_MAX ||
        memcmp(b, "NP", 2u) != 0 || (b[2] != 1u && b[2] != 2u) || b[7] != 0u ||
        b[4] > NINLIL_NETWORK_PATH_MAX)
        return NINLIL_ERR_INVALID;
    memset(&p, 0, sizeof(p));
    p.prepare_until_ms = get(b + 40, 8u);
    if ((b[2] == 1u && p.prepare_until_ms) ||
        (b[2] == 2u && !p.prepare_until_ms))
        return NINLIL_ERR_INVALID;
    p.phase = (ninlil_plan_phase)b[3];
    p.path.count = b[4];
    p.prepared = b[5];
    p.applied = b[6];
    p.epoch = get(b + 8, 8u);
    p.valid_until_ms = get(b + 16, 8u);
    p.profile = (uint32_t)get(b + 24, 4u);
    p.rto_ms = (uint32_t)get(b + 28, 4u);
    p.path.cost_us = get(b + 32, 8u);
    for (i = 0u; i < NINLIL_NETWORK_PATH_MAX; i++) {
        uint16_t node = (uint16_t)get(b + 48u + i * 10u, 2u);
        uint64_t epoch = get(b + 50u + i * 10u, 8u);
        if (i < p.path.count) {
            p.path.nodes[i] = node;
            p.path.membership_epochs[i] = epoch;
        } else if (node || epoch)
            return NINLIL_ERR_INVALID;
    }
    if (!ninlil_network_plan_valid(&p))
        return NINLIL_ERR_INVALID;
    *out = p;
    return NINLIL_OK;
}
