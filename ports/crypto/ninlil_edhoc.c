#include "ninlil_edhoc.h"
#include "edhoc_cipher_suite_2.h"

#include <stdatomic.h>
#include <string.h>

// libedhoc's custom allocator has no user-context parameter. This platform
// bridge serializes calls explicitly and clears its finite scratch workspace.
#define ARENA_SIZE 16384u
static union {
    max_align_t alignment;
    uint8_t bytes[ARENA_SIZE];
} arena;
static atomic_flag arena_lock = ATOMIC_FLAG_INIT;
static size_t arena_used;

void *edhoc_mem_alloc(size_t size);
void edhoc_mem_free(void *ptr);

#define ARENA_BLOCK 256u
#define ARENA_BLOCKS (ARENA_SIZE / ARENA_BLOCK)
static uint16_t arena_runs[ARENA_BLOCKS];

void *edhoc_mem_alloc(size_t size)
{
    size_t count, i, j;
    if (size > ARENA_SIZE)
        return NULL;
    // Empty EDHOC message-4 plaintext still requests a freeable buffer.
    if (size == 0u)
        size = 1u;
    count = (size + ARENA_BLOCK - 1u) / ARENA_BLOCK;
    for (i = 0u; i + count <= ARENA_BLOCKS; i++) {
        for (j = 0u; j < count && arena_runs[i + j] == 0u; j++) {
        }
        if (j != count)
            continue;
        arena_runs[i] = (uint16_t)count;
        for (j = 1u; j < count; j++)
            arena_runs[i + j] = UINT16_MAX;
        if ((i + count) * ARENA_BLOCK > arena_used)
            arena_used = (i + count) * ARENA_BLOCK;
        memset(arena.bytes + i * ARENA_BLOCK, 0, count * ARENA_BLOCK);
        return arena.bytes + i * ARENA_BLOCK;
    }
    return NULL;
}

void edhoc_mem_free(void *ptr)
{
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)arena.bytes;
    size_t i, count, j;
    if (!ptr || address < base || address - base >= ARENA_SIZE ||
        (address - base) % ARENA_BLOCK != 0u)
        return;
    i = (size_t)(address - base) / ARENA_BLOCK;
    count = arena_runs[i];
    if (count == 0u || count > ARENA_BLOCKS - i)
        return;
    ninlil_secret_clear(ptr, count * ARENA_BLOCK);
    for (j = 0u; j < count; j++)
        arena_runs[i + j] = 0u;
}

static int enter(void)
{
    if (atomic_flag_test_and_set(&arena_lock))
        return NINLIL_ERR_BUSY;
    arena_used = 0u;
    return NINLIL_OK;
}

static void leave(void)
{
    ninlil_secret_clear(arena.bytes, arena_used);
    arena_used = 0u;
    memset(arena_runs, 0, sizeof(arena_runs));
    atomic_flag_clear(&arena_lock);
}

static int fetch(void *ctx, struct edhoc_auth_creds *credential)
{
    ninlil_edhoc *h = ctx;
    return h->config.credentials.fetch(h->config.credential_ctx, credential);
}

static int verify(void *ctx, struct edhoc_auth_creds *credential,
                  const uint8_t **key, size_t *key_length)
{
    ninlil_edhoc *h = ctx;
    uint8_t identity[32];
    int rc = h->config.credentials.verify(h->config.credential_ctx, credential,
                                          key, key_length);
    if (rc != EDHOC_SUCCESS || !*key || *key_length != 65u ||
        h->config.peer_identity(h->config.credential_ctx, identity) !=
            NINLIL_OK ||
        memcmp(identity, h->config.expected_peer, 32u) != 0)
        return EDHOC_ERROR_CREDENTIALS_FAILURE;
    memcpy(h->peer_identity, identity, sizeof(identity));
    return EDHOC_SUCCESS;
}

static int compose_ead(void *ctx, enum edhoc_message message,
                       struct edhoc_ead_token *tokens, size_t capacity,
                       size_t *length)
{
    (void)ctx;
    (void)message;
    (void)tokens;
    (void)capacity;
    *length = 0u;
    return EDHOC_SUCCESS;
}

