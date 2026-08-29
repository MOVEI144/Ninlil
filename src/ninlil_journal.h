#ifndef NINLIL_JOURNAL_H
#define NINLIL_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct ninlil_journal ninlil_journal;

typedef struct ninlil_journal_ref {
    uint64_t offset;
    uint16_t length;
} ninlil_journal_ref;

typedef int (*ninlil_journal_on_record)(void *ctx, uint8_t type,
                                        const uint8_t *payload, uint16_t length,
                                        const ninlil_journal_ref *reference);

int ninlil_journal_open(ninlil_journal **out, const char *location,
                        uint64_t maximum_bytes,
                        ninlil_journal_on_record on_record, void *ctx);
int ninlil_journal_append(ninlil_journal *journal, uint8_t type,
                          const uint8_t *payload, uint16_t length,
                          ninlil_journal_ref *reference);
int ninlil_journal_read(ninlil_journal *journal,
                        const ninlil_journal_ref *reference,
                        uint16_t relative_offset, uint8_t *buffer,
                        uint16_t length);
void ninlil_journal_close(ninlil_journal *journal);

#endif
