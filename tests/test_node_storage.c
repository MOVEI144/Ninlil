#include "security_test_io.h"
#include <stdio.h>
#include <stdlib.h>
#define ESP_PLATFORM 1
#define CONFIG_NINLIL_NODE_ID 1
#include "../embedded/esp32s3/main/node_storage.c"
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
static flash identity_flash_fixture, era_flash_fixture;
int node_deployment_open(void)
{
    return NINLIL_OK; /* This test isolates loss of the existing Root era store.
                       */
}
int ninlil_esp_security_region(ninlil_esp_security_partition *context,
                               ninlil_security_io *io, const char *label,
                               size_t offset, size_t size)
{
    flash *f;
    (void)context;
    if (offset || size != 8192u)
        return NINLIL_ERR_INVALID;
    if (strcmp(label, "node_identity") == 0)
        f = &identity_flash_fixture;
    else if (strcmp(label, "node_era") == 0)
        f = &era_flash_fixture;
    else
        return NINLIL_ERR_INVALID;
    *io = (ninlil_security_io){read_flash, write_flash, erase_flash, f,
                               sizeof(f->bytes)};
    return NINLIL_OK;
}
int ninlil_esp_network_emit(void *ctx, uint16_t next,
                            ninlil_traffic_class traffic, const uint8_t *frame,
                            size_t size)
{
    (void)ctx;
    (void)next;
    (void)traffic;
    (void)frame;
    (void)size;
    return NINLIL_ERR_FAULT;
}
int node_application_open(const char *location, int provision)
{
    (void)location;
    (void)provision;
    return NINLIL_ERR_FAULT;
}
void node_application_close(void)
{
}
int main(void)
{
    int rc;
    memset(identity_flash_fixture.bytes, 255,
           sizeof(identity_flash_fixture.bytes));
    memset(era_flash_fixture.bytes, 255, sizeof(era_flash_fixture.bytes));
    CHECK(node_storage_open() == NINLIL_ERR_EMPTY);
    CHECK(node_storage_provision() == NINLIL_OK);
    CHECK(ninlil_identity_mark_initialized(&node_identity) == NINLIL_OK);
    ninlil_identity_close(&node_identity);
    ninlil_counter_close(&era_store);
    memset(era_flash_fixture.bytes, 255, sizeof(era_flash_fixture.bytes));
    rc = node_storage_open();
    CHECK(rc == NINLIL_ERR_CORRUPT || rc == NINLIL_ERR_NOT_FOUND);
    CHECK(node_storage_provision() == NINLIL_ERR_CORRUPT);
    for (size_t i = 0u; i < sizeof(era_flash_fixture.bytes); i++)
        CHECK(era_flash_fixture.bytes[i] == 255u);
    ninlil_identity_close(&node_identity);
    /* A device that has never been Root may create its first physical era
     * under a CA credential. The irreversible marker then prevents recreation.
     */
    memset(identity_flash_fixture.bytes, 255,
           sizeof(identity_flash_fixture.bytes));
    CHECK(node_storage_open() == NINLIL_ERR_EMPTY);
    node_config.local = 2u;
    CHECK(node_storage_provision() == NINLIL_OK);
    CHECK(ninlil_identity_mark_initialized(&node_identity) == NINLIL_OK);
    CHECK(ninlil_identity_mark_deployed(&node_identity) == NINLIL_OK);
    node_config.local = node_config.root = 1u;
    node_config.authority_key = node_identity.public_key;
    CHECK(eras(1) == NINLIL_OK && node_identity.initialized == 7u);
    ninlil_counter_close(&era_store);
    memset(era_flash_fixture.bytes, 255, sizeof(era_flash_fixture.bytes));
    CHECK(eras(1) == NINLIL_ERR_CORRUPT);
    ninlil_identity_close(&node_identity);
    puts("established identity cannot recreate a lost root boot-era history "
         "PASS");
    return 0;
}
