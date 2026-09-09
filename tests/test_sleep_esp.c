#include "esp_sleep.h"
#include "esp_timer.h"
#include "ninlil_sleep.h"
#include "ninlil_sleep_esp.h"
#include <stdio.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                         \
            return 1;                                                          \
        }                                                                      \
    } while (0)
static int suspended, rejected, timer_fail, radio_fail, recovered, disabled;
static int64_t clock_us;
static uint64_t requested, resumed_at;
int64_t esp_timer_get_time(void)
{
    return clock_us;
}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t duration)
{
    requested = duration;
    return timer_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source)
{
    disabled = source == ESP_SLEEP_WAKEUP_TIMER;
    return ESP_OK;
}
esp_err_t esp_light_sleep_start(void)
{
    if (!rejected)
        clock_us += (int64_t)requested;
    return rejected ? ESP_FAIL : ESP_OK;
}
int ninlil_node_suspend(ninlil_node *node, uint64_t now)
{
    (void)node;
    (void)now;
    suspended = 1;
    return NINLIL_OK;
}
int ninlil_node_resume(ninlil_node *node, uint64_t now)
{
    (void)node;
    suspended = 0;
    resumed_at = now;
    return NINLIL_OK;
}
int ninlil_sx1262_radio_sleep(ninlil_sx1262_radio *radio)
{
    (void)radio;
    return radio_fail ? NINLIL_ERR_IO : NINLIL_OK;
}
int ninlil_sx1262_radio_recover(ninlil_sx1262_radio *radio)
{
    (void)radio;
    recovered++;
    return NINLIL_OK;
}
int main(void)
{
    ninlil_sx1262_radio radio = {0};
    ninlil_esp_network_pump pump = {0};
    uint32_t elapsed;
    int token;
    pump.node = (ninlil_node *)(void *)&token;
    pump.radio = &radio;
    CHECK(ninlil_esp_node_sleep(&pump, 0u, &elapsed) == NINLIL_ERR_INVALID);
    CHECK(!suspended && !recovered);
    clock_us = 1000000;
    CHECK(ninlil_esp_node_sleep(&pump, 5000u, &elapsed) == NINLIL_OK);
    CHECK(elapsed == 5000u && resumed_at == 6000u && !suspended && disabled &&
          recovered == 1);
    rejected = 1;
    CHECK(ninlil_esp_node_sleep(&pump, 5000u, &elapsed) == NINLIL_ERR_BUSY);
    CHECK(!elapsed && !suspended && recovered == 2);
    timer_fail = 1;
    CHECK(ninlil_esp_node_sleep(&pump, 5000u, &elapsed) == NINLIL_ERR_IO);
    CHECK(!suspended && recovered == 3);
    radio_fail = 1;
    CHECK(ninlil_esp_node_sleep(&pump, 5000u, &elapsed) == NINLIL_ERR_IO);
    CHECK(!suspended && recovered == 4);
    pump.scheduler.busy = 1u;
    radio_fail = timer_fail = rejected = 0;
    CHECK(ninlil_esp_node_sleep(&pump, 5000u, &elapsed) == NINLIL_OK);
    CHECK(recovered == 5 && pump.scheduler.busy && elapsed == 5000u);
    puts("ESP timer, rejected sleep, radio failure recovery and elapsed clock "
         "PASS");
    return 0;
}
