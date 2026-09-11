#ifndef NINLIL_BULK_H
#define NINLIL_BULK_H
#include "ninlil.h"
#define NINLIL_BULK_MAX 65536u
#define NINLIL_BULK_CHUNK 40u
typedef struct ninlil_bulk ninlil_bulk;
typedef struct ninlil_bulk_manifest {
    ninlil_id id;
    uint32_t length;
    uint8_t sha256[32];
} ninlil_bulk_manifest;
typedef struct ninlil_bulk_status {
    ninlil_bulk_manifest manifest;
    uint32_t stored_bytes;
    uint16_t acknowledged_frames;
    uint8_t sending, ready, remote_stored;
} ninlil_bulk_status;
/* One durable object per dedicated 256 KiB logical journal. peer/service are
 * provisioned, authenticated Core grants must permit 64-byte BULK messages.
 * Exclusively consume that service's BULK class on Core's execution owner.
 * Other classes may have separate owners using receive_class. Requires a
 * powered role with BULK slots; the additional object index uses <28 KiB RAM.
 * Stores are not silently retired: retain until the application has adopted
 * the object and its external retention contract allows retiring the store.
 * A source body is copied before sending. Same identity/different bytes fails.
 * ready means complete, hash-verified bytes; remote_stored is bulk-service
 * adoption, never business application success. No OTA execution is provided.
 */
int ninlil_bulk_open(ninlil_bulk **out, const char *location, uint16_t peer,
                     uint16_t service, int sending);
void ninlil_bulk_close(ninlil_bulk *bulk);
int ninlil_bulk_begin(ninlil_bulk *bulk, const ninlil_bulk_manifest *manifest);
int ninlil_bulk_write(ninlil_bulk *bulk, uint32_t offset, const uint8_t *data,
                      uint16_t length);
int ninlil_bulk_seal(ninlil_bulk *bulk);
int ninlil_bulk_read(ninlil_bulk *bulk, uint32_t offset, uint8_t *data,
                     uint16_t length);
/* One inbound adoption and one outbound opportunity per call; Core owns
 * retries/custody. Reopen resumes the same object after loss/restart. */
int ninlil_bulk_step(ninlil_bulk *bulk, ninlil_runtime *core);
int ninlil_bulk_query(ninlil_bulk *bulk, ninlil_bulk_status *status);
int ninlil_bulk_collect(ninlil_bulk *bulk);
#endif
