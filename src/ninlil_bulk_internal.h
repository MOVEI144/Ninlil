#ifndef NINLIL_BULK_INTERNAL_H
#define NINLIL_BULK_INTERNAL_H
#include "ninlil_bulk.h"
#include "ninlil_journal.h"
#define BULK_PARTS                                                             \
    ((NINLIL_BULK_MAX + NINLIL_BULK_CHUNK - 1u) / NINLIL_BULK_CHUNK)
struct ninlil_bulk {
    ninlil_journal *journal;
    ninlil_bulk_status state;
    ninlil_journal_ref parts[BULK_PARTS];
    ninlil_inbound pending;
    uint16_t peer, service;
    uint8_t begun, offered;
    int fault;
};
uint32_t ninlil_bulk_get(const uint8_t *data, unsigned int size);
void ninlil_bulk_put(uint8_t *data, uint32_t value, unsigned int size);
int ninlil_bulk_ack(ninlil_bulk *bulk, uint16_t cursor);
int ninlil_bulk_digest(ninlil_bulk *bulk, uint8_t digest[32]);
#endif
