#include "ninlil_maintenance.h"
#include "ninlil_internal.h"
#include <string.h>
int ninlil_peer_idle(ninlil_runtime *r, uint16_t peer)
{
    int rc = ninlil_verify_retained(r);
    if (rc != NINLIL_OK)
        return rc;
    for (uint16_t i = 0u; i < r->outbound_capacity; i++)
        if (r->outbound[i].used && (!peer || r->outbound[i].target == peer))
            return NINLIL_ERR_BUSY;
    for (uint16_t i = 0u; i < r->inbound_capacity; i++)
        if (r->inbound[i].used && (!peer || r->inbound[i].source == peer))
            return NINLIL_ERR_BUSY;
    for (uint16_t i = 0u; i < r->archive_capacity; i++)
        if (r->archive[i].used && r->archive[i].need_receipt &&
            (!peer || r->archive[i].peer == peer))
            return NINLIL_ERR_BUSY;
    for (uint16_t i = 0u; i < r->rejection_capacity; i++)
        if (r->rejections[i].used && r->rejections[i].pending &&
            (!peer || r->rejections[i].target == peer))
            return NINLIL_ERR_BUSY;
    return NINLIL_OK;
}
static int binding_only(void *ctx, ninlil_journal *next)
{
    ninlil_runtime *r = ctx;
    uint8_t binding[33] = {1u};
    memcpy(binding + 1, r->storage_identity, 32u);
    return ninlil_journal_append(next, NINLIL_JRN_STORAGE_BINDING, binding,
                                 sizeof(binding), NULL);
}
int ninlil_retire_completed(ninlil_runtime *r)
{
    int rc = ninlil_peer_idle(r, 0u);
    if (rc != NINLIL_OK)
        return rc;
    if (!r->storage_bound)
        return NINLIL_ERR_STATE;
    rc = ninlil_journal_rewrite(r->journal, binding_only, r);
    /* Either publication or an ambiguous write requires authoritative reopen.
     */
    if (rc == NINLIL_OK ||
        (rc != NINLIL_ERR_CAPACITY && rc != NINLIL_ERR_NOT_FOUND))
        r->fatal_error = rc == NINLIL_OK ? NINLIL_ERR_STATE : rc;
    return rc;
}
