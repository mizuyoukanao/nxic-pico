#ifndef NXIC_GAMEPAD_H_
#define NXIC_GAMEPAD_H_
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PAD_NONE, PAD_SWITCH_PRO, PAD_SWITCH_GRIP, PAD_SWITCH_LEFT, PAD_SWITCH_RIGHT,
    PAD_SWITCH2_PRO, PAD_SWITCH2_LEFT, PAD_SWITCH2_RIGHT
} gamepad_kind_t;

typedef struct {
    uint8_t buttons[3]; // Switch 1 0x30 layout, unsupported buttons masked
    uint16_t axis[4];   // LX, LY, RX, RY; center 0x800, up/right positive
    uint8_t axes;       // bitmask of axes physically present
} gamepad_state_t;

typedef struct { uint16_t center, min, max; } gamepad_cal_t;
gamepad_kind_t gamepad_identify(uint16_t vid, uint16_t pid);
void gamepad_neutral(gamepad_state_t *st);
bool gamepad_parse(gamepad_kind_t kind, const uint8_t *report, size_t len,
                   const gamepad_cal_t cal[4], gamepad_state_t *st);
void gamepad_merge(gamepad_state_t *dst, const gamepad_state_t *src);
uint16_t gamepad_axis_keys(uint16_t analog, bool negative, bool positive);
void gamepad_default_cal(gamepad_cal_t cal[4]);
bool gamepad_set_cal(gamepad_cal_t cal[4], bool right, const uint8_t data[9]);
#endif
