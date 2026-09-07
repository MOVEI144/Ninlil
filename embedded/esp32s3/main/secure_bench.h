#ifndef NINLIL_SECURE_BENCH_H
#define NINLIL_SECURE_BENCH_H

#include "ninlil_edhoc.h"
#include "ninlil_sx1262_radio.h"

/* Explicit USB-owned lab only. Private credentials and exported session keys
 * never leave the MCU. Public-key pinning is performed by the bench operator.
 * No unsolicited TX, automatic provisioning, or production trust bootstrap. */
void ninlil_secure_bench(ninlil_sx1262_radio *radio);
int ninlil_bench_identity(uint8_t public_key[65]);
int ninlil_bench_handshake(ninlil_edhoc *h, const uint8_t peer[65],
                           uint64_t now_ms);

#endif
