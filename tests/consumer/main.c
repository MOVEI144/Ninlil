#include <ninlil.h>
#ifdef CONSUMER_SECURE
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
    ninlil_node_close(node);
#endif
    return 0;
}
