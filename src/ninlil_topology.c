#include "ninlil_topology.h"

#include <string.h>

static int id_equal(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

static int id_valid(const ninlil_id *id)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= id->bytes[index];
    return combined != 0u;
}

static int lease_same(const ninlil_route_lease *left,
                      const ninlil_route_lease *right)
{
    uint8_t index;

    if (left->peer != right->peer ||
        left->active_gateway_uid != right->active_gateway_uid ||
        left->route_epoch != right->route_epoch ||
        left->lease_from_ms != right->lease_from_ms ||
        left->lease_until_ms != right->lease_until_ms ||
        left->backup_count != right->backup_count ||
        left->released != right->released || left->used != right->used)
        return 0;
    for (index = 0u; index < left->backup_count; index++) {
        if (left->backup_gateway_uids[index] !=
            right->backup_gateway_uids[index])
            return 0;
    }
    return 1;
}

static int gateway_known(const ninlil_topology *topology, uint64_t gateway_uid)
{
    uint8_t index;

    for (index = 0u; index < topology->gateway_count; index++) {
        if (topology->gateways[index] == gateway_uid)
            return 1;
    }
    return 0;
}

static ninlil_route_lease *find_route(ninlil_topology *topology, uint16_t peer)
{
    uint16_t index;

    for (index = 0u; index < topology->route_capacity; index++) {
        if (topology->routes[index].used &&
            topology->routes[index].peer == peer)
            return &topology->routes[index];
    }
    return NULL;
}

static const ninlil_route_lease *
find_route_const(const ninlil_topology *topology, uint16_t peer)
{
    uint16_t index;

    for (index = 0u; index < topology->route_capacity; index++) {
        if (topology->routes[index].used &&
            topology->routes[index].peer == peer)
            return &topology->routes[index];
    }
    return NULL;
}

int ninlil_topology_open(ninlil_topology *topology, ninlil_route_lease *routes,
                         uint16_t route_capacity, ninlil_uplink_dedupe *dedupe,
                         uint16_t dedupe_capacity, ninlil_route_commit commit,
                         void *commit_ctx, ninlil_reception_observer observer,
                         void *observer_ctx)
{
    if (!topology || !routes || !dedupe || !commit || route_capacity == 0u ||
        route_capacity > NINLIL_ROUTE_PEER_MAX || dedupe_capacity == 0u ||
        dedupe_capacity > NINLIL_HOST_DEDUPE_MAX)
        return NINLIL_ERR_INVALID;
    memset(topology, 0, sizeof(*topology));
    memset(routes, 0, (size_t)route_capacity * sizeof(*routes));
    memset(dedupe, 0, (size_t)dedupe_capacity * sizeof(*dedupe));
    topology->routes = routes;
    topology->route_capacity = route_capacity;
    topology->dedupe = dedupe;
    topology->dedupe_capacity = dedupe_capacity;
    topology->commit = commit;
    topology->commit_ctx = commit_ctx;
    topology->observer = observer;
    topology->observer_ctx = observer_ctx;
    return NINLIL_OK;
}

int ninlil_topology_add_gateway(ninlil_topology *topology, uint64_t gateway_uid)
{
    if (!topology || topology->poisoned || gateway_uid == 0u)
        return NINLIL_ERR_INVALID;
    if (gateway_known(topology, gateway_uid))
        return NINLIL_OK;
    if (topology->gateway_count >= NINLIL_DOMAIN_GATEWAY_MAX)
        return NINLIL_ERR_CAPACITY;
    topology->gateways[topology->gateway_count++] = gateway_uid;
    return NINLIL_OK;
}

void ninlil_topology_set_authority(ninlil_topology *topology, int known)
{
    if (topology)
        topology->authority_known = known ? 1u : 0u;
}

