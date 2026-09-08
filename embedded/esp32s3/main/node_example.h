#ifndef NINLIL_NODE_EXAMPLE_H
#define NINLIL_NODE_EXAMPLE_H
#include "ninlil_identity.h"
#include "ninlil_journal.h"
#include "ninlil_node.h"
#define NODE_EXAMPLE_APP_MAX 64u
extern ninlil_identity node_identity;
extern ninlil_node_config node_config;
int node_storage_open(void);
int node_storage_provision(void);
int node_application_open(const char *location, int provision);
void node_application_close(void);
int node_application_step(ninlil_runtime *core);
int node_application_submit(ninlil_runtime *core, uint16_t target,
                            uint32_t sequence, ninlil_id *id);
uint16_t node_application_count(void);
#endif
