#include "sim.h"

#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    sim_manifest before, after;
    FILE *file;
    int rc;
    if (size > 8192u)
        return 0;
    file = tmpfile();
    if (!file)
        abort();
    memset(&before, 0xA5, sizeof(before));
    after = before;
    if (fwrite(data, 1u, size, file) != size || fseek(file, 0L, SEEK_SET) != 0)
        abort();
    rc = sim_manifest_read(file, &after);
    if (fclose(file) != 0)
        abort();
    if (rc == NINLIL_OK) {
        if (sim_manifest_validate(&after) != NINLIL_OK)
            abort();
    } else if (memcmp(&before, &after, sizeof(before)) != 0) {
        abort();
    }
    return 0;
}
