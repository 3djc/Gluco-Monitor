// "NTP" is the host clock: configTzTime() sets TZ and fires the sync callback right away.
#pragma once
#include <Arduino.h>
#include <sys/time.h>
typedef void (*sntp_sync_time_cb_t)(struct timeval *tv);
inline void sntp_set_sync_interval(uint32_t) {}
void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t cb);
inline void esp_sntp_servermode_dhcp(bool) {}
void configTzTime(const char *tz, const char *s1, const char *s2 = nullptr);
