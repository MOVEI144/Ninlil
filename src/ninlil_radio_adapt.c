#include "ninlil_radio_adapt.h"
#include <string.h>
int ninlil_radio_adapt_open(ninlil_radio_adapt *s, int8_t minimum,
                            int8_t maximum)
{
    if (!s || minimum < -9 || maximum > 22 || minimum > maximum ||
        (maximum - minimum) % 3 != 0)
        return NINLIL_ERR_INVALID;
    memset(s, 0, sizeof(*s));
    s->minimum_dbm = minimum;
    s->maximum_dbm = s->power_dbm = maximum;
    return NINLIL_OK;
}
int ninlil_radio_adapt_observe(ninlil_radio_adapt *s, uint64_t now,
                               uint64_t observed, uint16_t attempts,
                               uint16_t delivered, int8_t *power)
{
    if (!s || !power || now < s->now_ms || observed > now ||
        delivered > attempts || attempts > 8u)
        return NINLIL_ERR_INVALID;
    s->now_ms = now;
    if (!observed || now - observed > 60000u || attempts != 8u) {
        s->power_dbm = s->maximum_dbm;
        s->good = 0u;
        s->changed_ms = now;
    } else if (observed > s->observed_ms) {
        s->observed_ms = observed;
        if (delivered <= 6u) {
            s->power_dbm = s->maximum_dbm;
            s->changed_ms = now;
            s->good = 0u;
        } else if (delivered == 8u) {
            if (s->good < 3u)
                s->good++;
            if (s->good == 3u && now - s->changed_ms >= 30000u &&
                s->power_dbm > s->minimum_dbm) {
                s->power_dbm = (int8_t)(s->power_dbm - 3);
                s->changed_ms = now;
                s->good = 0u;
            }
        } else
            s->good = 0u;
    }
    *power = s->power_dbm;
    return NINLIL_OK;
}
