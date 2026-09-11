#ifndef NINLIL_JOURNAL_H
#define NINLIL_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct ninlil_journal ninlil_journal;

typedef struct ninlil_journal_ref {
    uint64_t offset;
    uint32_t generation;
    uint16_t length;
    uint8_t type;
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
/* Maintenance only, with no concurrent/reentrant journal operations. Visit
 * verifies every committed record. Rewrite publishes a complete replacement
 * only after snapshot succeeds; stage relocated references until success.
 * Ambiguous publication requires closing and reopening. */
typedef int (*ninlil_journal_snapshot)(void *ctx, ninlil_journal *replacement);
int ninlil_journal_visit(ninlil_journal *journal,
                         ninlil_journal_on_record on_record, void *ctx);
int ninlil_journal_rewrite(ninlil_journal *journal,
                           ninlil_journal_snapshot snapshot, void *ctx);
int ninlil_journal_usage(ninlil_journal *journal, uint64_t *used,
                         uint64_t *capacity);
void ninlil_journal_close(ninlil_journal *journal);

#endif
