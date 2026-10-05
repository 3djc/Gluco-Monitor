#pragma once
#include <stdint.h>
#define portNUM_PROCESSORS 2
typedef struct
{
    uint32_t timeout_ms;
    uint32_t idle_core_mask;
    bool trigger_panic;
} esp_task_wdt_config_t;
inline int esp_task_wdt_deinit() { return 0; }
inline int esp_task_wdt_init(const esp_task_wdt_config_t *) { return 0; }
inline int esp_task_wdt_add(void *) { return 0; }
inline int esp_task_wdt_reset() { return 0; }
