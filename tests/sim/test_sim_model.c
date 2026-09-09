#include "ninlil_wire.h"
#include "sim.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr, "CHECK %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static sim_manifest baseline(void)
{
    sim_manifest m = {0};
    m.version = 1u;
    m.seed = 7u;
    m.nodes = 5u;
    m.burst = 4u;
    m.payload_bytes = 52u;
    m.sf = 7u;
    m.duration_ms = 120000u;
    m.latency_target_ms = 120000u;
    return m;
}

static int test_airtime(void)
{
    CHECK(sim_airtime_us(7u, 92u) == 158976u);
    CHECK(sim_airtime_us(7u, 26u) == 61696u);
    CHECK(sim_airtime_us(9u, 92u) == 513024u);
    CHECK(sim_airtime_us(9u, 26u) == 205824u);
    CHECK(sim_airtime_us(12u, 92u) == 3776512u);
    CHECK(sim_airtime_us(12u, 26u) == 1646592u);
    CHECK(sim_airtime_us(6u, 92u) == 0u);
    CHECK(sim_airtime_us(13u, 92u) == 0u);
    CHECK(sim_airtime_us(7u, 93u) == 0u);
    CHECK(sim_airtime_us(UINT32_MAX, SIZE_MAX) == 0u);
    return 0;
}

static int parse_text(const char *text, sim_manifest *out)
{
    FILE *file = tmpfile();
    int rc;
    if (!file)
        return NINLIL_ERR_IO;
    if (fputs(text, file) == EOF || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NINLIL_ERR_IO;
    }
    rc = sim_manifest_read(file, out);
    if (fclose(file) != 0)
        return NINLIL_ERR_IO;
    return rc;
}

static int test_manifest(void)
{
    const char *bad[] = {
        "",          "seed=-1\n",        "seed=+1\n",   "seed=4294967296\n",
        "seed=7x\n", "seed=7\nseed=7\n", "unknown=1\n", "seed=\n",
        "seed=7",    "seed=7 \n",        "seed=7\n"};
    sim_manifest m = baseline(), unchanged;
    size_t index;
    CHECK(sim_manifest_validate(&m) == NINLIL_OK);
    for (index = 0u; index < sizeof(bad) / sizeof(bad[0]); index++) {
        unchanged = m;
        CHECK(parse_text(bad[index], &m) == NINLIL_ERR_INVALID);
        CHECK(memcmp(&m, &unchanged, sizeof(m)) == 0);
    }
    m.nodes = 6u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    m = baseline();
    m.burst = 25u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    m = baseline();
    m.payload_bytes = 53u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    m = baseline();
    m.interval_ms = UINT32_MAX - 5u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    m = baseline();
    m.offline_node = 5u;
    m.offline_end_ms = 1000u;
    CHECK(sim_manifest_validate(&m) == NINLIL_OK);
    m.offline_start_ms = 1000u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    m = baseline();
    m.restart_node = 1u;
    m.restart_ms = 120000u;
    CHECK(sim_manifest_validate(&m) == NINLIL_ERR_INVALID);
    return 0;
}

static size_t packet_for(uint8_t *packet, uint16_t source, uint16_t target)
{
    ninlil_submission request;
    ninlil_id id = {{1u}};
    uint8_t payload[52] = {0};
    ninlil_submission_defaults(&request);
    request.target = target;
    request.service = UINT16_C(0x0100);
    request.payload_len = 52u;
    return ninlil_wire_encode_data(packet, source, &request, &id, payload);
}

