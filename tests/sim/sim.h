#ifndef NINLIL_SIM_H
#define NINLIL_SIM_H

#include "ninlil.h"

#include <stdio.h>

#define SIM_NODES 5u
#define SIM_MTU 92u
#define SIM_RX_SLOTS 8u
#define SIM_TX_SLOTS 8u
#define SIM_BURST 24u
#define SIM_MESSAGES (2u * (SIM_NODES - 1u) * SIM_BURST)
#define SIM_TICK_US UINT64_C(10000)

/* Test-only model v1: fixed BW125, CR4/5, preamble8, explicit header, CRC.
 * All manifest fields are required; no implicit production radio settings. */
typedef struct sim_manifest {
    uint32_t version, seed, nodes, burst, payload_bytes, sf, duration_ms;
    uint32_t interval_ms, latency_target_ms, loss_permille, duplicate_permille;
    uint32_t offline_node, offline_start_ms, offline_end_ms;
    uint32_t restart_node, restart_ms, receipt_hold_ms;
} sim_manifest;

typedef struct sim_packet {
    uint8_t bytes[SIM_MTU];
    size_t length;
    uint16_t target;
    uint8_t type;
} sim_packet;

typedef struct sim_node {
    ninlil_runtime *runtime;
    ninlil_link link;
    void *network;
    uint16_t id;
    uint32_t rng, boot;
    int online;
    sim_packet pending[SIM_TX_SLOTS];
    size_t pending_count, pending_peak;
    sim_packet rx[SIM_RX_SLOTS];
    size_t rx_count, rx_peak;
    char path[100];
} sim_node;

typedef struct sim_network {
    sim_manifest manifest;
    sim_node nodes[SIM_NODES];
    sim_packet flight;
    uint64_t now_us, due_us, slot_us, used_slot;
    uint64_t data_airtime_us, receipt_airtime_us;
    uint32_t rng, tx_data, tx_receipt, lost_data, lost_receipt;
    uint32_t duplicates, rx_overflow, busy, interrupted, coalesced;
    int in_flight, flight_blocked;
} sim_network;

typedef struct sim_message {
    ninlil_id key, id;
    uint16_t source, target, sequence;
    uint64_t admitted_us, first_offer_us, satisfied_us;
    uint32_t offers, last_offer_boot;
    int admitted, rejected, satisfied;
} sim_message;

typedef struct sim_run {
    sim_network network;
    sim_message messages[SIM_MESSAGES];
    size_t count;
    uint32_t offered, admitted, rejected, reoffers, restarts;
    uint32_t isolated_active, healthy_satisfied;
    int checkpoint_done;
    char directory[40];
} sim_run;

int sim_manifest_read(FILE *file, sim_manifest *out);
int sim_manifest_write(FILE *file, const sim_manifest *manifest);
int sim_manifest_validate(const sim_manifest *manifest);
uint64_t sim_airtime_us(uint32_t sf, size_t bytes);
int sim_network_init(sim_network *network, const sim_manifest *manifest);
void sim_network_advance(sim_network *network);
void sim_node_online(sim_node *node, int online);
int sim_run_open(sim_run *run, const sim_manifest *manifest);
int sim_run_tick(sim_run *run);
int sim_run_finish(sim_run *run, FILE *report);
void sim_run_close(sim_run *run);
int sim_run_reopen(sim_run *run, uint32_t node);

#endif
