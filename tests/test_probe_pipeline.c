/* Executes the unchanged production node/pump translation units against typed
 * dependency doubles. This is not an RF, cryptographic, or SDK-ABI test. */
#include "ninlil_node_internal.h"
#include "ninlil_network_pump.h"
#include "ninlil_sleep.h"
#include "esp_random.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int64_t clock_us;
static int driver_result, sends, controls;
static uint8_t control_bytes[200];
static size_t control_length;
static node_control_kind control_kind;
static ninlil_network_edge received_edge;
static int8_t actual_power = -6;
static const uint8_t session[16] = {7u};

int64_t esp_timer_get_time(void) { return clock_us; }
uint32_t esp_random(void) { return 0u; }
uint64_t ninlil_node_get(const uint8_t *p, size_t n)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < n; i++) value = (value << 8) | p[i];
    return value;
}
void ninlil_node_put(uint8_t *p, uint64_t value, size_t n)
{
    while (n) { p[--n] = (uint8_t)value; value >>= 8; }
}
int ninlil_node_index(const ninlil_node *n, uint16_t id)
{
    for (unsigned int i = 0u; i < n->config.member_count; i++)
        if (n->members[i].grant.node == id) return (int)i;
    return -1;
}
int ninlil_node_policy(void *ctx, uint16_t peer, ninlil_peer_policy *policy)
{ (void)ctx; (void)peer; (void)policy; return NINLIL_ERR_UNAUTHORIZED; }
int ninlil_node_control_send(ninlil_node *n, uint16_t peer, node_control_kind kind,
                             const uint8_t *data, size_t size)
{
    (void)n; (void)peer;
    if (size > sizeof(control_bytes)) return NINLIL_ERR_TOO_LARGE;
    memcpy(control_bytes, data, size);
    control_length = size; control_kind = kind; controls++;
    return NINLIL_OK;
}
int ninlil_node_recovery_receive(ninlil_node *n, uint16_t peer, node_control_kind k,
                                 const uint8_t *data, size_t size)
{ (void)n; (void)peer; (void)k; (void)data; (void)size; return NINLIL_ERR_INVALID; }
int ninlil_node_recovery_step(ninlil_node *n) { (void)n; return NINLIL_OK; }
int ninlil_node_lease(ninlil_node *n, uint64_t *now)
{ *now = n->now_ms + 1000000u; return NINLIL_OK; }
int ninlil_coordinator_observe(ninlil_coordinator *c, uint16_t reporter,
                               const ninlil_network_edge *e, uint64_t now)
{
    (void)c;
    if (e->from != reporter || e->observed_ms > now || !e->membership_epoch)
        return NINLIL_ERR_UNAUTHORIZED;
    received_edge = *e;
    return NINLIL_OK;
}
int ninlil_secure_seal_neighbor(ninlil_secure_session *s, const uint8_t *in,
                                 size_t length, uint8_t *out, size_t capacity,
                                 size_t *written)
{ (void)s; (void)in; (void)length; (void)out; (void)capacity; (void)written; return NINLIL_ERR_BUSY; }
int ninlil_secure_inspect_tx(const ninlil_secure_session *s, const uint8_t *frame,
                             size_t size, uint8_t *out, size_t capacity,
                             size_t *written)
{
    if (!s->ready || size <= 40u || size > 240u || size - 40u > capacity ||
        memcmp(frame + 8, s->material.fingerprint, 16u))
        return NINLIL_ERR_UNAUTHORIZED;
    memcpy(out, frame + 40, size - 40u); *written = size - 40u;
    return NINLIL_OK;
}
void ninlil_secret_clear(void *data, size_t size) { memset(data, 0, size); }
int ninlil_ingest(ninlil_runtime *r, const uint8_t *data, size_t size)
{ (void)r; (void)data; (void)size; return NINLIL_ERR_STATE; }
int ninlil_health(const ninlil_runtime *r) { (void)r; return NINLIL_OK; }
int ninlil_verify_retained(ninlil_runtime *r) { (void)r; return NINLIL_OK; }
int ninlil_step(ninlil_runtime *r) { (void)r; return NINLIL_OK; }
void ninlil_edhoc_close(void *e) { (void)e; }
void ninlil_lease_invalidate(void *c) { (void)c; }
int ninlil_node_step(ninlil_node *n, uint64_t now)
{ n->now_ms = now; return NINLIL_OK; }
int ninlil_node_receive(ninlil_node *n, const uint8_t *f, size_t length, uint64_t now)
{ (void)n; (void)f; (void)length; (void)now; return NINLIL_ERR_EMPTY; }
int ninlil_node_frame_current(ninlil_node *n, const uint8_t *f, size_t length, uint64_t now)
{ n->now_ms = now; return ninlil_node_probe_current(n, f, length); }
void ninlil_node_transmitted(ninlil_node *n, const uint8_t *f, size_t length,
                              int result, uint32_t airtime, uint64_t now)
{
    n->now_ms = now;
    if (result == NINLIL_OK)
        ninlil_node_probe_transmitted(n, f, length, airtime);
}
int ninlil_routed_receive(ninlil_routed *r, const uint8_t *f, size_t length, uint64_t now)
{ (void)r; (void)f; (void)length; (void)now; return NINLIL_ERR_EMPTY; }
int ninlil_routed_poll(ninlil_routed *r, uint64_t now)
{ (void)r; (void)now; return NINLIL_OK; }
int ninlil_routed_frame_current(ninlil_routed *r, const uint8_t *f, size_t length)
{ (void)r; (void)f; (void)length; return NINLIL_ERR_STATE; }
int ninlil_sx1262_radio_airtime(ninlil_sx1262_radio *r, uint16_t size, uint32_t *airtime)
{ (void)r; (void)size; *airtime = 10000u; return NINLIL_OK; }
int ninlil_sx1262_radio_power(ninlil_sx1262_radio *r, int8_t power)
{ r->requested_power_dbm = power; return NINLIL_OK; }
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *r, uint8_t *f, uint16_t capacity,
                                uint16_t *size, ninlil_sx1262_rx_info *info, unsigned int wait)
{ (void)r; (void)f; (void)capacity; (void)size; (void)info; (void)wait; return NINLIL_ERR_EMPTY; }
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *r, const uint8_t *f, uint16_t size)
{
    (void)f; (void)size; sends++;
    if (driver_result != NINLIL_OK) return driver_result;
    clock_us += 10000;
    r->applied_power_dbm = actual_power;
    return NINLIL_OK;
}

