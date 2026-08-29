#include "ninlil_custody.h"

#include <string.h>

static int id_equal(const ninlil_id *left, const ninlil_id *right)
{
    return memcmp(left->bytes, right->bytes, NINLIL_ID_BYTES) == 0;
}

static int id_valid(const ninlil_id *id)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < NINLIL_ID_BYTES; index++)
        combined |= id->bytes[index];
    return combined != 0u;
}

static int evidence_valid(ninlil_evidence evidence)
{
    return evidence == NINLIL_EVIDENCE_HOST_ADAPTER_STORED ||
           evidence == NINLIL_EVIDENCE_GATEWAY_CUSTODY ||
           evidence == NINLIL_EVIDENCE_REMOTE_STORED;
}

int ninlil_custody_reconnect_delay(ninlil_custody_reconnect_kind kind,
                                   uint8_t retry_count, uint32_t *delay_ms)
{
    uint32_t initial;
    uint8_t shift;
    uint32_t candidate;

    if (!delay_ms || (kind != NINLIL_CUSTODY_RECONNECT_HOST &&
                      kind != NINLIL_CUSTODY_RECONNECT_GATEWAY))
        return NINLIL_ERR_INVALID;
    initial = kind == NINLIL_CUSTODY_RECONNECT_HOST
                  ? NINLIL_HOST_RECONNECT_INITIAL_MS
                  : NINLIL_GATEWAY_RECONNECT_INITIAL_MS;
    shift = retry_count > 7u ? 7u : retry_count;
    candidate = initial << shift;
    *delay_ms = candidate > NINLIL_RECONNECT_MAX_MS ? NINLIL_RECONNECT_MAX_MS
                                                    : candidate;
    return NINLIL_OK;
}

static int entry_same(const ninlil_custody_entry *left,
                      const ninlil_custody_entry *right)
{
    return id_equal(&left->message_id, &right->message_id) &&
           left->payload_token == right->payload_token &&
           left->route_epoch == right->route_epoch &&
           left->payload_bytes == right->payload_bytes &&
           left->peer == right->peer && left->evidence == right->evidence &&
           left->used == right->used && left->terminal == right->terminal;
}

static ninlil_custody_entry *find_entry(ninlil_custody_spool *spool,
                                        const ninlil_id *id)
{
    uint16_t index;

    for (index = 0u; index < spool->capacity; index++) {
        if (spool->entries[index].used &&
            id_equal(&spool->entries[index].message_id, id))
            return &spool->entries[index];
    }
    return NULL;
}

static ninlil_custody_entry *find_free(ninlil_custody_spool *spool)
{
    uint16_t index;

    for (index = 0u; index < spool->capacity; index++) {
        if (!spool->entries[index].used)
            return &spool->entries[index];
    }
    return NULL;
}

static int durable_commit(ninlil_custody_spool *spool,
                          ninlil_custody_record_type type,
                          const ninlil_custody_entry *entry)
{
    int rc = spool->commit(spool->commit_ctx, type, entry);

    if (rc != NINLIL_OK)
        spool->poisoned = 1u;
    return rc;
}

int ninlil_custody_open(ninlil_custody_spool *spool,
                        ninlil_custody_entry *entries, uint16_t capacity,
                        uint64_t byte_limit, uint16_t pending_per_peer,
                        ninlil_custody_commit commit, void *commit_ctx)
{
    if (!spool || !entries || !commit || capacity == 0u ||
        capacity > NINLIL_HOST_SPOOL_MAX_MESSAGES || byte_limit == 0u ||
        byte_limit > NINLIL_HOST_SPOOL_MAX_BYTES || pending_per_peer == 0u ||
        pending_per_peer > NINLIL_HOST_PENDING_PER_PEER_MAX)
        return NINLIL_ERR_INVALID;
    memset(spool, 0, sizeof(*spool));
    memset(entries, 0, (size_t)capacity * sizeof(*entries));
    spool->entries = entries;
    spool->capacity = capacity;
    spool->byte_limit = byte_limit;
    spool->pending_per_peer = pending_per_peer;
    spool->commit = commit;
    spool->commit_ctx = commit_ctx;
    return NINLIL_OK;
}

static uint16_t peer_live(const ninlil_custody_spool *spool, uint16_t peer);

