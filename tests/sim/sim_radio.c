#include "ninlil_wire.h"
#include "sim.h"

#include <string.h>

uint64_t sim_airtime_us(uint32_t sf, size_t bytes)
{
    uint32_t numerator, denominator, symbols, quarter_symbols;
    if (sf < 7u || sf > 12u || bytes > SIM_MTU)
        return 0u;
    /* Restricted Semtech LoRa airtime equation; see SIMULATION.md.
     * Quarter-symbol arithmetic avoids floating point and millisecond rounding.
     */
    numerator = 8u * (uint32_t)bytes + 44u;
    numerator = numerator > 4u * sf ? numerator - 4u * sf : 0u;
    denominator = 4u * (sf - (sf >= 11u ? 2u : 0u));
    symbols = 8u + 5u * ((numerator + denominator - 1u) / denominator);
    quarter_symbols = 4u * (8u + symbols) + 17u;
    return (uint64_t)quarter_symbols * (UINT64_C(1) << sf) * 2u;
}

static uint32_t sample(sim_network *network)
{
    network->rng = network->rng * UINT32_C(1664525) + UINT32_C(1013904223);
    return (network->rng >> 8) % 1000u;
}

static int decode(const uint8_t *data, size_t length, sim_packet *packet,
                  uint16_t *source)
{
    uint8_t type;
    if (!data || length > SIM_MTU ||
        ninlil_wire_packet_type(data, length, &type) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    if (type == NINLIL_WIRE_DATA) {
        ninlil_wire_data_view view;
        if (ninlil_wire_decode_data(data, length, &view) != NINLIL_OK)
            return NINLIL_ERR_INVALID;
        *source = view.source;
        packet->target = view.target;
    } else {
        ninlil_wire_receipt_view view;
        if (ninlil_wire_decode_receipt(data, length, &view) != NINLIL_OK)
            return NINLIL_ERR_INVALID;
        *source = view.source;
        packet->target = view.target;
    }
    packet->type = type;
    packet->length = length;
    memcpy(packet->bytes, data, length);
    return NINLIL_OK;
}

static int radio_send(void *ctx, const uint8_t *data, size_t length)
{
    sim_node *node = ctx;
    sim_network *network = node->network;
    sim_packet packet = {0};
    uint16_t source;
    size_t index;
    int rc = decode(data, length, &packet, &source);

    if (rc != NINLIL_OK)
        return rc;
    if (source != node->id || packet.target == 0u ||
        packet.target > network->manifest.nodes || packet.target == source ||
        (source != 1u && packet.target != 1u))
        return NINLIL_ERR_UNAUTHORIZED;
    if (!node->online)
        return NINLIL_ERR_BUSY;
    /* A staged byte-identical retry has the same outstanding link obligation.
     * Coalescing is volatile and never supplies remote evidence. */
    for (index = 0u; index < node->pending_count; index++) {
        if (node->pending[index].length == length &&
            memcmp(node->pending[index].bytes, data, length) == 0) {
            network->coalesced++;
            return NINLIL_OK;
        }
    }
    if (node->pending_count == SIM_TX_SLOTS) {
        network->busy++;
        return NINLIL_ERR_BUSY;
    }
    node->pending[node->pending_count++] = packet;
    if (node->pending_peak < node->pending_count)
        node->pending_peak = node->pending_count;
    return NINLIL_OK;
}

static int radio_recv(void *ctx, uint8_t *buffer, size_t capacity,
                      size_t *length)
{
    sim_node *node = ctx;
    if (!buffer || !length)
        return NINLIL_ERR_INVALID;
    if (!node->online || node->rx_count == 0u)
        return 0;
    if (node->rx[0].length > capacity)
        return NINLIL_ERR_TOO_LARGE;
    *length = node->rx[0].length;
    memcpy(buffer, node->rx[0].bytes, *length);
    node->rx_count--;
    memmove(node->rx, node->rx + 1, node->rx_count * sizeof(node->rx[0]));
    return 1;
}

int sim_network_init(sim_network *network, const sim_manifest *manifest)
{
    uint32_t index;
    if (!network || sim_manifest_validate(manifest) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    memset(network, 0, sizeof(*network));
    network->manifest = *manifest;
    network->rng = manifest->seed;
    network->used_slot = UINT64_MAX;
    network->slot_us =
        ((sim_airtime_us(manifest->sf, SIM_MTU) + SIM_TICK_US - 1u) /
             SIM_TICK_US +
         1u) *
        SIM_TICK_US;
    for (index = 0u; index < manifest->nodes; index++) {
        sim_node *node = &network->nodes[index];
        node->network = network;
        node->id = (uint16_t)(index + 1u);
        node->rng = manifest->seed + index + 1u;
        node->boot = 1u;
        node->online = 1;
        node->link = (ninlil_link){radio_send, radio_recv, node, SIM_MTU};
    }
    return NINLIL_OK;
}

void sim_node_online(sim_node *node, int online)
{
    sim_network *network = node->network;
    if (!online && node->online) {
        /* A receiver going away mid-frame cannot receive its tail on return. */
        uint64_t slot = network->used_slot;
        if (network->in_flight &&
            (network->flight.target == node->id ||
             slot % network->manifest.nodes == (uint64_t)(node->id - 1u))) {
            network->flight_blocked = 1;
            network->interrupted++;
        }
        node->rx_count = 0u;
        node->pending_count = 0u;
    }
    node->online = online;
}

static void enqueue(sim_network *network, sim_node *node)
{
    if (node->rx_count == SIM_RX_SLOTS) {
        network->rx_overflow++;
        return;
    }
    node->rx[node->rx_count++] = network->flight;
    if (node->rx_peak < node->rx_count)
        node->rx_peak = node->rx_count;
}

static void complete_flight(sim_network *network)
{
    sim_node *target;
    uint32_t loss, duplicate;
    int lost;
    if (!network->in_flight || network->now_us < network->due_us)
        return;
    target = &network->nodes[network->flight.target - 1u];
    loss = sample(network);
    duplicate = sample(network);
    lost =
        network->flight_blocked || !target->online ||
        loss < network->manifest.loss_permille ||
        (network->flight.type == NINLIL_WIRE_RECEIPT &&
         network->now_us < (uint64_t)network->manifest.receipt_hold_ms * 1000u);
    if (lost) {
        if (network->flight.type == NINLIL_WIRE_DATA)
            network->lost_data++;
        else
            network->lost_receipt++;
    } else {
        enqueue(network, target);
        if (duplicate < network->manifest.duplicate_permille) {
            /* Adapter duplicate fault, not a second free physical TX. */
            network->duplicates++;
            enqueue(network, target);
        }
    }
    network->in_flight = 0;
}

void sim_network_advance(sim_network *network)
{
    uint64_t slot = network->now_us / network->slot_us;
    sim_node *node = &network->nodes[slot % network->manifest.nodes];
    uint64_t airtime;
    complete_flight(network);
    if (network->in_flight || network->used_slot == slot || !node->online ||
        node->pending_count == 0u)
        return;
    airtime = sim_airtime_us(network->manifest.sf, node->pending[0].length);
    if (network->now_us % network->slot_us + airtime > network->slot_us)
        return;
    network->flight = node->pending[0];
    node->pending_count--;
    memmove(node->pending, node->pending + 1,
            node->pending_count * sizeof(node->pending[0]));
    network->in_flight = 1;
    network->flight_blocked =
        !network->nodes[network->flight.target - 1u].online;
    network->due_us = network->now_us + airtime;
    network->used_slot = slot;
    if (network->flight.type == NINLIL_WIRE_DATA) {
        network->tx_data++;
        network->data_airtime_us += airtime;
    } else {
        network->tx_receipt++;
        network->receipt_airtime_us += airtime;
    }
}
