#ifndef NINLIL_NETWORK_INTERNAL_H
#define NINLIL_NETWORK_INTERNAL_H
#include "ninlil_network.h"
ninlil_network_flow *ninlil_network_flow_find(ninlil_coordinator *c,
                                              const ninlil_network_path *p,
                                              int create);
int ninlil_network_snapshot_epochs(ninlil_coordinator *c,
                                   ninlil_network_path *p);
int ninlil_network_policy(ninlil_coordinator *c, uint16_t node, int relay,
                          uint64_t epoch);
uint64_t ninlil_network_path_cost(ninlil_coordinator *c,
                                  const ninlil_network_path *path,
                                  uint64_t now);
#endif
