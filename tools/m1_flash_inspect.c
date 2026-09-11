/* Read-only validation of the exact 128 KiB M1 device-journal image.
 * Output is provisional until the final VALIDATED marker and exit status 0.
 * The production Flash parser validates checksums and commit markers. */
#include "ninlil_flash_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SIZE (128u * 1024u)

static int read_image(void *ctx, size_t offset, uint8_t *buffer, size_t length)
{
    const uint8_t *image = ctx;

    if (offset > IMAGE_SIZE || length > IMAGE_SIZE - offset)
        return -1;
    memcpy(buffer, image + offset, length);
    return 0;
}

static int refuse_write(void *ctx, size_t offset, const uint8_t *buffer,
                        size_t length)
{
    (void)ctx;
    (void)offset;
    (void)buffer;
    (void)length;
    return -1;
}

static int refuse_erase(void *ctx, size_t offset, size_t length)
{
    (void)ctx;
    (void)offset;
    (void)length;
    return -1;
}

static int print_record(void *ctx, uint8_t type, const uint8_t *payload,
                        uint16_t length, size_t offset)
{
    unsigned int *count = ctx;
    uint16_t index;

    (*count)++;
    printf("RECORD type=%u offset=%zu data=", (unsigned int)type, offset);
    for (index = 0u; index < length; index++)
        printf("%02x", (unsigned int)payload[index]);
    return putchar('\n') == EOF ? NINLIL_ERR_IO : NINLIL_OK;
}

int main(int argc, char **argv)
{
    uint8_t *image;
    FILE *file;
    ninlil_flash_store store;
    ninlil_flash_io io;
    unsigned int count = 0u;
    int rc;

    if (argc != 2)
        return 2;
    file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    image = malloc(IMAGE_SIZE);
    if (!image) {
        (void)fclose(file);
        return 2;
    }
    rc = fread(image, 1u, IMAGE_SIZE, file) == IMAGE_SIZE &&
                 fgetc(file) == EOF && !ferror(file)
             ? 0
             : 2;
    if (fclose(file) != 0)
        rc = 2;
    if (rc == 0) {
        memset(&io, 0, sizeof(io));
        io.read = read_image;
        io.write = refuse_write;
        io.erase = refuse_erase;
        io.ctx = image;
        io.size = IMAGE_SIZE;
        rc = ninlil_flash_store_open(&store, &io, print_record, &count);
        if (rc == NINLIL_OK)
            printf("VALIDATED records=%u next_sequence=%lu append_offset=%zu\n",
                   count, (unsigned long)store.next_sequence,
                   store.append_offset);
    }
    free(image);
    return rc == 0 && fflush(stdout) == 0 ? 0 : 1;
}
