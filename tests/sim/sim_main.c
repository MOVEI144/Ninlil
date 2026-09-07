#include "sim.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    sim_manifest manifest;
    sim_run *run;
    FILE *input;
    int rc;
    if (argc != 2) {
        fprintf(stderr, "usage: ninlil_sim manifest.txt\n");
        return 1;
    }
    input = fopen(argv[1], "rb");
    if (!input)
        return 1;
    rc = sim_manifest_read(input, &manifest);
    if (fclose(input) != 0)
        rc = NINLIL_ERR_IO;
    if (rc != NINLIL_OK) {
        fprintf(stderr, "invalid manifest (no runtime opened)\n");
        return 1;
    }
    run = calloc(1u, sizeof(*run));
    if (!run)
        return 1;
    rc = sim_run_open(run, &manifest);
    while (rc == NINLIL_OK &&
           run->network.now_us < (uint64_t)manifest.duration_ms * 1000u)
        rc = sim_run_tick(run);
    if (rc == NINLIL_OK)
        rc = sim_run_finish(run, stdout);
    else
        fprintf(stderr, "simulation invariant/error=%d\n", rc);
    sim_run_close(run);
    free(run);
    return rc == NINLIL_OK ? 0 : rc == NINLIL_ERR_TIMEOUT ? 2 : 1;
}