static int restored_entry_valid(const ninlil_custody_entry *entry)
{
    return entry && entry->used == 1u && id_valid(&entry->message_id) &&
           entry->peer > 0u && entry->peer < UINT16_MAX &&
           entry->payload_token != 0u && entry->route_epoch > 0u &&
           evidence_valid(entry->evidence) &&
           entry->terminal ==
               (uint8_t)(entry->evidence == NINLIL_EVIDENCE_REMOTE_STORED);
}

int ninlil_custody_restore(ninlil_custody_spool *spool,
                           ninlil_custody_record_type record_type,
                           const ninlil_custody_entry *entry)
{
    ninlil_custody_entry *slot;
    ninlil_custody_entry expected;

    if (!spool || spool->poisoned || !restored_entry_valid(entry))
        return NINLIL_ERR_INVALID;
    slot = find_entry(spool, &entry->message_id);
    if (record_type == NINLIL_CUSTODY_RECORD_ADMIT) {
        if (entry->terminal ||
            (entry->evidence != NINLIL_EVIDENCE_HOST_ADAPTER_STORED &&
             entry->evidence != NINLIL_EVIDENCE_GATEWAY_CUSTODY))
            return NINLIL_ERR_CORRUPT;
        if (slot)
            return entry_same(slot, entry) ? NINLIL_OK : NINLIL_ERR_CORRUPT;
        slot = find_free(spool);
        if (!slot)
            return NINLIL_ERR_CAPACITY;
        if (spool->live >= spool->capacity ||
            peer_live(spool, entry->peer) >= spool->pending_per_peer ||
            spool->live_bytes > spool->byte_limit ||
            entry->payload_bytes > spool->byte_limit - spool->live_bytes)
            return NINLIL_ERR_CAPACITY;
        *slot = *entry;
        spool->live++;
        spool->live_bytes += entry->payload_bytes;
        return NINLIL_OK;
    }
    if (!slot)
        return NINLIL_ERR_CORRUPT;
    if (record_type == NINLIL_CUSTODY_RECORD_EVIDENCE) {
        expected = *slot;
        if (entry->evidence <= slot->evidence || slot->terminal)
            return NINLIL_ERR_CORRUPT;
        expected.evidence = entry->evidence;
        expected.terminal =
            (uint8_t)(entry->evidence == NINLIL_EVIDENCE_REMOTE_STORED);
        if (!entry_same(&expected, entry))
            return NINLIL_ERR_CORRUPT;
        *slot = *entry;
        if (entry->terminal) {
            spool->live--;
            spool->live_bytes -= entry->payload_bytes;
        }
        return NINLIL_OK;
    }
    if (record_type == NINLIL_CUSTODY_RECORD_ROUTE) {
        expected = *slot;
        if (slot->terminal || entry->route_epoch <= slot->route_epoch)
            return NINLIL_ERR_CORRUPT;
        expected.route_epoch = entry->route_epoch;
        if (!entry_same(&expected, entry))
            return NINLIL_ERR_CORRUPT;
        *slot = *entry;
        return NINLIL_OK;
    }
    if (record_type == NINLIL_CUSTODY_RECORD_FORGET) {
        if (!slot->terminal || !entry_same(slot, entry))
            return NINLIL_ERR_CORRUPT;
        memset(slot, 0, sizeof(*slot));
        return NINLIL_OK;
    }
    return NINLIL_ERR_CORRUPT;
}

static uint16_t peer_live(const ninlil_custody_spool *spool, uint16_t peer)
{
    uint16_t index;
    uint16_t count = 0u;

    for (index = 0u; index < spool->capacity; index++) {
        if (spool->entries[index].used && !spool->entries[index].terminal &&
            spool->entries[index].peer == peer)
            count++;
    }
    return count;
}