static int process_ead(void *ctx, enum edhoc_message message,
                       const struct edhoc_ead_token *tokens, size_t count)
{
    (void)ctx;
    (void)message;
    (void)tokens;
    /* This profile negotiates no EAD extensions, critical or otherwise. */
    return count == 0u ? EDHOC_SUCCESS : EDHOC_ERROR_NOT_SUPPORTED;
}

int ninlil_edhoc_open(ninlil_edhoc *h, const ninlil_edhoc_config *config,
                      uint64_t now_ms)
{
    const enum edhoc_method method = EDHOC_METHOD_0;
    const struct edhoc_credentials credentials = {.fetch = fetch,
                                                  .verify = verify};
    const struct edhoc_ead ead = {.compose = compose_ead,
                                  .process = process_ead};
    struct edhoc_connection_id cid;
    int rc;
    if (!h || !config || config->initiator > 1u || !config->credentials.fetch ||
        !config->credentials.verify || !config->peer_identity ||
        config->connection_id < -24 || config->connection_id > 23)
        return NINLIL_ERR_INVALID;
    rc = enter();
    if (rc != NINLIL_OK)
        return rc;
    memset(h, 0, sizeof(*h));
    h->config = *config;
    h->started_ms = now_ms;
    memset(&cid, 0, sizeof(cid));
    cid.encode_type = EDHOC_CID_TYPE_ONE_BYTE_INTEGER;
    cid.int_value = config->connection_id;
    rc = edhoc_context_init(&h->context);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_set_methods(&h->context, &method, 1u);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_set_cipher_suites(&h->context,
                                     edhoc_cipher_suite_2_get_suite(), 1u);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_set_connection_id(&h->context, &cid);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_set_user_context(&h->context, h);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_bind_keys(&h->context, edhoc_cipher_suite_2_get_keys());
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_bind_crypto(&h->context, edhoc_cipher_suite_2_get_crypto());
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_bind_credentials(&h->context, &credentials);
    if (rc == EDHOC_SUCCESS)
        rc = edhoc_bind_ead(&h->context, &ead);
    h->opened = rc == EDHOC_SUCCESS ? 1u : 0u;
    leave();
    if (!h->opened) {
        ninlil_edhoc_close(h);
        return NINLIL_ERR_FAULT;
    }
    return NINLIL_OK;
}

static int advance(ninlil_edhoc *h, const uint8_t *input, size_t length)
{
    int rc = EDHOC_ERROR_BAD_STATE;
    if (h->config.initiator) {
        if (h->step == 0u && length == 0u)
            rc = edhoc_message_1_compose(&h->context, h->last_output,
                                         sizeof(h->last_output),
                                         &h->output_length);
        else if (h->step == 1u) {
            rc = edhoc_message_2_process(&h->context, input, length);
            if (rc == EDHOC_SUCCESS)
                rc = edhoc_message_3_compose(&h->context, h->last_output,
                                             sizeof(h->last_output),
                                             &h->output_length);
        } else if (h->step == 2u) {
            rc = edhoc_message_4_process(&h->context, input, length);
            h->output_length = 0u;
        }
    } else {
        if (h->step == 0u) {
            rc = edhoc_message_1_process(&h->context, input, length);
            if (rc == EDHOC_SUCCESS)
                rc = edhoc_message_2_compose(&h->context, h->last_output,
                                             sizeof(h->last_output),
                                             &h->output_length);
        } else if (h->step == 1u) {
            rc = edhoc_message_3_process(&h->context, input, length);
            if (rc == EDHOC_SUCCESS)
                rc = edhoc_message_4_compose(&h->context, h->last_output,
                                             sizeof(h->last_output),
                                             &h->output_length);
        }
    }
    h->upstream_error = rc;
    if (rc != EDHOC_SUCCESS)
        return NINLIL_ERR_UNAUTHORIZED;
    h->step++;
    if (h->step == (h->config.initiator ? 3u : 2u)) {
        uint8_t material[74];
        // IANA's 32768..65535 range is private use. One Ninlil profile label
        // derives independent direction keys, IVs and a session fingerprint.
        rc = edhoc_export_prk_exporter(&h->context, 32768u, material,
                                       sizeof(material));
        h->upstream_error = rc;
        if (rc == EDHOC_SUCCESS) {
            memcpy(h->material.keys, material, 32u);
            memcpy(h->material.ivs, material + 32, 26u);
            memcpy(h->material.fingerprint, material + 58, 16u);
            rc = edhoc_export_prk_exporter(&h->context, 32769u, material,
                                           sizeof(material));
            h->upstream_error = rc;
            if (rc == EDHOC_SUCCESS) {
                memcpy(h->hop_material.keys, material, 32u);
                memcpy(h->hop_material.ivs, material + 32, 26u);
                memcpy(h->hop_material.fingerprint, material + 58, 16u);
                h->authenticated = 1u;
            }
        }
        ninlil_secret_clear(material, sizeof(material));
        if (rc != EDHOC_SUCCESS)
            return NINLIL_ERR_UNAUTHORIZED;
    }
    return NINLIL_OK;
}

