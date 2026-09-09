#include "sim.h"

#include <stddef.h>
#include <string.h>

typedef struct manifest_field {
    const char *name;
    size_t offset;
} manifest_field;

#define FIELD(name)                                                            \
    {                                                                          \
        #name, offsetof(sim_manifest, name)                                    \
    }
static const manifest_field fields[] = {FIELD(version),
                                        FIELD(seed),
                                        FIELD(nodes),
                                        FIELD(burst),
                                        FIELD(payload_bytes),
                                        FIELD(sf),
                                        FIELD(duration_ms),
                                        FIELD(interval_ms),
                                        FIELD(latency_target_ms),
                                        FIELD(loss_permille),
                                        FIELD(duplicate_permille),
                                        FIELD(offline_node),
                                        FIELD(offline_start_ms),
                                        FIELD(offline_end_ms),
                                        FIELD(restart_node),
                                        FIELD(restart_ms),
                                        FIELD(receipt_hold_ms)};
#undef FIELD

int sim_manifest_validate(const sim_manifest *m)
{
    if (!m || m->version != 1u || m->seed == 0u || m->nodes < 2u ||
        m->nodes > SIM_NODES || m->burst == 0u || m->burst > SIM_BURST ||
        m->payload_bytes < 4u || m->payload_bytes > SIM_MTU - 40u ||
        m->sf < 7u || m->sf > 12u || m->duration_ms == 0u ||
        m->duration_ms > 600000u || m->duration_ms % 10u != 0u ||
        m->interval_ms % 10u != 0u ||
        (uint64_t)m->interval_ms * (m->burst - 1u) >= m->duration_ms ||
        m->latency_target_ms == 0u || m->latency_target_ms > m->duration_ms ||
        m->loss_permille > 1000u || m->duplicate_permille > 1000u ||
        m->receipt_hold_ms > m->duration_ms || m->receipt_hold_ms % 10u != 0u ||
        m->offline_node > m->nodes || m->restart_node > m->nodes)
        return NINLIL_ERR_INVALID;
    if (m->offline_node == 0u) {
        if (m->offline_start_ms != 0u || m->offline_end_ms != 0u)
            return NINLIL_ERR_INVALID;
    } else if (m->offline_start_ms >= m->offline_end_ms ||
               m->offline_end_ms > m->duration_ms ||
               m->offline_start_ms % 10u != 0u ||
               m->offline_end_ms % 10u != 0u) {
        return NINLIL_ERR_INVALID;
    }
    if (m->restart_node == 0u)
        return m->restart_ms == 0u ? NINLIL_OK : NINLIL_ERR_INVALID;
    if (m->restart_ms == 0u || m->restart_ms >= m->duration_ms ||
        m->restart_ms % 10u != 0u)
        return NINLIL_ERR_INVALID;
    return NINLIL_OK;
}

int sim_manifest_read(FILE *file, sim_manifest *out)
{
    sim_manifest candidate = {0};
    uint32_t seen = 0u;
    size_t lines = 0u;
    char line[128];

    if (!file || !out)
        return NINLIL_ERR_INVALID;
    while (fgets(line, sizeof(line), file)) {
        char *equals, *value;
        uint32_t number = 0u;
        size_t index, length = strlen(line);
        if (++lines > 64u || length == 0u || line[length - 1u] != '\n')
            return NINLIL_ERR_INVALID;
        line[--length] = '\0';
        if (length > 0u && line[length - 1u] == '\r')
            line[--length] = '\0';
        if (length == 0u || line[0] == '#')
            continue;
        equals = strchr(line, '=');
        if (!equals || equals[1] == '\0')
            return NINLIL_ERR_INVALID;
        *equals = '\0';
        for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++)
            if (strcmp(line, fields[index].name) == 0)
                break;
        if (index == sizeof(fields) / sizeof(fields[0]) ||
            (seen & (UINT32_C(1) << index)) != 0u)
            return NINLIL_ERR_INVALID;
        for (value = equals + 1; *value; value++) {
            uint32_t digit;
            if (*value < '0' || *value > '9')
                return NINLIL_ERR_INVALID;
            digit = (uint32_t)(*value - '0');
            if (number > (UINT32_MAX - digit) / 10u)
                return NINLIL_ERR_INVALID;
            number = number * 10u + digit;
        }
        memcpy((unsigned char *)&candidate + fields[index].offset, &number,
               sizeof(number));
        seen |= UINT32_C(1) << index;
    }
    if (ferror(file) || seen != (UINT32_C(1) << 17u) - 1u ||
        sim_manifest_validate(&candidate) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    *out = candidate;
    return NINLIL_OK;
}

int sim_manifest_write(FILE *file, const sim_manifest *manifest)
{
    size_t index;
    if (!file || sim_manifest_validate(manifest) != NINLIL_OK)
        return NINLIL_ERR_INVALID;
    for (index = 0u; index < sizeof(fields) / sizeof(fields[0]); index++) {
        uint32_t value;
        memcpy(&value, (const unsigned char *)manifest + fields[index].offset,
               sizeof(value));
        if (fprintf(file, "%s=%u\n", fields[index].name, value) < 0)
            return NINLIL_ERR_IO;
    }
    return NINLIL_OK;
}
