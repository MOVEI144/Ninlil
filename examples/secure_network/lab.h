#ifndef LAB_H
#define LAB_H
#include "lab_crypto.h"
#include "ninlil_airtime.h"
#include "ninlil_control_log.h"
#include "ninlil_routed.h"
#include "security_test_io.h"
#include "test_support.h"
#include <stdio.h>
#define REQUIRE(x)                                                             \
    do {                                                                       \
        int lab_rc_ = (x);                                                     \
        if (lab_rc_ != NINLIL_OK) {                                            \
            fprintf(stderr, "%s:%d %s => %d\n", __FILE__, __LINE__, #x,        \
                    lab_rc_);                                                  \
            return 1;                                                          \
        }                                                                      \
    } while (0)
#define ASSERT(x)                                                              \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x);             \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct lab_session {
    ninlil_secure_session session;
    ninlil_counter_store counter;
    flash storage;
} lab_session;
struct lab;
typedef struct lab_node {
    struct lab *lab;
    uint16_t id;
    lab_identity identity;
    lab_session sessions[LAB_NODES][2];
    ninlil_routed routed;
    ninlil_runtime *core;
    ninlil_relay relay;
    ninlil_relay_slot slots[16];
    ninlil_airtime_scheduler scheduler;
    ninlil_control_log *log;
    ninlil_join_endpoint member;
    uint64_t token;
    char core_path[320];
    char log_path[320];
} lab_node;

typedef struct lab {
    lab_node nodes[LAB_NODES];
    ninlil_join_authority authority;
    ninlil_join_peer members[LAB_NODES];
    ninlil_control_log *log;
    ninlil_coordinator coordinator;
    ninlil_network_node graph_nodes[LAB_NODES];
    ninlil_network_edge edges[32];
    char directory[256];
    char log_path[320];
    uint64_t now_ms;
    uint64_t physical_attempts;
    uint64_t dropped;
    uint64_t ticks;
    uint32_t rng;
    uint32_t weak_attempts[2];
    int adaptive;
    int drop_ack;
    uint8_t offline[LAB_NODES];
} lab;
int lab_open(lab *l, int adaptive);
int lab_tick(lab *l);
int lab_rekey(lab *l, uint16_t peer);
int lab_plan(lab *l, uint16_t from, uint16_t to, uint16_t excluded);
int lab_refresh(lab *l);
void lab_close(lab *l);
#endif