int ninlil_edhoc_exchange(ninlil_edhoc *h, const uint8_t *input, size_t length,
                          uint64_t now_ms, uint8_t *output, size_t capacity,
                          size_t *written)
{
    int rc;
    if (!h || !h->opened || (!input && length != 0u) || !output || !written ||
        length > NINLIL_EDHOC_MESSAGE_MAX)
        return NINLIL_ERR_INVALID;
    // Require maximum capacity before mutating the handshake or generating
    // keys.
    if (capacity < NINLIL_EDHOC_MESSAGE_MAX)
        return NINLIL_ERR_TOO_LARGE;
    if (now_ms < h->started_ms ||
        now_ms - h->started_ms > NINLIL_EDHOC_DEADLINE_MS) {
        ninlil_edhoc_close(h);
        return NINLIL_ERR_TIMEOUT;
    }
    if (h->step > 0u &&
        (length == 0u || (length == h->input_length &&
                          memcmp(input, h->last_input, length) == 0))) {
        if (h->retries >= NINLIL_EDHOC_RETRY_MAX) {
            ninlil_edhoc_close(h);
            return NINLIL_ERR_TIMEOUT;
        }
        h->retries++;
        memcpy(output, h->last_output, h->output_length);
        *written = h->output_length;
        return NINLIL_OK;
    }
    rc = enter();
    if (rc != NINLIL_OK)
        return rc;
    rc = advance(h, input, length);
    leave();
    if (rc != NINLIL_OK) {
        int upstream_error = h->upstream_error;
        ninlil_edhoc_close(h);
        h->upstream_error = upstream_error;
        return rc;
    }
    if (length != 0u)
        memcpy(h->last_input, input, length);
    h->input_length = length;
    h->retries = 0u;
    memcpy(output, h->last_output, h->output_length);
    *written = h->output_length;
    return NINLIL_OK;
}

int ninlil_edhoc_material(const ninlil_edhoc *h,
                          ninlil_session_material *material,
                          uint8_t peer_identity[32])
{
    if (!h || !h->opened || !h->authenticated || !material || !peer_identity)
        return NINLIL_ERR_STATE;
    *material = h->material;
    memcpy(peer_identity, h->peer_identity, 32u);
    return NINLIL_OK;
}

int ninlil_edhoc_hop_material(const ninlil_edhoc *h,
                              ninlil_session_material *material)
{
    if (!h || !h->opened || !h->authenticated || !material)
        return NINLIL_ERR_STATE;
    *material = h->hop_material;
    return NINLIL_OK;
}

void ninlil_edhoc_close(ninlil_edhoc *h)
{
    if (h) {
        (void)edhoc_context_deinit(&h->context);
        ninlil_secret_clear(h, sizeof(*h));
    }
}
