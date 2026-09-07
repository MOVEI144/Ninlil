#include "ninlil_secure.h"

#include <string.h>

void ninlil_secret_clear(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length > 0u) {
        *bytes++ = 0u;
        length--;
    }
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void make_nonce(const uint8_t iv[13], uint64_t counter,
                       uint8_t nonce[13])
{
    unsigned int i;
    memcpy(nonce, iv, 13u);
    for (i = 0u; i < 5u; i++)
        nonce[12u - i] ^= (uint8_t)(counter >> (8u * i));
}

int ninlil_secure_open(ninlil_secure_session *s,
                       const ninlil_session_material *m,
                       ninlil_counter_store *counter, ninlil_aead aead,
                       uint16_t local, uint16_t peer, uint8_t direction)
{
    if (!s || !m || !counter || !aead.crypt || !counter->opened ||
        counter->poisoned || local == 0u || peer == 0u || local == UINT16_MAX ||
        peer == UINT16_MAX || local == peer || direction > 1u ||
        counter->config.direction != direction ||
        memcmp(counter->config.session_fingerprint, m->fingerprint, 16u) != 0 ||
        memcmp(m->keys[0], m->keys[1], 16u) == 0)
        return NINLIL_ERR_INVALID;
    memset(s, 0, sizeof(*s));
    s->material = *m;
    s->counter = counter;
    s->aead = aead;
    s->local = local;
    s->peer = peer;
    s->direction = direction;
    s->ready = 1u;
    return NINLIL_OK;
}

void ninlil_secure_close(ninlil_secure_session *s)
{
    if (s)
        ninlil_secret_clear(s, sizeof(*s));
}

static int seal_channel(ninlil_secure_session *s, uint8_t channel,
                        const uint8_t *plain, size_t length, uint8_t *frame,
                        size_t capacity, size_t *written)
{
    uint8_t scratch[NINLIL_SECURE_FRAME_MAX];
    uint8_t nonce[13];
    uint64_t counter;
    size_t encrypted = 0u;
    unsigned int i;
    int rc;

    if (!s || !s->ready || !plain || !frame || !written || length == 0u)
        return NINLIL_ERR_INVALID;
    if (length > NINLIL_SECURE_PLAINTEXT_MAX ||
        capacity < length + NINLIL_SECURE_OVERHEAD)
        return NINLIL_ERR_TOO_LARGE;
    // Reserve before encryption; an error consumes the nonce, never retries it.
    rc = ninlil_counter_next(s->counter, &counter);
    if (rc != NINLIL_OK) {
        ninlil_secure_close(s);
        return rc;
    }
    memset(scratch, 0, NINLIL_SECURE_HEADER);
    memcpy(scratch, "NS\001", 3u);
    scratch[3] = s->direction;
    scratch[31] = channel;
    put16(scratch + 4, s->local);
    put16(scratch + 6, s->peer);
    memcpy(scratch + 8, s->material.fingerprint, 16u);
    for (i = 0u; i < 5u; i++)
        scratch[28u - i] = (uint8_t)(counter >> (8u * i));
    put16(scratch + 29, (uint16_t)length);
    make_nonce(s->material.ivs[s->direction], counter, nonce);
    rc = s->aead.crypt(s->aead.ctx, 1, s->material.keys[s->direction], nonce,
                       scratch, NINLIL_SECURE_HEADER, plain, length,
                       scratch + NINLIL_SECURE_HEADER,
                       sizeof(scratch) - NINLIL_SECURE_HEADER, &encrypted);
    if (rc == NINLIL_OK && encrypted != length + NINLIL_SECURE_TAG)
        rc = NINLIL_ERR_FAULT;
    if (rc == NINLIL_OK) {
        memcpy(frame, scratch, NINLIL_SECURE_HEADER + encrypted);
        *written = NINLIL_SECURE_HEADER + encrypted;
    }
    ninlil_secret_clear(scratch, sizeof(scratch));
    ninlil_secret_clear(nonce, sizeof(nonce));
    return rc;
}

