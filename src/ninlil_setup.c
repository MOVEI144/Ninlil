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
    uint64_t revision;
    uint16_t length;
    uint8_t phase, count, autorun, poisoned;
};
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
    if (type == 1u && !s->phase) {
        if (ninlil_member_decode(data, length, &s->members[0]) != NINLIL_OK)
            return NINLIL_ERR_CORRUPT;
        s->phase = s->count = 1u;
        return NINLIL_OK;
    }
    if (type == 2u && s->phase == 1u) {
        if (length > sizeof(s->credential) ||
            ninlil_admission_verify(s->members[0].public_key, data, length,
                                    &s->members[1]) != NINLIL_OK)
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
    if (rc == NINLIL_OK && ((s->phase && s->phase != 3u) ||
                            (identity->initialized == 3u && !s->phase)))
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
    int rc =
        length ? ninlil_journal_append(next, 1u, bytes, (uint16_t)length, &ref)
               : NINLIL_ERR_INVALID;
    if (rc == NINLIL_OK && s->length)
        rc = ninlil_journal_append(next, 2u, s->credential, s->length, &ref);
    for (unsigned int i = 0u; i < 8u; i++)
        seal[11u - i] = (uint8_t)(s->revision >> (i * 8u));
    seal[12] = s->autorun;
    memcpy(seal + 13, s->identity->identity, 32u);
    return rc == NINLIL_OK
               ? ninlil_journal_append(next, 3u, seal, sizeof(seal), &ref)
               : rc;
}
int ninlil_setup_update(ninlil_setup *s, uint64_t expected,
                        const ninlil_node_member *root, const uint8_t *data,
                        size_t length, int autorun)
{
    ninlil_setup next = {0};
    ninlil_setup previous = {0};
    uint8_t checked[NINLIL_MEMBER_RECORD_MAX];
    int rc;
    if (!s || !root || s->poisoned || (!data && length) ||
        length > NINLIL_ADMISSION_MAX || (autorun != 0 && autorun != 1) ||
        expected == UINT64_MAX)
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
    next.count = 1u;
    if (length) {
        rc = ninlil_admission_verify(root->public_key, data, length,
                                     &next.members[1]);
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
    return NINLIL_OK;
}
int ninlil_setup_advertise(ninlil_setup *s, ninlil_node *n)
{
    if (!s || s->poisoned || !s->revision)
        return NINLIL_ERR_STATE;
    return s->length ? ninlil_node_advertise(n, s->credential, s->length)
                     : NINLIL_OK;
}
int ninlil_setup_autorun(ninlil_setup *s, int autorun)
{
    if (!s || s->poisoned || !s->revision || (autorun != 0 && autorun != 1))
        return NINLIL_ERR_STATE;
    return s->autorun == autorun
               ? ninlil_setup_update(s, s->revision - 1u, &s->members[0],
                                     s->credential, s->length, autorun)
               : ninlil_setup_update(s, s->revision, &s->members[0],
                                     s->credential, s->length, autorun);
}