static int test_radio(void)
{
    sim_network network;
    sim_manifest m = baseline();
    uint8_t packet[SIM_MTU], received[SIM_MTU];
    size_t length = packet_for(packet, 1u, 2u), received_length = 0u;
    ninlil_link *parent, *child;
    CHECK(sim_network_init(&network, &m) == NINLIL_OK);
    parent = &network.nodes[0].link;
    child = &network.nodes[1].link;
    CHECK(parent->send(parent->ctx, packet, length) == NINLIL_OK);
    CHECK(network.tx_data == 0u);
    sim_network_advance(&network);
    CHECK(child->recv(child->ctx, received, sizeof(received),
                      &received_length) == 0);
    CHECK(parent->send(parent->ctx, packet, length) == NINLIL_OK);
    CHECK(parent->send(parent->ctx, packet, length) == NINLIL_OK);
    CHECK(network.nodes[0].pending_count == 1u && network.coalesced == 1u);
    length = packet_for(packet, 2u, 1u);
    CHECK(child->send(child->ctx, packet, length) == NINLIL_OK);
    sim_network_advance(&network);
    CHECK(network.tx_data == 1u);
    network.now_us = network.due_us - 1u;
    sim_network_advance(&network);
    CHECK(network.nodes[1].rx_count == 0u);
    network.now_us++;
    sim_network_advance(&network);
    CHECK(network.nodes[1].rx_count == 1u);
    CHECK(child->recv(child->ctx, received, 1u, &received_length) ==
          NINLIL_ERR_TOO_LARGE);
    CHECK(network.nodes[1].rx_count == 1u);
    CHECK(child->recv(child->ctx, received, sizeof(received),
                      &received_length) == 1);
    CHECK(received_length == SIM_MTU);
    network.now_us = network.slot_us;
    sim_network_advance(&network);
    CHECK(network.tx_data == 2u);
    sim_node_online(&network.nodes[0], 0);
    sim_node_online(&network.nodes[0], 1);
    network.now_us = network.due_us;
    sim_network_advance(&network);
    CHECK(network.nodes[0].rx_count == 0u && network.lost_data == 1u);
    CHECK(network.interrupted == 1u);
    length = packet_for(packet, 2u, 3u);
    CHECK(child->send(child->ctx, packet, length) == NINLIL_ERR_UNAUTHORIZED);
    packet[0] ^= 1u;
    CHECK(child->send(child->ctx, packet, length) == NINLIL_ERR_INVALID);
    CHECK(network.tx_data == 2u);
    return 0;
}

static int test_queue_bound_and_loss(void)
{
    sim_network network;
    sim_manifest m = baseline();
    uint8_t packet[SIM_MTU];
    size_t length = packet_for(packet, 1u, 2u);
    uint32_t index;
    m.duplicate_permille = 1000u;
    CHECK(sim_network_init(&network, &m) == NINLIL_OK);
    for (index = 0u; index < 5u; index++) {
        network.now_us = (uint64_t)index * m.nodes * network.slot_us;
        CHECK(network.nodes[0].link.send(&network.nodes[0], packet, length) ==
              NINLIL_OK);
        sim_network_advance(&network);
        network.now_us = network.due_us;
        sim_network_advance(&network);
    }
    CHECK(network.nodes[1].rx_count == SIM_RX_SLOTS);
    CHECK(network.nodes[1].rx_peak == SIM_RX_SLOTS &&
          network.rx_overflow == 2u);
    CHECK(network.duplicates == 5u);
    m.loss_permille = 1000u;
    CHECK(sim_network_init(&network, &m) == NINLIL_OK);
    CHECK(network.nodes[0].link.send(&network.nodes[0], packet, length) ==
          NINLIL_OK);
    sim_network_advance(&network);
    network.now_us = network.due_us;
    sim_network_advance(&network);
    CHECK(network.lost_data == 1u && network.nodes[1].rx_count == 0u);
    CHECK(sim_network_init(&network, &m) == NINLIL_OK);
    for (index = 0u; index <= SIM_TX_SLOTS; index++) {
        ninlil_submission request;
        ninlil_id id = {{1u}};
        uint8_t payload[52] = {0};
        int expected = index < SIM_TX_SLOTS ? NINLIL_OK : NINLIL_ERR_BUSY;
        id.bytes[1] = (uint8_t)index;
        ninlil_submission_defaults(&request);
        request.target = 2u;
        request.service = UINT16_C(0x0100);
        request.payload_len = 52u;
        length = ninlil_wire_encode_data(packet, 1u, &request, &id, payload);
        CHECK(network.nodes[0].link.send(&network.nodes[0], packet, length) ==
              expected);
    }
    CHECK(network.nodes[0].pending_count == SIM_TX_SLOTS &&
          network.tx_data == 0u);
    sim_node_online(&network.nodes[0], 0);
    CHECK(network.nodes[0].pending_count == 0u);
    return 0;
}

int main(void)
{
    if (test_airtime() || test_manifest() || test_radio() ||
        test_queue_bound_and_loss())
        return 1;
    puts("sim model: airtime, invalid input, half duplex, mid-frame loss, "
         "queue bound PASS");
    return 0;
}
