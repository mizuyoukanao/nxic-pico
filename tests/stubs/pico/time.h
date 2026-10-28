#ifndef TEST_PICO_TIME_H
#define TEST_PICO_TIME_H
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t absolute_time_t;
extern uint32_t test_now;
static inline absolute_time_t get_absolute_time(void) { return test_now; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return t; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return test_now + ms; }
static inline bool time_reached(absolute_time_t t) { return test_now >= t; }
#endif
