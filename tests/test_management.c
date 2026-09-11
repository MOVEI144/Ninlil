/* Compile the real USB consumer: build-time IDs are not installed settings. */
#include "../embedded/esp32s3/main/node_management.c"
#include <stdio.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
ninlil_identity node_identity;
ninlil_node_config node_config;
int node_storage_provision(void)
{
    return NINLIL_ERR_FAULT;
}
int main(void)
{
    uint8_t reply[128], request = 4u;
    size_t length;
    node_config.local = node_config.root = 1u;
    CHECK(node_management(NULL, &request, 1u, reply, &length) == NINLIL_OK);
    CHECK(length == 15u && get(reply + 9, 2u) == 0u &&
          get(reply + 11, 2u) == 0u);
    node_config.member_count = 1u;
    CHECK(node_management(NULL, &request, 1u, reply, &length) == NINLIL_OK);
    CHECK(get(reply + 9, 2u) == 1u && get(reply + 11, 2u) == 1u);
    node_identity.initialized = 3u;
    CHECK(node_management(NULL, &request, 1u, reply, &length) ==
          NINLIL_ERR_CORRUPT);
    CHECK(!length);
    puts("Unconfigured IDs, configured IDs and missing deployment fence PASS");
    return 0;
}
