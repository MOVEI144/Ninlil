#include <ninlil.h>
#ifdef CONSUMER_SECURE
#include <ninlil_binding.h>
#include <ninlil_bulk.h>
#include <ninlil_fanout_core.h>
#include <ninlil_identity.h>
#include <ninlil_node.h>
#endif
int main(void)
{
    ninlil_role_profile profile;
    if (ninlil_role_profile_standard(NINLIL_ROLE_POWERED_ENDPOINT, &profile) !=
        NINLIL_OK)
        return 1;
#ifdef CONSUMER_SECURE
    ninlil_node *node = 0;
    ninlil_identity identity = {0};
    if (psa_crypto_init() != PSA_SUCCESS ||
        ninlil_node_open(&node, 0, 0u) != NINLIL_ERR_INVALID)
        return 1;
    ninlil_identity_close(&identity);
    if (ninlil_bulk_open(NULL, NULL, 0u, 0u, 0) != NINLIL_ERR_INVALID)
        return 1;
    if (ninlil_submit_bound(NULL, NULL, NULL, NULL) != NINLIL_ERR_INVALID ||
        ninlil_fanout_core_connect(NULL, NULL, NULL) != NINLIL_ERR_INVALID)
        return 1;
    ninlil_node_close(node);
#endif
    return 0;
}
