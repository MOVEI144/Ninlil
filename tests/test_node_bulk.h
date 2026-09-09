#ifndef TEST_NODE_BULK_H
#define TEST_NODE_BULK_H
#include "ninlil_node.h"
void test_node_bulk(ninlil_node *source, ninlil_node **receiver,
                    void (*tick)(void), void (*restart_receiver)(void));
#endif
