#include "ninlil_identity_flash.h"
#include "ninlil_network_pump.h"
#include "ninlil_security_partitions.h"
#include "node_example.h"
#include <string.h>

#ifdef NINLIL_NODE_ROSTER_HEADER
#include NINLIL_NODE_ROSTER_HEADER
#else
/* Buildable provisioning console; no trust and no autonomous RF startup. */
static const ninlil_node_member node_roster[1];
#define NODE_ROSTER_COUNT 0u
#endif
ninlil_identity node_identity;
ninlil_node_config node_config;
static ninlil_identity_flash identity_store;
static ninlil_identity_io identity_io;
static ninlil_esp_security_partition identity_region, era_region;
static ninlil_esp_security_partition sessions[NINLIL_NODE_MEMBERS_MAX * 2u];
static ninlil_counter_store era_store;

static int random_fill(void *ctx, uint8_t *out, size_t size)
{
    (void)ctx;
    return psa_generate_random(out, size) == PSA_SUCCESS ? NINLIL_OK
                                                         : NINLIL_ERR_IO;
}
static int counter_io(void *ctx, uint16_t slot, ninlil_security_io *io)
{
    (void)ctx;
    if (slot >= NINLIL_NODE_MEMBERS_MAX * 2u)
        return NINLIL_ERR_INVALID;
    return ninlil_esp_security_region(&sessions[slot], io, "node_sessions",
                                      (size_t)slot *
                                          NINLIL_SECURITY_PARTITION_SIZE,
                                      NINLIL_SECURITY_PARTITION_SIZE);
}
static int configuration(void)
{
    unsigned int i;
    memset(&node_config, 0, sizeof(node_config));
    node_config.local = CONFIG_NINLIL_NODE_ID;
    node_config.root = 1u;
    node_config.members = node_roster;
    node_config.member_count = NODE_ROSTER_COUNT;
    node_config.identity = &node_identity;
    node_config.journal_location = "node_core";
    node_config.control_location = "node_control";
    node_config.control_max_bytes = 0x20000u;
    node_config.permitted_profile = 1u;
    node_config.random.fill = random_fill;
    node_config.root_eras = &era_store;
    node_config.counter_io = counter_io;
    node_config.emit = ninlil_esp_network_emit;
    for (i = 0u; i < node_config.member_count; i++)
        if (node_roster[i].grant.node == node_config.local)
            return ninlil_role_profile_standard(node_roster[i].grant.role,
                                                &node_config.resources);
    return NINLIL_ERR_NOT_FOUND;
}
static int eras(int provision)
{
    ninlil_security_io io;
    ninlil_counter_config c = {{0}, 0u, 1u, UINT32_MAX - 1u};
    int rc;
    if (node_config.local != node_config.root || era_store.opened)
        return NINLIL_OK;
    memcpy(c.session_fingerprint, node_identity.identity, 16u);
    rc = ninlil_esp_security_region(&era_region, &io, "node_era", 0u,
                                    NINLIL_SECURITY_PARTITION_SIZE);
    if (rc == NINLIL_OK)
        rc = ninlil_counter_open(&era_store, &io,
                                 NINLIL_COUNTER_RESUME_EXISTING, &c);
    if (rc == NINLIL_ERR_NOT_FOUND && provision && !node_identity.initialized)
        rc =
            ninlil_counter_open(&era_store, &io, NINLIL_COUNTER_CREATE_NEW, &c);
    if (rc == NINLIL_ERR_NOT_FOUND && node_identity.initialized)
        rc = NINLIL_ERR_CORRUPT;
    return rc;
}
int node_storage_open(void)
{
    ninlil_security_io io;
    ninlil_flash_io flash_io;
    int rc = psa_crypto_init() == PSA_SUCCESS ? NINLIL_OK : NINLIL_ERR_IO;
    (void)configuration();
    if (rc == NINLIL_OK)
        rc = ninlil_esp_security_region(&identity_region, &io, "node_identity",
                                        0u, NINLIL_SECURITY_PARTITION_SIZE);
    if (rc != NINLIL_OK)
        return rc;
    flash_io = (ninlil_flash_io){io.read, io.write, io.erase, io.ctx, io.size};
    rc = ninlil_identity_flash_open(&identity_store, &flash_io, &identity_io);
    if (rc == NINLIL_OK)
        rc = ninlil_identity_open(&node_identity, identity_io);
    if (rc == NINLIL_OK)
        rc = node_deployment_open();
    if (rc == NINLIL_OK)
        rc = eras(0);
    return rc;
}
int node_storage_provision(void)
{
    int rc = NINLIL_OK;
    if (!node_identity.signing_key)
        rc = ninlil_identity_provision(&node_identity, identity_io);
    if (rc == NINLIL_OK)
        rc = eras(1);
    if (rc == NINLIL_OK && node_config.member_count)
        rc = node_application_open("node_app",
                                   node_identity.initialized ? 0 : 1);
    if (rc == NINLIL_OK && node_config.member_count)
        rc = ninlil_node_provision_stores(&node_config);
    node_application_close();
    return rc;
}
