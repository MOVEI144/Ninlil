#include "ninlil_journal.h"
#include "ninlil_setup.h"
#include "node_example.h"
#include <string.h>

static ninlil_setup *deployment;
static uint64_t revision;
static int autorun;
static uint8_t upload[NINLIL_DEPLOYMENT_PACKET_MAX],
    response[NINLIL_ADMISSION_MAX];
static uint16_t total, used, response_length;
static uint8_t operation;
static int resume_transfer(void);
static uint64_t get(const uint8_t *p, size_t size)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < size; i++)
        value = (value << 8) | p[i];
    return value;
}
static void put(uint8_t *p, uint64_t value, size_t size)
{
    while (size) {
        p[--size] = (uint8_t)value;
        value >>= 8;
    }
}
static int refresh(void)
{
    return ninlil_setup_config(deployment, &node_config, &revision, &autorun);
}
int node_deployment_open(void)
{
    int rc;
    ninlil_setup_close(deployment);
    deployment = NULL;
    revision = 0u;
    autorun = 0;
    rc = ninlil_setup_open(&deployment, "node_setup", &node_identity);
    if (rc == NINLIL_OK)
        rc = refresh();
    if (rc == NINLIL_ERR_BUSY && ninlil_setup_transfer_pending(deployment))
        rc = resume_transfer();
    return rc == NINLIL_ERR_EMPTY ? NINLIL_OK : rc;
}
int node_deployment_autorun(void)
{
    return deployment && revision && autorun &&
           (node_identity.initialized & 2u) && !node_config.offline;
}
int node_deployment_start(ninlil_node *n)
{
    if ((node_identity.initialized & 2u) && (!deployment || !revision))
        return NINLIL_ERR_CORRUPT;
    return deployment && revision ? ninlil_setup_advertise(deployment, n)
                                  : NINLIL_OK;
}
int node_deployment_disarm(void)
{
    int rc = deployment && revision ? ninlil_setup_autorun(deployment, 0)
                                    : NINLIL_OK;
    if (rc == NINLIL_OK && deployment && revision)
        rc = refresh();
    return rc;
}
static int configure(ninlil_node *active)
{
    ninlil_node_member root, local;
    ninlil_node *previous = NULL;
    uint16_t size;
    size_t prefix = operation == 4u ? 76u : 11u;
    const uint8_t *key;
    int rc;
    if (active || total < prefix + 1u || upload[8] > 1u || node_config.offline)
        return NINLIL_ERR_STATE;
    size = (uint16_t)get(upload + prefix - 2u, 2u);
    if (size > total - prefix ||
        (operation == 4u
             ? ninlil_admission_verify(upload + 9, upload + prefix, size, &root)
             : ninlil_member_decode(upload + prefix, size, &root)) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    key = operation == 4u ? upload + 9 : root.public_key;
    local = root;
    if (total > size + prefix &&
        ninlil_admission_verify(key, upload + size + prefix,
                                total - size - prefix, &local) != NINLIL_OK)
        return NINLIL_ERR_UNAUTHORIZED;
    if (memcmp(local.grant.identity, node_identity.identity, 32u) ||
        memcmp(local.public_key, node_identity.public_key, 65u))
        return NINLIL_ERR_UNAUTHORIZED;
    /* Normal updates retain deployment identity. Explicit transfer uses op 5.
     */
    if (node_config.authority_key &&
        (operation != 4u || memcmp(node_config.authority_key, key, 65u)))
        return NINLIL_ERR_CONFLICT;
    if (node_config.member_count && node_identity.initialized) {
        const ninlil_node_member *old = NULL;
        for (unsigned int i = 0u; i < node_config.member_count; i++)
            if (node_config.members[i].grant.node == node_config.local)
                old = &node_config.members[i];
        for (unsigned int i = 0u; i < node_config.member_count; i++)
            if (node_config.members[i].grant.node == node_config.root &&
                (memcmp(root.public_key, node_config.members[i].public_key,
                        65u) ||
                 memcmp(root.grant.identity,
                        node_config.members[i].grant.identity, 32u) ||
                 root.grant.membership_epoch !=
                     node_config.members[i].grant.membership_epoch))
                return NINLIL_ERR_CONFLICT;
        if (!old || local.grant.node != node_config.local ||
            root.grant.node != node_config.root ||
            local.grant.role != old->grant.role ||
            memcmp(local.grant.authority, old->grant.authority, 16u) ||
            local.grant.membership_epoch < old->grant.membership_epoch ||
            local.grant.binding_epoch < old->grant.binding_epoch)
            return NINLIL_ERR_CONFLICT;
        if (get(upload, 8u) == revision) {
            /* Mark prior Root use before assigning an independent issuer. */
            rc = node_storage_provision();
            if (rc != NINLIL_OK)
                return rc;
            rc = ninlil_node_open(&previous, &node_config, 0u);
            if (rc == NINLIL_OK)
                rc = ninlil_node_preserve_members(previous);
            ninlil_node_close(previous);
            if (rc != NINLIL_OK)
                return rc;
        }
    }
    if (!deployment) {
        rc = node_deployment_open();
        if (rc != NINLIL_OK)
            return rc;
    }
    rc = operation == 4u
             ? ninlil_setup_packet(deployment, upload, total)
             : ninlil_setup_update(deployment, get(upload, 8u), &root,
                                   upload + size + prefix,
                                   total - size - prefix, upload[8]);
    if (rc == NINLIL_OK)
        rc = refresh();
    if (rc == NINLIL_OK)
        rc = node_storage_provision();
    if (rc == NINLIL_OK)
        rc = ninlil_identity_mark_deployed(&node_identity);
    return rc;
}
static int bulk_retained(void *ctx, uint8_t type, const uint8_t *data,
                         uint16_t length, const ninlil_journal_ref *ref)
{
    (void)ctx;
    (void)type;
    (void)data;
    (void)length;
    (void)ref;
    return NINLIL_ERR_BUSY; /* Application must explicitly adopt/retire objects.
                             */
}
static int retire(int commit)
{
    ninlil_node *old = NULL;
    ninlil_journal *bulk = NULL;
    ninlil_node_config c = node_config;
    int rc = ninlil_journal_open(&bulk, "node_bulk", 256u * 1024u,
                                 bulk_retained, NULL);
    ninlil_journal_close(bulk);
    c.offline = 1u;
    if (rc == NINLIL_OK)
        rc = ninlil_node_open(&old, &c, 0u);
    if (rc == NINLIL_OK)
        rc = commit ? ninlil_node_retire_deployment(old)
                    : ninlil_node_deployment_idle(old);
    ninlil_node_close(old);
    return rc;
}
static int resume_transfer(void)
{
    int rc = retire(1);
    if (rc == NINLIL_OK)
        rc = ninlil_setup_transfer_finish(deployment);
    if (rc == NINLIL_OK)
        rc = refresh();
    if (rc == NINLIL_OK)
        rc = node_storage_provision();
    if (rc == NINLIL_OK)
        rc = ninlil_identity_mark_deployed(&node_identity);
    return rc;
}
static int transfer(ninlil_node *active)
{
    int rc;
    if (active || !deployment || !revision)
        return NINLIL_ERR_STATE;
    if (!ninlil_setup_transfer_pending(deployment)) {
        if (get(upload, 8u) + 1u == revision)
            return ninlil_setup_packet(deployment, upload,
                                       total); /* Lost final response. */
        rc = retire(0);
        if (rc != NINLIL_OK)
            return rc;
    }
    rc = ninlil_setup_transfer_begin(deployment, upload, total);
    if (rc == NINLIL_OK) {
        (void)refresh(); /* Intent forces offline before any retirement. */
        rc = resume_transfer();
    }
    return rc;
}
int node_management(ninlil_node *n, const uint8_t *data, size_t size,
                    uint8_t *out, size_t *written)
{
    *written = 0u;
    if ((node_identity.initialized & 2u) && (!deployment || !revision))
        return NINLIL_ERR_CORRUPT;
    if (!size)
        return NINLIL_ERR_INVALID;
    if (data[0] == 6u && size == 2u) {
        ninlil_control_counter counts;
        int rc = ninlil_node_control_inspect(n, data[1], &counts);
        if (rc != NINLIL_OK)
            return rc;
        put(out, counts.received, 2u);
        put(out + 2, counts.accepted, 2u);
        put(out + 4, (uint32_t)counts.last_result, 4u);
        *written = 8u;
        return NINLIL_OK;
    }
    if (data[0] == 0u && size == 4u && data[1] >= 1u && data[1] <= 5u) {
        uint16_t length = (uint16_t)get(data + 2, 2u);
        if (!length || length > sizeof(upload))
            return NINLIL_ERR_TOO_LARGE;
        total = length;
        used = response_length = 0u;
        operation = data[1];
        return NINLIL_OK;
    }
    if (data[0] == 1u && size > 3u && size <= 123u && operation) {
        uint16_t offset = (uint16_t)get(data + 1, 2u);
        size_t length = size - 3u;
        if (offset > used || offset > total || length > (size_t)total - offset)
            return NINLIL_ERR_INVALID;
        if (offset < used)
            return offset + length <= used &&
                           !memcmp(upload + offset, data + 3, length)
                       ? NINLIL_OK
                       : NINLIL_ERR_CONFLICT;
        memcpy(upload + used, data + 3, length);
        used = (uint16_t)(used + length);
        return NINLIL_OK;
    }
    if (data[0] == 2u && size == 1u && operation && used == total) {
        ninlil_node_member member;
        size_t length = 0u;
        int rc;
        response_length = 0u;
        if (operation == 1u || operation == 4u)
            rc = configure(n);
        else if (operation == 5u)
            rc = transfer(n);
        else if (operation == 2u) {
            ninlil_node *temporary = NULL;
            rc = ninlil_member_decode(upload, total, &member);
            if (rc == NINLIL_OK && !n) {
                rc = ninlil_node_open(&temporary, &node_config, 0u);
                n = temporary;
            }
            if (rc == NINLIL_OK)
                rc = ninlil_node_authorize(n, &member, response,
                                           sizeof(response), &length);
            ninlil_node_close(temporary);
        } else
            rc = ninlil_node_admit(n, upload, total);
        if (rc == NINLIL_OK)
            response_length = (uint16_t)length;
        return rc;
    }
    if (data[0] == 3u && size == 3u) {
        uint16_t offset = (uint16_t)get(data + 1, 2u);
        size_t length;
        if (offset > response_length)
            return NINLIL_ERR_INVALID;
        length = (size_t)response_length - offset;
        if (length > 120u)
            length = 120u;
        memcpy(out, response + offset, length);
        *written = length;
        return NINLIL_OK;
    }
    if (data[0] == 4u && size == 1u) {
        put(out, revision, 8u);
        out[8] = (uint8_t)autorun;
        put(out + 9, node_config.member_count ? node_config.local : 0u, 2u);
        put(out + 11, node_config.member_count ? node_config.root : 0u, 2u);
        put(out + 13, response_length, 2u);
        *written = 15u;
        return NINLIL_OK;
    }
    if (data[0] == 5u && size == 3u) {
        ninlil_node_member member;
        uint16_t address = (uint16_t)get(data + 1, 2u);
        int rc = NINLIL_ERR_NOT_FOUND;
        if (n)
            rc = ninlil_node_member_inspect(n, address, &member);
        else
            for (unsigned int i = 0u; i < node_config.member_count; i++)
                if (node_config.members[i].grant.node == address) {
                    member = node_config.members[i];
                    rc = NINLIL_OK;
                }
        response_length =
            rc == NINLIL_OK ? (uint16_t)ninlil_member_encode(&member, response,
                                                             sizeof(response))
                            : 0u;
        return rc;
    }
    return NINLIL_ERR_INVALID;
}
