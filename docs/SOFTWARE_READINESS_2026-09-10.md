# Software integration checkpoint — NOT implementation-complete

## Identity and purpose

This checkpoint starts from the exact tree of upstream commit
`b38878e79f1e7b28ecbc195762395b1d67ab6475`, restores the complete retained
identity-bound Core patch, and reconnects the surviving service-spool module.
The upstream tree and commit object were reproduced byte-for-byte from the
archived source and GitHub metadata; their Git object hashes match upstream.
The new commit is local, not a GitHub publication or a merged release.

## Implemented in this source tree

The cumulative observation, confirmed-power feedback, bounded fixed-PHY route
candidate selection, durable Group journal and identity-bound Core integration
are present. The added service cache separates a configurable retained outbound
index (up to 1024 entries) from at most 32 current service opportunities. It
retains the existing traffic-class admission reserves, per-peer service grants,
Flash ceiling, message identity, binding checks, attempted flag and evidence.

New configuration is explicit: all-zero `ninlil_config.spool` retains the
standard profile. An enabled configuration must declare retained/total ownership,
service slots and RAM budget; inconsistent limits fail before journal creation.
Open allocates finite arrays; service operations do not allocate per packet.
`ninlil_node_config.spool` passes this configuration to the real Core.

Service state is boot-local scheduling state, not a delivery outcome or durable
operator cancellation. Failed or deferred service returns the opportunity but
keeps custody. Reopen rebuilds the service cache from retained records. A smaller
configuration which cannot replay all records is refused; no entries are evicted.
The cold cursor examines at most 64 entries per selection. Resume does not
promise immediate service or override retry intervals, grants or deadlines.

`ninlil_service_pause` now also blocks already-staged DATA at the real
`ninlil_transmit_check` boundary. It does not block receipt processing, release
ownership or change the original deadline. Finalization clears the old slot's
boot-local service metadata before reuse. Reopen clears the nonpersistent pause
as documented; applications requiring persistent holds must reapply their own
saved policy before driving the Core.

All consumers must rebuild: `NINLIL_CONFIG_API_VERSION=3` and
`NINLIL_NODE_CONFIG_API_VERSION=4`. This increment adds no journal opcode. The
restored binding patch already introduced record types 11/12 and bound create v6;
older binaries must not open stores containing those records. No erase/downgrade
workaround is supplied.

## Tests and boundaries

`tests/test_service_spool.c` links the actual Core, binding, serializer, collection
and POSIX/raw-Flash-model journals. A named Link model returns BUSY for an
already-admitted prefix of 32 contracts. All 511 contracts remain ACTIVE while
the other 479 receive a Link opportunity. This is not 511 physical radios or
proof of remote Application acceptance. Exact IDs and bindings are rechecked
after collection and reopen; a replacement identity cannot receive the old work.

Additional tests cover staged-DATA pause, receipt processing while paused,
resumed transmission validation, reused service slots, invalid RAM/resource
configurations, no journal side effect on invalid configuration, and smaller
reopen limits. The receipt-only test injects a canonical receipt at the Core's
trusted ingress; it does not test RF authentication. Existing full SDK tests
remain in place. Raw logs, JUnit and source hashes accompany the checkpoint.
No compiler warning, corruption check, test or size ceiling was weakened.

## Unfinished implementation — merge blockers

1. The autonomous owner still has the 16-member / 32-flow structure. The service
   backlog is not an implementation of the planned DOMAIN512 registration,
   session, neighborhood and routing architecture.
2. The most recent coordinated-PHY work is not available as a complete source
   tree in this execution. Only six PHY C units and a model test log survived;
   the matching public/private headers, program codec/validation, call-site
   edits and tests are missing from the available snapshot. They are preserved
   unchanged in the delivery's `recovery-inputs/`, not copied into this build.
   Neither those fragments nor the old PASS log prove this tree's PHY feature.
3. Autonomous measurement/exploration of alternate PHY profiles and joint
   route/PHY/MAC capacity optimization are not implementation-complete here.
4. The unchanged 50,000-nonblank-line project ceiling and applicable scoped
   ceilings are release gates. This checkpoint exceeds them. A passing native
   CTest matrix cannot override their failure.
5. Actual ESP-IDF configure/build/link and final firmware RAM/Flash verification
   have not been run in this environment. Native ESP syntax tests use stubs.

Seven-board physical HIL, electrical power interruption, radio calibration and
field qualification remain separate, unrun gates. There is no flashable release
artifact, completed-feature declaration, remote PR or merge in this checkpoint.

## Remote state

The available GitHub connector exposed reading/searching but no create/push/merge
actions during this task. Discovery of another usable write connection did not
find one. No token, workflow-permission or branch-protection bypass was attempted.
The accompanying PR description is a draft text, NOT a created pull request.