static void node_init(ninlil_node *n, uint16_t local)
{
    memset(n, 0, sizeof(*n));
    n->config.local = local; n->config.root = 1u;
    n->config.member_count = 2u; n->config.permitted_profile = 1u;
    n->local_index = (uint16_t)(local - 1u); n->root_index = 0u;
    n->joined = 1u;
    for (unsigned int i = 0u; i < 2u; i++) {
        n->members[i].grant.node = (uint16_t)(i + 1u);
        n->members[i].grant.membership_epoch = 1u;
        n->peers[i].member_active = 1u;
        n->peers[i].sessions[1].ready = 1u;
        memcpy(n->peers[i].sessions[1].material.fingerprint, session, 16u);
        n->peers[i].probe_at = UINT64_MAX;
    }
}
static void probe(ninlil_node *n, uint8_t frame[240], uint64_t token)
{
    memset(frame, 0, 240u); memcpy(frame, "NS\001", 3u);
    ninlil_node_put(frame + 4, 2u, 2u); ninlil_node_put(frame + 6, 1u, 2u);
    memcpy(frame + 8, session, 16u); frame[31] = 2u;
    frame[40] = NODE_PROBE; ninlil_node_put(frame + 41, token, 8u);
    n->peers[0].probe_token = token; n->peers[0].probe_sent = 0u;
}
int main(void)
{
    ninlil_node child, root;
    ninlil_esp_network_pump pump;
    ninlil_probe_monitor monitor;
    ninlil_sx1262_radio radio = {{-3}, -3, -3};
    uint8_t frame[240], reply[8], report[41], ack[11];
    ninlil_link_window w;
    node_init(&child, 2u); node_init(&root, 1u);
    clock_us = 0;
    CHECK(ninlil_esp_node_open(&pump, &radio, &child, 800000u) == NINLIL_OK);
#ifdef CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL
    CHECK(pump.closed_probes && pump.monitor == &pump.probe_workspace);
#else
    CHECK(!pump.closed_probes && pump.monitor == NULL);
    CHECK(ninlil_esp_node_closed_probes(&pump, &monitor) == NINLIL_OK);
#endif
    CHECK(ninlil_esp_node_closed_probes(&pump, &monitor) == NINLIL_ERR_STATE);
    for (uint64_t i = 0u; i < 8u; i++) {
        int64_t queued = (int64_t)(1000000u + i * 4000000u);
        clock_us = queued; child.now_ms = (uint64_t)clock_us / 1000u;
        probe(&child, frame, i + 1u);
        CHECK(ninlil_esp_network_emit(&pump, 1u, NINLIL_TRAFFIC_NORMAL, frame, 240u) == 0);
        clock_us += 20000;
        /* Same staged content retains first admission, not the retry time. */
        CHECK(ninlil_esp_network_emit(&pump, 1u, NINLIL_TRAFFIC_NORMAL, frame, 240u) == 0);
        clock_us = queued + 100000;
        driver_result = NINLIL_ERR_BUSY;
        CHECK(ninlil_esp_network_step(&pump) == NINLIL_ERR_BUSY);
        CHECK(!child.peers[0].probe_sent);
        clock_us = queued + 500000;
        driver_result = NINLIL_OK;
        CHECK(ninlil_esp_network_step(&pump) == 0);
        CHECK(pump.last_measurement_result == NINLIL_OK);
        CHECK(pump.monitor->peers[0].metrics.context.power_dbm == -6);
        CHECK(radio.requested_power_dbm == -3); /* Must not be the measured value. */
        if (i >= 2u) {
            child.now_ms += 2500u;
            ninlil_node_put(reply, i + 1u, 8u);
            CHECK(ninlil_node_link_receive(&child, 1u, NODE_PROBE_REPLY, reply, 8u) == 0);
        }
        child.peers[0].probe_at = UINT64_MAX;
    }
    child.now_ms = 32509u; child.peers[0].report_at = 0u;
    CHECK(ninlil_node_links_step(&child) == 0 && controls == 0);
    child.now_ms = 32510u; child.peers[0].report_at = 0u;
    CHECK(ninlil_node_links_step(&child) == 0 && controls == 1);
    CHECK(control_kind == NODE_OBSERVATION && control_length == 41u);
    memcpy(report, control_bytes, sizeof(report));
    CHECK(ninlil_node_get(report + 12, 4u) == 500000u);
    CHECK(ninlil_node_get(report + 6, 2u) == 6u && report[40] == 63u);
    root.now_ms = child.now_ms;
    CHECK(ninlil_node_link_receive(&root, 2u, NODE_OBSERVATION, report, 41u) == 0);
    CHECK(received_edge.queue_us == 500000u && received_edge.airtime_us == 10000u);
    CHECK(control_kind == NODE_OBSERVATION_ACK && control_length == 11u);
    memcpy(ack, control_bytes, sizeof(ack));
    child.peers[0].probe_token = 9u;
    CHECK(ninlil_node_link_receive(&child, 1u, NODE_OBSERVATION_ACK, ack, 11u) == 0);
    CHECK(ninlil_probe_monitor_read(pump.monitor, 1u, session, child.now_ms, 1, &w) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_probe_monitor_read(pump.monitor, 1u, session, child.now_ms, 0, &w) == 0);
    CHECK(w.queue_max_us == 500000u && w.context.power_dbm == -6);
    child.members[child.local_index].grant.role = NINLIL_ROLE_BATTERY_LEAF;
    CHECK(ninlil_node_suspend(&child, 33000u) == 0);
    CHECK(ninlil_node_resume(&child, 90000u) == 0);
    CHECK(ninlil_probe_monitor_read(pump.monitor, 1u, session, 90000u, 0, &w) == NINLIL_ERR_EMPTY);
    printf("production pump -> TX_DONE -> authenticated-reply boundary -> node report -> Coordinator input PASS; TX=%d queue_us=%u\n",
           sends, received_edge.queue_us);
    return 0;
}
