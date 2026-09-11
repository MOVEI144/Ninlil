#include "ninlil_join.h"
#include "ninlil_network.h"
#include "ninlil_relay.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t length);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t length)
{
    ninlil_join_record join;
    ninlil_network_plan plan;
    ninlil_relay_record relay;
    uint8_t output[1024], input[1024], again[1024];
    size_t size;
    unsigned int kind;
    if (length > sizeof(input))
        return 0;
    for (kind = 0u; kind < 5u; kind++) {
        memcpy(input, data, length);
        if (length >= 3u && kind > 0u) {
            static const uint8_t magic[4][3] = {
                {'N', 'J', 1}, {'N', 'P', 1}, {'N', 'R', 1}, {'N', 'R', 2}};
            memcpy(input, magic[kind - 1u], 3u);
        }
        if (ninlil_join_decode(input, length, &join) == NINLIL_OK) {
            size = ninlil_join_encode(&join, output, sizeof(output));
            if (!size || ninlil_join_decode(output, size, &join) != NINLIL_OK ||
                ninlil_join_encode(&join, again, sizeof(again)) != size ||
                memcmp(output, again, size))
                abort();
        }
        if (ninlil_network_plan_decode(input, length, &plan) == NINLIL_OK) {
            size = ninlil_network_plan_encode(&plan, output, sizeof(output));
            if (!size ||
                ninlil_network_plan_decode(output, size, &plan) != NINLIL_OK)
                abort();
        }
        if (ninlil_relay_decode(input, length, &relay) == NINLIL_OK) {
            size = ninlil_relay_encode(&relay, output, sizeof(output));
            if (!size || ninlil_relay_decode(output, size, &relay) != NINLIL_OK)
                abort();
        }
    }
    return 0;
}
