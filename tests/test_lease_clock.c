#include "ninlil_lease_clock.h"
#include "security_test_io.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    flash storage = {0};
    ninlil_security_io io = {read_flash, write_flash, erase_flash, &storage,
                             NINLIL_SECURITY_PARTITION_SIZE};
    ninlil_counter_config config = {0};
    ninlil_counter_store eras;
    ninlil_lease_clock root, peer;
    uint64_t before = 0u, after, sample, peer_time;
    memset(storage.bytes, 255, sizeof(storage.bytes));
    memcpy(config.session_fingerprint, "Ninlil lease era", 16u);
    config.reservation_size = 1u;
    config.max_counter_exclusive = UINT32_MAX - 1u;
    CHECK(ninlil_counter_open(&eras, &io, NINLIL_COUNTER_CREATE_NEW, &config) ==
          NINLIL_OK);
    CHECK(ninlil_lease_root_open(&root, &eras, 100u) == NINLIL_OK);
    CHECK(ninlil_lease_now(&root, 61100u - 1u, &before) == NINLIL_ERR_STATE);
    CHECK(before == 0u);
    CHECK(ninlil_lease_now(&root, 61100u, &before) == NINLIL_OK);
    ninlil_counter_close(&eras);
    CHECK(ninlil_counter_open(&eras, &io, NINLIL_COUNTER_RESUME_EXISTING,
                              &config) == NINLIL_OK);
    CHECK(ninlil_lease_root_open(&root, &eras, 0u) == NINLIL_OK);
    CHECK(ninlil_lease_now(&root, 61000u, &after) == NINLIL_OK);
    CHECK(after > before + NINLIL_LEASE_MAX_MS);
    ninlil_lease_peer_open(&peer, 0u);
    CHECK(ninlil_lease_now(&peer, 100u, &peer_time) == NINLIL_ERR_STATE);
    CHECK(ninlil_lease_request(&peer, 17u, 1000u) == NINLIL_OK);
    CHECK(ninlil_lease_accept(&peer, 18u, after, 1100u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_lease_request(&peer, 17u, 1100u) == NINLIL_OK);
    CHECK(peer.request_ms == 1000u);
    CHECK(ninlil_lease_accept(&peer, 17u, after, 1200u) == NINLIL_OK);
    CHECK(ninlil_lease_accept(&peer, 17u, after, 1300u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_lease_now(&peer, 2000u, &sample) == NINLIL_OK);
    CHECK(sample >= after + 1000u);
    CHECK(ninlil_lease_request(&peer, 19u, 2100u) == NINLIL_OK);
    CHECK(ninlil_lease_now(&peer, 3000u, &peer_time) == NINLIL_OK);
    CHECK(peer_time >=
          sample + 1000u); /* Pending renewal cannot move anchor. */
    CHECK(ninlil_lease_accept(&peer, 19u, after,
                              2100u + NINLIL_LEASE_SYNC_MAX_RTT_MS + 1u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_lease_now(&peer, 1200u + NINLIL_LEASE_SYNC_MAX_AGE_MS,
                           &peer_time) == NINLIL_OK);
    CHECK(ninlil_lease_now(&peer, 1201u + NINLIL_LEASE_SYNC_MAX_AGE_MS,
                           &peer_time) == NINLIL_ERR_STATE);
    CHECK(ninlil_lease_request(&peer, 20u, 40000u) == NINLIL_OK);
    CHECK(ninlil_lease_accept(&peer, 20u, before, 41000u) ==
          NINLIL_ERR_UNAUTHORIZED);
    CHECK(ninlil_lease_accept(&peer, 20u, after + 40000u, 50000u) == NINLIL_OK);
    CHECK(ninlil_lease_now(&peer, 80000u, &peer_time) == NINLIL_OK);
    CHECK(peer_time >= after + 80000u &&
          peer_time <= after + 80000u + NINLIL_LEASE_SYNC_ERROR_BOUND_MS);
    ninlil_lease_invalidate(&peer);
    CHECK(ninlil_lease_now(&peer, 80001u, &peer_time) == NINLIL_ERR_STATE);
    CHECK(ninlil_lease_now(&root, 1u, &after) == NINLIL_ERR_STATE);
    CHECK(ninlil_lease_now(&root, 70000u, &after) == NINLIL_ERR_STATE);
    ninlil_counter_close(&eras);
    puts("durable boot era/quarantine/conservative sync/replay/stale clock "
         "PASS");
    return 0;
}
