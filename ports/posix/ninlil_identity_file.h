#ifndef NINLIL_IDENTITY_FILE_H
#define NINLIL_IDENTITY_FILE_H
#include "ninlil_identity.h"

typedef struct ninlil_identity_file {
    int directory_fd;
    int lock_fd;
} ninlil_identity_file;

/* Existing or newly created private directory (0700, current owner).
 * Holds an exclusive process lock until close. No network/OS trust discovery.
 */
int ninlil_identity_file_open(ninlil_identity_file *file, const char *directory,
                              ninlil_identity_io *io);
void ninlil_identity_file_close(ninlil_identity_file *file);
#endif
