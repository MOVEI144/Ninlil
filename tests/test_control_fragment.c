#include "ninlil_control_fragment.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check %d: %s\n", __LINE__, #x);                   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    ninlil_control_reassembly s;
    uint8_t input[1024], output[1024], frames[5][240];
    size_t sizes[5], i, written = 777u;
    ninlil_control_reassembly_clear(&s);
    for (i = 0u; i < sizeof(input); i++)
        input[i] = (uint8_t)i;
    memset(output, 0xA5, sizeof(output));
    for (i = 0u; i < 5u; i++) {
        sizes[i] = ninlil_control_fragment(123u, 2u, input, sizeof(input),
                                           (uint8_t)i, frames[i], 240u);
        CHECK(sizes[i] > 0u);
    }
    for (i = 4u; i > 0u; i--)
        CHECK(ninlil_control_reassemble(&s, frames[i], sizes[i], 100u, output,
                                        sizeof(output),
                                        &written) == NINLIL_ERR_EMPTY);
    CHECK(written == 777u && output[0] == 0xA5);
    CHECK(ninlil_control_reassemble(&s, frames[4], sizes[4], 101u, output,
                                    sizeof(output),
                                    &written) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_control_reassemble(&s, frames[0], sizes[0], 102u, output,
                                    sizeof(output), &written) == NINLIL_OK);
    CHECK(written == sizeof(input) &&
          memcmp(input, output, sizeof(input)) == 0);
    frames[0][20] ^= 1u;
    CHECK(ninlil_control_reassemble(&s, frames[0], sizes[0], 103u, output,
                                    sizeof(output),
                                    &written) == NINLIL_ERR_CONFLICT);
    CHECK(s.poisoned);
    ninlil_control_reassembly_clear(&s);
    CHECK(ninlil_control_reassemble(&s, frames[4], sizes[4], 100u, output,
                                    sizeof(output),
                                    &written) == NINLIL_ERR_EMPTY);
    CHECK(ninlil_control_reassemble(&s, frames[0], sizes[0], 5101u, output,
                                    sizeof(output),
                                    &written) == NINLIL_ERR_TIMEOUT);
    for (i = 0u; i < sizeof(frames[0]); i++) {
        ninlil_control_reassembly_clear(&s);
        (void)ninlil_control_reassemble(&s, frames[0], i, 200u, output,
                                        sizeof(output), &written);
    }
    puts("bounded control fragments reorder/duplicate/conflict/expiry/length "
         "PASS");
    return 0;
}
