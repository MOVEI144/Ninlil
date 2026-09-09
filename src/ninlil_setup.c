#include "ninlil_setup.h"
#include "ninlil_identity.h"
#include "ninlil_journal.h"
#include <stdlib.h>
#include <string.h>

struct ninlil_setup {
    ninlil_journal *journal;
    ninlil_identity *identity;
    ninlil_node_member members[2];
    uint8_t credential[NINLIL_ADMISSION_MAX];
    uint8_t authority_key[65], root_credential[NINLIL_ADMISSION_MAX];
    uint64_t revision;
    uint16_t length;
    uint16_t root_length;
    uint8_t transfer[NINLIL_DEPLOYMENT_PACKET_MAX];
    uint16_t transfer_length;
    uint8_t phase, count, autorun, poisoned;
};
static int packet_decode(ninlil_setup *s, const uint8_t *p, size_t length,
                         ninlil_setup *next);
static int match(ninlil_setup *s)
{
    const ninlil_node_member *local = &s->members[s->count - 1u];
    if (s->members[0].grant.role != NINLIL_ROLE_SITE_GATEWAY ||
        memcmp(local->grant.identity, s->identity->identity, 32u) ||
        memcmp(local->public_key, s->identity->public_key, 65u) ||
        memcmp(local->grant.authority, s->members[0].grant.authority, 16u) ||
        (s->count == 2u &&
         (local->grant.node == s->members[0].grant.node ||
          !memcmp(local->grant.identity, s->members[0].grant.identity, 32u) ||
          !memcmp(local->public_key, s->members[0].public_key, 65u))))
        return NINLIL_ERR_UNAUTHORIZED;
    for (unsigned int i = 0u; i < s->count; i++)
        for (unsigned int j = 0u; j < s->members[i].grant.service_count; j++)
            if (s->members[i].grant.services[j].maximum_payload_bytes > 64u)
                return NINLIL_ERR_TOO_LARGE;
    return NINLIL_OK;
}
static int replay(void *ctx, uint8_t type, const uint8_t *data, uint16_t length,
                  const ninlil_journal_ref *ref)
{
    ninlil_setup *s = ctx;
    (void)ref;
    if (type == 6u && (s->phase == 3u || s->phase == 5u) && length &&
        length <= 300u &&
        (size_t)s->transfer_length + length <= sizeof(s->transfer)) {
        memcpy(s->transfer + s->transfer_length, data, length);
        s->transfer_length = (uint16_t)(s->transfer_length + length);
        s->phase = 5u;
        return NINLIL_OK;
    }
    if (type == 7u && s->phase == 5u && length == 1u && data[0] == 1u) {
        ninlil_setup next = {0};
        if (packet_decode(s, s->transfer, s->transfer_length, &next) !=
                NINLIL_OK ||
            next.revision != s->revision + 1u)
            return NINLIL_ERR_CORRUPT;
        s->phase = 6u;
        return NINLIL_OK;
    }
    if (type == 4u && !s->phase && length == 65u && data[0] == 4u) {
        memcpy(s->authority_key, data, 65u);
        s->phase = 4u;
        return NINLIL_OK;
    }
    if (type == 5u && s->phase == 4u && length <= NINLIL_ADMISSION_MAX) {
        if (ninlil_admission_verify(s->authority_key, data, length,
                                    &s->members[0]) != NINLIL_OK ||
            s->members[0].grant.membership_epoch > UINT16_MAX)
            return NINLIL_ERR_CORRUPT;
        memcpy(s->root_credential, data, length);
        s->root_length = length;
        s->phase = s->count = 1u;
        return NINLIL_OK;
    }
    if (type == 1u && !s->phase) {
        if (ninlil_member_decode(data, length, &s->members[0]) != NINLIL_OK)
            return NINLIL_ERR_CORRUPT;
        s->phase = s->count = 1u;
        return NINLIL_OK;
    }
    if (type == 2u && s->phase == 1u) {
        if (length > sizeof(s->credential) ||
            ninlil_admission_verify(s->root_length ? s->authority_key
                                                   : s->members[0].public_key,
                                    data, length, &s->members[1]) != NINLIL_OK)
            return NINLIL_ERR_CORRUPT;
        memcpy(s->credential, data, length);
        s->length = length;
        s->count = s->phase = 2u;
        return NINLIL_OK;
    }
    if (type != 3u || (s->phase != 1u && s->phase != 2u) || length != 45u ||
        memcmp(data, "NCF\001", 4u) || data[12] > 1u ||
        memcmp(data + 13, s->identity->identity, 32u))
        return NINLIL_ERR_CORRUPT;
    for (unsigned int i = 4u; i < 12u; i++)
        s->revision = (s->revision << 8) | data[i];
    if (!s->revision || match(s) != NINLIL_OK)
        return NINLIL_ERR_CORRUPT;
    s->autorun = data[12];
    s->phase = 3u;
    return NINLIL_OK;
}
int ninlil_setup_open(ninlil_setup **out, const char *location,
                      ninlil_identity *identity)
{
    ninlil_setup *s;
    int rc;
    if (!out || !location || !identity || !identity->signing_key)
        return NINLIL_ERR_INVALID;
    *out = NULL;
    s = calloc(1u, sizeof(*s));
    if (!s)
        return NINLIL_ERR_CAPACITY;
    s->identity = identity;
    rc = ninlil_journal_open(&s->journal, location, 32768u, replay, s);
    if (rc == NINLIL_OK && ((s->phase && s->phase != 3u && s->phase != 6u) ||
                            ((identity->initialized & 2u) && !s->phase)))
        rc = NINLIL_ERR_CORRUPT;
    if (rc != NINLIL_OK) {
        ninlil_setup_close(s);
        return rc;
    }
    *out = s;
    return NINLIL_OK;
}
void ninlil_setup_close(ninlil_setup *s)
{
    if (s) {
        ninlil_journal_close(s->journal);
        free(s);
    }
}
static int snapshot(void *ctx, ninlil_journal *next)
{
    ninlil_setup *s = ctx;
    uint8_t bytes[NINLIL_MEMBER_RECORD_MAX], seal[45] = {'N', 'C', 'F', 1u};
    ninlil_journal_ref ref;
    size_t length = ninlil_member_encode(&s->members[0], bytes, sizeof(bytes));
    int rc;
    if (s->root_length) {
        rc = ninlil_journal_append(next, 4u, s->authority_key, 65u, &ref);
        if (rc == NINLIL_OK)
            rc = ninlil_journal_append(next, 5u, s->root_credential,
                                       s->root_length, &ref);
    } else
        rc = length ? ninlil_journal_append(next, 1u, bytes, (uint16_t)length,
                                            &ref)
                    : NINLIL_ERR_INVALID;
    if (rc == NINLIL_OK && s->length)
        rc = ninlil_journal_append(next, 2u, s->credential, s->length, &ref);
    for (unsigned int i = 0u; i < 8u; i++)
        seal[11u - i] = (uint8_t)(s->revision >> (i * 8u));
    seal[12] = s->autorun;
    memcpy(seal + 13, s->identity->identity, 32u);
    if (rc == NINLIL_OK)
        rc = ninlil_journal_append(next, 3u, seal, sizeof(seal), &ref);
    for (size_t offset = 0u; rc == NINLIL_OK && offset < s->transfer_length;
         offset += 300u) {
        size_t remaining = (size_t)s->transfer_length - offset;
        rc = ninlil_journal_append(
            next, 6u, s->transfer + offset,
            (uint16_t)(remaining > 300u ? 300u : remaining), &ref);
    }
    if (rc == NINLIL_OK && s->transfer_length)
        rc = ninlil_journal_append(next, 7u, (const uint8_t[]){1u}, 1u, &ref);
    return rc;
}
static int verify_current(ninlil_setup *s)
{
    ninlil_setup restored = {0};
    restored.identity = s->identity;
    int rc = ninlil_journal_visit(s->journal, replay, &restored);
    if (rc == NINLIL_OK &&
        (restored.revision != s->revision ||
         restored.transfer_length != s->transfer_length ||
         memcmp(restored.transfer, s->transfer, s->transfer_length)))
        rc = NINLIL_ERR_CORRUPT;
    if (rc != NINLIL_OK)
        s->poisoned = 1u;
    return rc;
}
static int update(ninlil_setup *s, uint64_t expected,
                  const ninlil_node_member *root, const uint8_t *data,
                  size_t length, int autorun, const uint8_t *authority,
                  const uint8_t *root_credential, size_t root_length)
{
    ninlil_setup next = {0};
    ninlil_setup previous = {0};
    uint8_t checked[NINLIL_MEMBER_RECORD_MAX];
    int rc;
    if (!s || !root || s->poisoned || s->transfer_length || (!data && length) ||
        length > NINLIL_ADMISSION_MAX || (autorun != 0 && autorun != 1) ||
        expected == UINT64_MAX || root_length > NINLIL_ADMISSION_MAX ||
        (root_length && (!authority || !root_credential)))
        return NINLIL_ERR_INVALID;
    previous.identity = s->identity;
    rc = ninlil_journal_visit(s->journal, replay, &previous);
    if (rc != NINLIL_OK || previous.revision != s->revision ||
        (previous.phase && previous.phase != 3u)) {
        s->poisoned = 1u;
        return rc != NINLIL_OK ? rc : NINLIL_ERR_CORRUPT;
    }
    if (expected != s->revision) {
        uint8_t old[NINLIL_MEMBER_RECORD_MAX];
        size_t first = ninlil_member_encode(root, checked, sizeof(checked));
        size_t second = ninlil_member_encode(&s->members[0], old, sizeof(old));
        return expected + 1u == s->revision && s->autorun == autorun &&
                       root_length == s->root_length &&
                       (!root_length ||
                        (!memcmp(authority, s->authority_key, 65u) &&
                         !memcmp(root_credential, s->root_credential,
                                 root_length))) &&
                       length == s->length && first && first == second &&
                       !memcmp(checked, old, first) &&
                       (!length || !memcmp(data, s->credential, length))
                   ? NINLIL_OK
                   : NINLIL_ERR_CONFLICT;
    }
    if (!ninlil_member_encode(root, checked, sizeof(checked)))
        return NINLIL_ERR_INVALID;
    next.identity = s->identity;
    next.members[0] = *root;
    if (root_length) {
        memcpy(next.authority_key, authority, 65u);
        memcpy(next.root_credential, root_credential, root_length);
        next.root_length = (uint16_t)root_length;
    }
    next.count = 1u;
    if (length) {
        rc = ninlil_admission_verify(root_length ? authority : root->public_key,
                                     data, length, &next.members[1]);
        if (rc != NINLIL_OK)
            return rc;
        next.count = 2u;
        memcpy(next.credential, data, length);
        next.length = (uint16_t)length;
    }
    rc = match(&next);
    if (rc != NINLIL_OK)
        return rc;
    next.revision = expected + 1u;
    next.autorun = (uint8_t)autorun;
    rc = ninlil_journal_rewrite(s->journal, snapshot, &next);
    if (rc == NINLIL_OK) {
        next.phase = 3u;
        next.journal = s->journal;
        *s = next;
    } else if (rc != NINLIL_ERR_CAPACITY && rc != NINLIL_ERR_NOT_FOUND)
        s->poisoned = 1u;
    return rc;
}
int ninlil_setup_update(ninlil_setup *s, uint64_t expected,
                        const ninlil_node_member *root, const uint8_t *data,
                        size_t length, int autorun)
{
    return update(s, expected, root, data, length, autorun, NULL, NULL, 0u);
}
int ninlil_setup_update_authority(ninlil_setup *s, uint64_t expected,
                                  const uint8_t authority[65],
                                  const uint8_t *root_data, size_t root_length,
                                  const uint8_t *data, size_t length,
                                  int autorun)
{
    ninlil_node_member root;
    int rc = ninlil_admission_verify(authority, root_data, root_length, &root);
    if (rc != NINLIL_OK || root.grant.membership_epoch > UINT16_MAX)
        return NINLIL_ERR_UNAUTHORIZED;
    return update(s, expected, &root, data, length, autorun, authority,
                  root_data, root_length);
}
int ninlil_setup_config(ninlil_setup *s, ninlil_node_config *c,
                        uint64_t *revision, int *autorun)
{
    ninlil_role_profile resources;
    int rc;
    if (!s || !c || !revision || !autorun || s->poisoned)
        return NINLIL_ERR_STATE;
    *revision = s->revision;
    *autorun = s->autorun;
    if (!s->revision)
        return NINLIL_ERR_EMPTY;
    rc = ninlil_role_profile_standard(s->members[s->count - 1u].grant.role,
                                      &resources);
    if (rc != NINLIL_OK)
        return rc;
    c->local = s->members[s->count - 1u].grant.node;
    c->root = s->members[0].grant.node;
    c->members = s->members;
    c->member_count = s->count;
    c->resources = resources;
    c->dynamic_enrollment = 1u;
    c->authority_key = s->root_length ? s->authority_key : NULL;
    c->offline = s->transfer_length ? 1u : 0u;
    if (s->transfer_length)
        *autorun = 0;
    return s->transfer_length ? NINLIL_ERR_BUSY : NINLIL_OK;
}
int ninlil_setup_advertise(ninlil_setup *s, ninlil_node *n)
{
    if (!s || s->poisoned || !s->revision || s->transfer_length)
        return NINLIL_ERR_STATE;
    return s->length ? ninlil_node_advertise(n, s->credential, s->length)
           : s->root_length
               ? ninlil_node_advertise(n, s->root_credential, s->root_length)
               : NINLIL_OK;
}
static int packet_decode(ninlil_setup *s, const uint8_t *p, size_t length,
                         ninlil_setup *next)
{
    uint64_t expected = 0u;
    size_t root_length;
    if (!s || !p || length < 77u || length > NINLIL_DEPLOYMENT_PACKET_MAX ||
        p[8] > 1u)
        return NINLIL_ERR_INVALID;
    for (unsigned int i = 0; i < 8u; i++)
        expected = (expected << 8) | p[i];
    root_length = ((size_t)p[74] << 8) | p[75];
    if (expected == UINT64_MAX || !root_length ||
        root_length > NINLIL_ADMISSION_MAX || root_length > length - 76u ||
        length - 76u - root_length > NINLIL_ADMISSION_MAX)
        return NINLIL_ERR_INVALID;
    next->identity = s->identity;
    next->revision = expected + 1u;
    next->autorun = p[8];
    memcpy(next->authority_key, p + 9, 65u);
    memcpy(next->root_credential, p + 76, root_length);
    next->root_length = (uint16_t)root_length;
    next->length = (uint16_t)(length - 76u - root_length);
    if (ninlil_admission_verify(p + 9, p + 76, root_length,
                                &next->members[0]) != NINLIL_OK ||
        next->members[0].grant.membership_epoch > UINT16_MAX)
        return NINLIL_ERR_UNAUTHORIZED;
    next->count = 1u;
    if (next->length) {
        memcpy(next->credential, p + 76u + root_length, next->length);
        if (ninlil_admission_verify(p + 9, next->credential, next->length,
                                    &next->members[1]) != NINLIL_OK)
            return NINLIL_ERR_UNAUTHORIZED;
        next->count = 2u;
    }
    return match(next);
}
int ninlil_setup_packet(ninlil_setup *s, const uint8_t *p, size_t length)
{
    ninlil_setup next = {0};
    int rc = packet_decode(s, p, length, &next);
    return rc == NINLIL_OK ? update(s, next.revision - 1u, &next.members[0],
                                    next.credential, next.length, next.autorun,
                                    next.authority_key, next.root_credential,
                                    next.root_length)
                           : rc;
}
int ninlil_setup_transfer_begin(ninlil_setup *s, const uint8_t *p,
                                size_t length)
{
    ninlil_setup next = {0};
    int rc;
    if (!s || s->poisoned || !s->revision)
        return NINLIL_ERR_STATE;
    rc = packet_decode(s, p, length, &next);
    if (rc != NINLIL_OK)
        return rc;
    if (s->transfer_length)
        return length == s->transfer_length && !memcmp(p, s->transfer, length)
                   ? NINLIL_OK
                   : NINLIL_ERR_CONFLICT;
    if (next.revision != s->revision + 1u ||
        next.members[next.count - 1u].grant.binding_epoch <=
            s->members[s->count - 1u].grant.binding_epoch)
        return NINLIL_ERR_CONFLICT;
    rc = verify_current(s);
    if (rc != NINLIL_OK)
        return rc;
    memcpy(s->transfer, p, length);
    s->transfer_length = (uint16_t)length;
    rc = ninlil_journal_rewrite(s->journal, snapshot, s);
    if (rc != NINLIL_OK)
        s->poisoned = 1u;
    return rc;
}
int ninlil_setup_transfer_pending(const ninlil_setup *s)
{
    return s && !s->poisoned && s->transfer_length;
}
int ninlil_setup_transfer_finish(ninlil_setup *s)
{
    ninlil_setup next = {0};
    int rc;
    if (!s || s->poisoned || !s->transfer_length)
        return NINLIL_ERR_STATE;
    rc = verify_current(s);
    if (rc == NINLIL_OK)
        rc = packet_decode(s, s->transfer, s->transfer_length, &next);
    if (rc == NINLIL_OK)
        rc = ninlil_journal_rewrite(s->journal, snapshot, &next);
    if (rc == NINLIL_OK) {
        next.phase = 3u;
        next.journal = s->journal;
        *s = next;
    } else
        s->poisoned = 1u;
    return rc;
}
int ninlil_setup_autorun(ninlil_setup *s, int autorun)
{
    if (!s || s->poisoned || !s->revision || (autorun != 0 && autorun != 1))
        return NINLIL_ERR_STATE;
    return update(s, s->autorun == autorun ? s->revision - 1u : s->revision,
                  &s->members[0], s->credential, s->length, autorun,
                  s->authority_key, s->root_credential, s->root_length);
}
