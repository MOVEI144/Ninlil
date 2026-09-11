#ifndef NINLIL_SLEEP_H
#define NINLIL_SLEEP_H
#include "ninlil_node.h"
/* Exclusive owner boundary for RAM-retaining sleep. Pending Core data stays
 * durable; no receive, step or TX is allowed until resume. Battery Leaf only.
 * Caller finishes physical TX and application/storage work before suspend.
 * Wake time must include elapsed sleep. Power loss still uses ordinary open. */
int ninlil_node_suspend(ninlil_node *node, uint64_t now_ms);
int ninlil_node_resume(ninlil_node *node, uint64_t now_ms);
#endif
