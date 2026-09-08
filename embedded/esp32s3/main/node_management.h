#ifndef NINLIL_NODE_MANAGEMENT_H
#define NINLIL_NODE_MANAGEMENT_H
#include "ninlil_node.h"
int node_deployment_open(void);
int node_deployment_start(ninlil_node *node);
int node_deployment_autorun(void);
int node_deployment_disarm(void);
int node_management(ninlil_node *node, const uint8_t *data, size_t size,
                    uint8_t *out, size_t *written);
#endif
