#pragma once
#include <stdint.h>
typedef struct
{
    int type, subtype;
    uint32_t address, size;
    char label[17];
} esp_partition_t;
typedef void *esp_partition_iterator_t;
#define ESP_PARTITION_TYPE_ANY 0xff
#define ESP_PARTITION_SUBTYPE_ANY 0xff
inline esp_partition_iterator_t esp_partition_find(int, int, const char *) { return nullptr; }
inline const esp_partition_t *esp_partition_get(esp_partition_iterator_t) { return nullptr; }
inline esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t) { return nullptr; }
inline void esp_partition_iterator_release(esp_partition_iterator_t) {}