static int unseal_channel(ninlil_secure_session *s, uint8_t channel,
                          const uint8_t *frame, size_t length, uint8_t *plain,
                          size_t capacity, size_t *written)
{
    uint8_t scratch[NINLIL_SECURE_PLAINTEXT_MAX];
    uint8_t nonce[13];
    uint8_t direction;
    uint64_t counter = 0u;
    uint64_t delta;
    uint16_t size;
    size_t decrypted = 0u;
    unsigned int i;
    int rc;

    if (!s || !s->ready || !frame || !plain || !written)
        return NINLIL_ERR_INVALID;
    if (length <= NINLIL_SECURE_OVERHEAD || length > NINLIL_SECURE_FRAME_MAX ||
        memcmp(frame, "NS\001", 3u) != 0 || frame[31] != channel)
        return NINLIL_ERR_INVALID;
    direction = (uint8_t)(1u - s->direction);
    if (frame[3] != direction || get16(frame + 4) != s->peer ||
        get16(frame + 6) != s->local ||
        memcmp(frame + 8, s->material.fingerprint, 16u) != 0)
        return NINLIL_ERR_UNAUTHORIZED;
    size = get16(frame + 29);
    if (size != length - NINLIL_SECURE_OVERHEAD)
        return NINLIL_ERR_INVALID;
    if (capacity < size)
        return NINLIL_ERR_TOO_LARGE;
    for (i = 0u; i < 5u; i++)
        counter = (counter << 8) | frame[24u + i];
    if (s->rx_bitmap != 0u && counter <= s->rx_high) {
        delta = s->rx_high - counter;
        if (delta >= 64u || (s->rx_bitmap & (UINT64_C(1) << delta)) != 0u)
            return NINLIL_ERR_CONFLICT;
    }
    make_nonce(s->material.ivs[direction], counter, nonce);
    rc = s->aead.crypt(
        s->aead.ctx, 0, s->material.keys[direction], nonce, frame,
        NINLIL_SECURE_HEADER, frame + NINLIL_SECURE_HEADER,
        length - NINLIL_SECURE_HEADER, scratch, sizeof(scratch), &decrypted);
    if (rc == NINLIL_OK && decrypted != size)
        rc = NINLIL_ERR_FAULT;
    if (rc == NINLIL_OK) {
        if (s->rx_bitmap == 0u || counter > s->rx_high) {
            delta = counter - s->rx_high;
            s->rx_bitmap =
                delta >= 64u ? 1u : (s->rx_bitmap << delta) | UINT64_C(1);
            s->rx_high = counter;
        } else {
            s->rx_bitmap |= UINT64_C(1) << (s->rx_high - counter);
        }
        memcpy(plain, scratch, decrypted);
        *written = decrypted;
    }
    ninlil_secret_clear(scratch, sizeof(scratch));
    ninlil_secret_clear(nonce, sizeof(nonce));
    return rc;
}

int ninlil_secure_bind_membership(ninlil_secure_session *s,
                                  uint64_t local_epoch, uint64_t peer_epoch)
{
    if (!s || !s->ready || !local_epoch || !peer_epoch ||
        (s->local_membership_epoch &&
         s->local_membership_epoch != local_epoch) ||
        (s->peer_membership_epoch && s->peer_membership_epoch != peer_epoch))
        return NINLIL_ERR_STATE;
    s->local_membership_epoch = local_epoch;
    s->peer_membership_epoch = peer_epoch;
    return NINLIL_OK;
}

int ninlil_secure_seal(ninlil_secure_session *s, const uint8_t *plain,
                       size_t length, uint8_t *frame, size_t capacity,
                       size_t *written)
{
    return seal_channel(s, 0u, plain, length, frame, capacity, written);
}
int ninlil_secure_unseal(ninlil_secure_session *s, const uint8_t *frame,
                         size_t length, uint8_t *plain, size_t capacity,
                         size_t *written)
{
    return unseal_channel(s, 0u, frame, length, plain, capacity, written);
}
int ninlil_secure_seal_control(ninlil_secure_session *s, const uint8_t *plain,
                               size_t length, uint8_t *frame, size_t capacity,
                               size_t *written)
{
    return seal_channel(s, 1u, plain, length, frame, capacity, written);
}
int ninlil_secure_unseal_control(ninlil_secure_session *s, const uint8_t *frame,
                                 size_t length, uint8_t *plain, size_t capacity,
                                 size_t *written)
{
    return unseal_channel(s, 1u, frame, length, plain, capacity, written);
}
