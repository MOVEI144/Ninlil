#include "esp_timer.h"
#include "ninlil_network_pump.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d %s\n", __LINE__, #x);                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static int64_t clock_us;
static int transmit_result;
static unsigned int transmitted;
static uint64_t membership_epoch = 1u;
static ninlil_secure_session active_session;
static int policy(void *ctx, uint16_t peer, ninlil_peer_policy *out)
{
    (void)ctx;
    (void)peer;
    memset(out, 0, sizeof(*out));
    out->membership_epoch = membership_epoch;
    out->session_membership_epoch = membership_epoch;
    return NINLIL_OK;
}
static ninlil_secure_session *session(void *ctx, uint16_t peer, int hop)
{
    (void)ctx;
    (void)peer;
    (void)hop;
    return &active_session;
}
int64_t esp_timer_get_time(void)
{
    return clock_us;
}
int ninlil_sx1262_radio_airtime(const ninlil_sx1262_radio *r, uint16_t length,
                                uint32_t *out)
{
    (void)r;
    if (!length || length > 240u)
        return NINLIL_ERR_INVALID;
    *out = 1000u;
    return NINLIL_OK;
}
int ninlil_sx1262_radio_send(ninlil_sx1262_radio *r, const uint8_t *data,
                             uint16_t length)
{
    (void)r;
    if (!data || length != 232u || data[0] != 'N')
        return NINLIL_ERR_INVALID;
    transmitted++;
    return transmit_result;
}
int ninlil_sx1262_radio_receive(ninlil_sx1262_radio *r, uint8_t *data,
                                uint16_t capacity, uint16_t *length,
                                ninlil_sx1262_rx_info *info, TickType_t wait)
{
    (void)r;
    (void)data;
    (void)capacity;
    (void)length;
    (void)info;
    (void)wait;
    return NINLIL_ERR_EMPTY;
}
int main(void)
{
    ninlil_sx1262_radio radio;
    ninlil_routed routed;
    ninlil_esp_network_pump pump;
    uint8_t frame[232];
    memset(&radio, 0, sizeof(radio));
    memset(&routed, 0, sizeof(routed));
    memset(frame, 0x77, sizeof(frame));
    routed.config.local = 1u;
    routed.config.policy = policy;
    routed.config.session = session;
    active_session.local = 1u;
    active_session.peer = 2u;
    active_session.ready = 1u;
    CHECK(ninlil_secure_bind_membership(&active_session, 1u, 1u) == NINLIL_OK);
    CHECK(ninlil_secure_bind_membership(&active_session, 2u, 1u) ==
          NINLIL_ERR_STATE);
    memset(active_session.material.fingerprint, 9, 16u);
    memcpy(frame, "NS\001", 3u);
    frame[4] = frame[6] = 0u;
    frame[5] = 1u;
    frame[7] = 2u;
    memcpy(frame + 8, active_session.material.fingerprint, 16u);
    CHECK(ninlil_esp_network_open(&pump, &radio, &routed, 1000000u) ==
          NINLIL_OK);
    CHECK(transmitted == 0u);
    CHECK(ninlil_esp_network_emit(&pump, 2u, NINLIL_TRAFFIC_CONTROL, frame,
                                  sizeof(frame)) == NINLIL_OK);
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_OK && transmitted == 0u);
    clock_us = 1000;
    transmit_result = NINLIL_ERR_TIMEOUT;
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_ERR_TIMEOUT &&
          transmitted == 1u);
    CHECK(!pump.scheduler.busy);
    clock_us = 2000;
    transmit_result = NINLIL_OK;
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_OK && transmitted == 2u);
    clock_us = 3000;
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_OK && transmitted == 2u);
    CHECK(ninlil_esp_network_emit(&pump, 2u, NINLIL_TRAFFIC_CONTROL, frame,
                                  sizeof(frame)) == NINLIL_OK);
    membership_epoch = 2u;
    clock_us = 4000;
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_ERR_UNAUTHORIZED &&
          transmitted == 2u);
    clock_us = -1;
    CHECK(ninlil_esp_network_step(&pump) == NINLIL_ERR_IO);
    puts("network pump admission != TX_DONE; timeout retains frame; bounded "
         "retry PASS");
    return 0;
}
