#include "ninlil_control_fragment.h"

#include <string.h>

void ninlil_control_reassembly_clear(ninlil_control_reassembly *s)
{
    if (s)
        memset(s, 0, sizeof(*s));
}

size_t ninlil_control_fragment(uint64_t exchange, uint8_t kind,
                               const uint8_t *message, size_t length,
                               uint8_t index, uint8_t *frame, size_t capacity)
{
    size_t count, offset, size, i;
    uint8_t output[240];
    if (!exchange || kind < 1u || kind > 4u || !message || !frame ||
        length == 0u || length > NINLIL_CONTROL_MESSAGE_MAX)
        return 0u;
    count = (length + NINLIL_CONTROL_FRAGMENT_BODY - 1u) /
            NINLIL_CONTROL_FRAGMENT_BODY;
    if (index >= count)
        return 0u;
    offset = (size_t)index * NINLIL_CONTROL_FRAGMENT_BODY;
    size = length - offset;
    if (size > NINLIL_CONTROL_FRAGMENT_BODY)
        size = NINLIL_CONTROL_FRAGMENT_BODY;
    if (capacity < NINLIL_CONTROL_FRAGMENT_HEADER + size)
        return 0u;
    memcpy(output, "NF\001", 3u);
    output[3] = kind;
    output[4] = index;
    output[5] = (uint8_t)count;
    output[6] = (uint8_t)(length >> 8);
    output[7] = (uint8_t)length;
    for (i = 0u; i < 8u; i++)
        output[15u - i] = (uint8_t)(exchange >> (i * 8u));
    memcpy(output + NINLIL_CONTROL_FRAGMENT_HEADER, message + offset, size);
    memcpy(frame, output, NINLIL_CONTROL_FRAGMENT_HEADER + size);
    return NINLIL_CONTROL_FRAGMENT_HEADER + size;
}

int ninlil_control_reassemble(ninlil_control_reassembly *s,
                              const uint8_t *frame, size_t length, uint64_t now,
                              uint8_t *message, size_t capacity,
                              size_t *written)
{
    uint64_t token = 0u;
    uint16_t total;
    uint8_t index, count, bit;
    size_t offset, size, i;
    if (!s || !frame || !message || !written || s->poisoned)
        return NINLIL_ERR_INVALID;
    if (length <= NINLIL_CONTROL_FRAGMENT_HEADER || length > 240u ||
        memcmp(frame, "NF\001", 3u) != 0 || frame[3] < 1u || frame[3] > 4u)
        return NINLIL_ERR_INVALID;
    index = frame[4];
    count = frame[5];
    total = (uint16_t)(((uint16_t)frame[6] << 8) | frame[7]);
    if (!total || total > NINLIL_CONTROL_MESSAGE_MAX || !count ||
        count > NINLIL_CONTROL_FRAGMENT_MAX ||
        count != (total + NINLIL_CONTROL_FRAGMENT_BODY - 1u) /
                     NINLIL_CONTROL_FRAGMENT_BODY ||
        index >= count)
        return NINLIL_ERR_INVALID;
    offset = (size_t)index * NINLIL_CONTROL_FRAGMENT_BODY;
    size = total - offset;
    if (size > NINLIL_CONTROL_FRAGMENT_BODY)
        size = NINLIL_CONTROL_FRAGMENT_BODY;
    if (length != NINLIL_CONTROL_FRAGMENT_HEADER + size)
        return NINLIL_ERR_INVALID;
    if (capacity < total)
        return NINLIL_ERR_TOO_LARGE;
    for (i = 8u; i < 16u; i++)
        token = (token << 8) | frame[i];
    if (!token)
        return NINLIL_ERR_INVALID;
    if (s->exchange && (now < s->began_ms || now - s->began_ms > 5000u)) {
        ninlil_control_reassembly_clear(s);
        return NINLIL_ERR_TIMEOUT;
    }
    if (s->exchange && (s->exchange != token || s->kind != frame[3]))
        return NINLIL_ERR_BUSY;
    if (s->exchange && (s->length != total || s->count != count)) {
        s->poisoned = 1u;
        return NINLIL_ERR_CONFLICT;
    }
    if (!s->exchange) {
        s->exchange = token;
        s->kind = frame[3];
        s->length = total;
        s->count = count;
        s->began_ms = now;
    }
    bit = (uint8_t)(1u << index);
    if ((s->received & bit) != 0u &&
        memcmp(s->bytes + offset, frame + NINLIL_CONTROL_FRAGMENT_HEADER,
               size) != 0) {
        s->poisoned = 1u;
        return NINLIL_ERR_CONFLICT;
    }
    memcpy(s->bytes + offset, frame + NINLIL_CONTROL_FRAGMENT_HEADER, size);
    s->received |= bit;
    if (s->received != (uint8_t)((1u << count) - 1u))
        return NINLIL_ERR_EMPTY;
    memcpy(message, s->bytes, total);
    *written = total;
    return NINLIL_OK;
}