int ninlil_custody_admit(ninlil_custody_spool *spool, const ninlil_id *id,
                         uint16_t peer, uint32_t payload_bytes,
                         uint64_t payload_token, uint64_t route_epoch,
                         ninlil_evidence local_custody)
{
    ninlil_custody_entry candidate;
    ninlil_custody_entry *existing;
    ninlil_custody_entry *slot;
    int rc;

    if (!spool || !id || !id_valid(id) || spool->poisoned || peer == 0u ||
        peer == UINT16_MAX || payload_token == 0u || route_epoch == 0u ||
        (local_custody != NINLIL_EVIDENCE_HOST_ADAPTER_STORED &&
         local_custody != NINLIL_EVIDENCE_GATEWAY_CUSTODY))
        return NINLIL_ERR_INVALID;
    memset(&candidate, 0, sizeof(candidate));
    candidate.message_id = *id;
    candidate.peer = peer;
    candidate.payload_bytes = payload_bytes;
    candidate.payload_token = payload_token;
    candidate.route_epoch = route_epoch;
    candidate.evidence = local_custody;
    candidate.used = 1u;
    existing = find_entry(spool, id);
    if (existing)
        return entry_same(existing, &candidate) ? NINLIL_OK
                                                : NINLIL_ERR_CONFLICT;
    slot = find_free(spool);
    if (!slot || spool->live >= spool->capacity ||
        peer_live(spool, peer) >= spool->pending_per_peer ||
        spool->live_bytes > spool->byte_limit ||
        payload_bytes > spool->byte_limit - spool->live_bytes)
        return NINLIL_ERR_CAPACITY;
    rc = durable_commit(spool, NINLIL_CUSTODY_RECORD_ADMIT, &candidate);
    if (rc != NINLIL_OK)
        return rc;
    *slot = candidate;
    spool->live++;
    spool->live_bytes += payload_bytes;
    return NINLIL_OK;
}

int ninlil_custody_note_evidence(ninlil_custody_spool *spool,
                                 const ninlil_id *id, ninlil_evidence evidence)
{
    ninlil_custody_entry *entry;
    ninlil_custody_entry candidate;
    int rc;

    if (!spool || !id || !id_valid(id) || spool->poisoned ||
        (evidence != NINLIL_EVIDENCE_GATEWAY_CUSTODY &&
         evidence != NINLIL_EVIDENCE_REMOTE_STORED))
        return NINLIL_ERR_INVALID;
    entry = find_entry(spool, id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (entry->evidence == evidence)
        return NINLIL_OK;
    if (entry->terminal ||
        (entry->evidence == NINLIL_EVIDENCE_GATEWAY_CUSTODY &&
         evidence != NINLIL_EVIDENCE_REMOTE_STORED))
        return NINLIL_ERR_STATE;
    candidate = *entry;
    candidate.evidence = evidence;
    candidate.terminal = (uint8_t)(evidence == NINLIL_EVIDENCE_REMOTE_STORED);
    rc = durable_commit(spool, NINLIL_CUSTODY_RECORD_EVIDENCE, &candidate);
    if (rc != NINLIL_OK)
        return rc;
    *entry = candidate;
    if (candidate.terminal) {
        spool->live--;
        spool->live_bytes -= candidate.payload_bytes;
    }
    return NINLIL_OK;
}

int ninlil_custody_update_route(ninlil_custody_spool *spool,
                                const ninlil_id *id, uint64_t route_epoch)
{
    ninlil_custody_entry *entry;
    ninlil_custody_entry candidate;
    int rc;

    if (!spool || !id || !id_valid(id) || spool->poisoned || route_epoch == 0u)
        return NINLIL_ERR_INVALID;
    entry = find_entry(spool, id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (entry->route_epoch == route_epoch)
        return NINLIL_OK;
    if (entry->terminal || route_epoch < entry->route_epoch)
        return NINLIL_ERR_STATE;
    candidate = *entry;
    candidate.route_epoch = route_epoch;
    rc = durable_commit(spool, NINLIL_CUSTODY_RECORD_ROUTE, &candidate);
    if (rc == NINLIL_OK)
        *entry = candidate;
    return rc;
}

int ninlil_custody_replay_next(const ninlil_custody_spool *spool,
                               uint16_t *cursor, ninlil_custody_entry *entry)
{
    if (!spool || !cursor || !entry || spool->poisoned ||
        *cursor > spool->capacity)
        return NINLIL_ERR_INVALID;
    while (*cursor < spool->capacity) {
        const ninlil_custody_entry *candidate = &spool->entries[*cursor];

        (*cursor)++;
        if (candidate->used && !candidate->terminal) {
            *entry = *candidate;
            return NINLIL_OK;
        }
    }
    return NINLIL_ERR_EMPTY;
}

int ninlil_custody_forget(ninlil_custody_spool *spool, const ninlil_id *id)
{
    ninlil_custody_entry *entry;
    int rc;

    if (!spool || !id || !id_valid(id) || spool->poisoned)
        return NINLIL_ERR_INVALID;
    entry = find_entry(spool, id);
    if (!entry)
        return NINLIL_ERR_NOT_FOUND;
    if (!entry->terminal)
        return NINLIL_ERR_STATE;
    rc = durable_commit(spool, NINLIL_CUSTODY_RECORD_FORGET, entry);
    if (rc == NINLIL_OK)
        memset(entry, 0, sizeof(*entry));
    return rc;
}
