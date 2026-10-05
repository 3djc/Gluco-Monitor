#pragma once
#include <stdio.h>
#define log_e(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) ((void)0)
#define log_d(fmt, ...) ((void)0)
#define log_v(fmt, ...) ((void)0)
