#ifndef NINLIL_TOPOLOGY_H
#define NINLIL_TOPOLOGY_H

#include "ninlil.h"

#include <stdint.h>

#define NINLIL_DOMAIN_GATEWAY_MAX 8u
#define NINLIL_ROUTE_BACKUP_MAX 2u
#define NINLIL_ROUTE_PEER_MAX 512u
#define NINLIL_HOST_DEDUPE_MAX 1024u

typedef struct ninlil_reception_observation {
    ninlil_id message_id;
    uint64_t gateway_uid;
    uint64_t timestamp_ms;
    uint32_t radio_profile_id;
    int16_t rssi_dbm;
    int8_t snr_db;
} ninlil_reception_observation;

typedef struct ninlil_route_lease {
    uint16_t peer;
    uint64_t active_gateway_uid;
    uint64_t backup_gateway_uids[NINLIL_ROUTE_BACKUP_MAX];
    uint64_t route_epoch;
    uint64_t lease_until_ms;
    uint8_t backup_count;
    uint8_t released;
    uint8_t used;
} ninlil_route_lease;

typedef struct ninlil_uplink_dedupe {
    ninlil_id message_id;
    uint64_t sequence;
    uint8_t used;
} ninlil_uplink_dedupe;

typedef int (*ninlil_route_commit)(void *ctx,
                                   const ninlil_route_lease *lease);
typedef void (*ninlil_reception_observer)(
    void *ctx, const ninlil_reception_observation *observation);

typedef struct ninlil_topology {
    ninlil_route_lease *routes;
    ninlil_uplink_dedupe *dedupe;
    uint16_t route_capacity;
    uint16_t dedupe_capacity;
    uint16_t dedupe_cursor;
    uint64_t gateways[NINLIL_DOMAIN_GATEWAY_MAX];
    uint64_t dedupe_sequence;
    uint8_t gateway_count;
    uint8_t authority_known;
    uint8_t poisoned;
    ninlil_route_commit commit;
    void *commit_ctx;
    ninlil_reception_observer observer;
    void *observer_ctx;
} ninlil_topology;

int ninlil_topology_open(ninlil_topology *topology,
                         ninlil_route_lease *routes, uint16_t route_capacity,
                         ninlil_uplink_dedupe *dedupe,
                         uint16_t dedupe_capacity, ninlil_route_commit commit,
                         void *commit_ctx, ninlil_reception_observer observer,
                         void *observer_ctx);
int ninlil_topology_add_gateway(ninlil_topology *topology,
                                uint64_t gateway_uid);
/* Restore is used while replaying the Host's authoritative route log. */
int ninlil_topology_restore_route(ninlil_topology *topology,
                                  const ninlil_route_lease *lease);
void ninlil_topology_set_authority(ninlil_topology *topology, int known);
/* Every reception is reported diagnostically, while is_new is true only for
 * the first cached logical message ID. Metadata never enters Application data. */
int ninlil_topology_note_uplink(
    ninlil_topology *topology,
    const ninlil_reception_observation *observation, int *is_new);
int ninlil_topology_assign_route(ninlil_topology *topology, uint16_t peer,
                                 uint64_t active_gateway_uid,
                                 const uint64_t *backup_gateway_uids,
                                 uint8_t backup_count, uint64_t route_epoch,
                                 uint64_t lease_until_ms, uint64_t now_ms);
int ninlil_topology_release_route(ninlil_topology *topology, uint16_t peer,
                                  uint64_t gateway_uid,
                                  uint64_t route_epoch);
/* already_owned permits replay of existing custody while authority is
 * temporarily unknown; new downlink admission always fails closed. */
int ninlil_topology_check_downlink(const ninlil_topology *topology,
                                   uint16_t peer, uint64_t gateway_uid,
                                   uint64_t route_epoch, uint64_t now_ms,
                                   int already_owned);

#endif