int ninlil_topology_note_uplink(ninlil_topology *topology,
                                const ninlil_reception_observation *observation,
                                int *is_new)
{
    uint16_t capacity;
    uint16_t index;
    ninlil_uplink_dedupe *slot = NULL;

    if (!topology || !observation || !id_valid(&observation->message_id) ||
        !is_new || topology->poisoned || !topology->dedupe)
        return NINLIL_ERR_INVALID;
    capacity = topology->dedupe_capacity;
    if (capacity == 0u || !gateway_known(topology, observation->gateway_uid))
        return NINLIL_ERR_INVALID;
    for (index = 0u; index < capacity; index++) {
        if (topology->dedupe[index].used &&
            id_equal(&topology->dedupe[index].message_id,
                     &observation->message_id)) {
            *is_new = 0;
            if (topology->observer)
                topology->observer(topology->observer_ctx, observation);
            return NINLIL_OK;
        }
        if (!slot && !topology->dedupe[index].used)
            slot = &topology->dedupe[index];
    }
    if (!slot) {
        slot = &topology->dedupe[topology->dedupe_cursor];
        topology->dedupe_cursor =
            (uint16_t)((topology->dedupe_cursor + 1u) % capacity);
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = 1u;
    slot->message_id = observation->message_id;
    *is_new = 1;
    if (topology->observer)
        topology->observer(topology->observer_ctx, observation);
    return NINLIL_OK;
}

static int backups_valid(const ninlil_topology *topology, uint64_t active,
                         const uint64_t *backups, uint8_t backup_count)
{
    uint8_t index;

    if (backup_count > NINLIL_ROUTE_BACKUP_MAX ||
        (backup_count > 0u && !backups))
        return 0;
    for (index = 0u; index < backup_count; index++) {
        uint8_t previous;

        if (!gateway_known(topology, backups[index]) ||
            backups[index] == active)
            return 0;
        for (previous = 0u; previous < index; previous++) {
            if (backups[previous] == backups[index])
                return 0;
        }
    }
    return 1;
}

int ninlil_topology_restore_route(ninlil_topology *topology,
                                  const ninlil_route_lease *lease)
{
    ninlil_route_lease *existing;
    ninlil_route_lease *slot = NULL;
    uint16_t index;

    if (!topology || !lease || topology->poisoned || lease->used != 1u ||
        lease->released > 1u || lease->peer == 0u ||
        lease->peer == UINT16_MAX || lease->active_gateway_uid == 0u ||
        lease->route_epoch == 0u ||
        lease->lease_until_ms <= lease->lease_from_ms ||
        lease->lease_until_ms - lease->lease_from_ms >
            NINLIL_ROUTE_LEASE_MAX_MS ||
        lease->backup_count > NINLIL_ROUTE_BACKUP_MAX ||
        !gateway_known(topology, lease->active_gateway_uid))
        return NINLIL_ERR_INVALID;
    for (index = 0u; index < lease->backup_count; index++) {
        uint16_t previous;

        if (!gateway_known(topology, lease->backup_gateway_uids[index]) ||
            lease->backup_gateway_uids[index] == lease->active_gateway_uid)
            return NINLIL_ERR_INVALID;
        for (previous = 0u; previous < index; previous++) {
            if (lease->backup_gateway_uids[previous] ==
                lease->backup_gateway_uids[index])
                return NINLIL_ERR_INVALID;
        }
    }
    existing = find_route(topology, lease->peer);
    if (existing) {
        ninlil_route_lease released = *existing;

        if (lease_same(existing, lease))
            return NINLIL_OK;
        released.released = 1u;
        if (lease->route_epoch == existing->route_epoch &&
            !existing->released && lease_same(&released, lease)) {
            *existing = *lease;
            return NINLIL_OK;
        }
        if (lease->route_epoch > existing->route_epoch) {
            *existing = *lease;
            return NINLIL_OK;
        }
        return NINLIL_ERR_CORRUPT;
    }
    for (index = 0u; index < topology->route_capacity; index++) {
        if (!topology->routes[index].used) {
            slot = &topology->routes[index];
            break;
        }
    }
    if (!slot)
        return NINLIL_ERR_CAPACITY;
    *slot = *lease;
    return NINLIL_OK;
}

static int route_matches(const ninlil_route_lease *route, uint64_t active,
                         const uint64_t *backups, uint8_t backup_count,
                         uint64_t route_epoch, uint64_t lease_until_ms)
{
    uint8_t index;

    if (!route || route->released || route->active_gateway_uid != active ||
        route->backup_count != backup_count ||
        route->route_epoch != route_epoch ||
        route->lease_until_ms != lease_until_ms)
        return 0;
    for (index = 0u; index < backup_count; index++) {
        if (route->backup_gateway_uids[index] != backups[index])
            return 0;
    }
    return 1;
}

static int commit_route(ninlil_topology *topology,
                        const ninlil_route_lease *candidate)
{
    int rc = topology->commit(topology->commit_ctx, candidate);

    if (rc != NINLIL_OK)
        topology->poisoned = 1u;
    return rc;
}

int ninlil_topology_assign_route(ninlil_topology *topology, uint16_t peer,
                                 uint64_t active_gateway_uid,
                                 const uint64_t *backup_gateway_uids,
                                 uint8_t backup_count, uint64_t route_epoch,
                                 uint64_t lease_until_ms, uint64_t now_ms)
{
    ninlil_route_lease candidate;
    ninlil_route_lease *existing;
    ninlil_route_lease *slot = NULL;
    uint16_t index;
    int rc;

    if (!topology || topology->poisoned || peer == 0u || peer == UINT16_MAX ||
        active_gateway_uid == 0u || route_epoch == 0u ||
        lease_until_ms <= now_ms ||
        lease_until_ms - now_ms > NINLIL_ROUTE_LEASE_MAX_MS ||
        !gateway_known(topology, active_gateway_uid) ||
        !backups_valid(topology, active_gateway_uid, backup_gateway_uids,
                       backup_count))
        return NINLIL_ERR_INVALID;
    if (!topology->authority_known)
        return NINLIL_ERR_UNAUTHORIZED;
    existing = find_route(topology, peer);
    if (route_matches(existing, active_gateway_uid, backup_gateway_uids,
                      backup_count, route_epoch, lease_until_ms))
        return NINLIL_OK;
    if (existing && existing->active_gateway_uid != active_gateway_uid &&
        !existing->released && now_ms < existing->lease_until_ms)
        return NINLIL_ERR_BUSY;
    if (existing && route_epoch <= existing->route_epoch)
        return NINLIL_ERR_CONFLICT;
    if (!existing) {
        for (index = 0u; index < topology->route_capacity; index++) {
            if (!topology->routes[index].used) {
                slot = &topology->routes[index];
                break;
            }
        }
        if (!slot)
            return NINLIL_ERR_CAPACITY;
    } else {
        slot = existing;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.used = 1u;
    candidate.peer = peer;
    candidate.active_gateway_uid = active_gateway_uid;
    candidate.backup_count = backup_count;
    candidate.route_epoch = route_epoch;
    candidate.lease_from_ms = now_ms;
    candidate.lease_until_ms = lease_until_ms;
    for (index = 0u; index < backup_count; index++)
        candidate.backup_gateway_uids[index] = backup_gateway_uids[index];
    rc = commit_route(topology, &candidate);
    if (rc == NINLIL_OK)
        *slot = candidate;
    return rc;
}

int ninlil_topology_release_route(ninlil_topology *topology, uint16_t peer,
                                  uint64_t gateway_uid, uint64_t route_epoch)
{
    ninlil_route_lease *existing;
    ninlil_route_lease candidate;
    int rc;

    if (!topology || topology->poisoned || peer == 0u || peer == UINT16_MAX ||
        gateway_uid == 0u || route_epoch == 0u)
        return NINLIL_ERR_INVALID;
    if (!topology->authority_known)
        return NINLIL_ERR_UNAUTHORIZED;
    existing = find_route(topology, peer);
    if (!existing)
        return NINLIL_ERR_NOT_FOUND;
    if (existing->active_gateway_uid != gateway_uid ||
        existing->route_epoch != route_epoch)
        return NINLIL_ERR_CONFLICT;
    if (existing->released)
        return NINLIL_OK;
    candidate = *existing;
    candidate.released = 1u;
    rc = commit_route(topology, &candidate);
    if (rc == NINLIL_OK)
        *existing = candidate;
    return rc;
}

int ninlil_topology_check_downlink(const ninlil_topology *topology,
                                   uint16_t peer, uint64_t gateway_uid,
                                   uint64_t route_epoch, uint64_t now_ms)
{
    const ninlil_route_lease *route;

    if (!topology || topology->poisoned || peer == 0u || peer == UINT16_MAX ||
        gateway_uid == 0u || route_epoch == 0u)
        return NINLIL_ERR_INVALID;
    if (!topology->authority_known)
        return NINLIL_ERR_UNAUTHORIZED;
    route = find_route_const(topology, peer);
    if (!route)
        return NINLIL_ERR_NOT_FOUND;
    if (route->released || route->active_gateway_uid != gateway_uid ||
        route->route_epoch != route_epoch || now_ms < route->lease_from_ms ||
        now_ms >= route->lease_until_ms)
        return NINLIL_ERR_CONFLICT;
    return NINLIL_OK;
}
