#ifndef NINLIL_FANOUT_INTERNAL_H
#define NINLIL_FANOUT_INTERNAL_H
#include "ninlil_fanout.h"
/* Shared semantic validation for the owner and its persistent codec. No IO. */
int ninlil_fanout_contract_valid(const ninlil_fanout_contract *contract);
int ninlil_fanout_contract_equal(const ninlil_fanout_contract *a,
                                 const ninlil_fanout_contract *b);
int ninlil_fanout_target_equal(const ninlil_fanout_target *a,
                               const ninlil_fanout_target *b);
int ninlil_fanout_snapshot_valid(const ninlil_fanout_target *targets,
                                 uint16_t count, const uint8_t source[32]);
int ninlil_fanout_record_check(const ninlil_fanout *owner,
                               const ninlil_fanout_record *record);
#endif
