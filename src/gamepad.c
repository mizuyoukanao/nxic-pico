// Protocol facts: dekuNukem/Nintendo_Switch_Reverse_Engineering and
// ndeadly/switch2_controller_research (see docs/validation.md).
#include "gamepad.h"
#include <string.h>

gamepad_kind_t gamepad_identify(uint16_t vid, uint16_t pid) {
    if (vid != 0x057e) return PAD_NONE;
    switch (pid) {
        case 0x2009: return PAD_SWITCH_PRO;
        case 0x200e: return PAD_SWITCH_GRIP;
        case 0x2006: return PAD_SWITCH_LEFT;
        case 0x2007: return PAD_SWITCH_RIGHT;
        case 0x2069: return PAD_SWITCH2_PRO;
        case 0x2067: return PAD_SWITCH2_LEFT;
        case 0x2066: return PAD_SWITCH2_RIGHT;
        default: return PAD_NONE;
    }
}

void gamepad_neutral(gamepad_state_t *st) {
    memset(st, 0, sizeof(*st));
    for (int i = 0; i < 4; ++i) st->axis[i] = 0x800;
}

void gamepad_default_cal(gamepad_cal_t cal[4]) {
    for (int i = 0; i < 4; ++i) cal[i] = (gamepad_cal_t){0x800, 0x100, 0xf00};
}

static void unpack(const uint8_t *p, uint16_t *x, uint16_t *y) {
    *x = p[0] | ((p[1] & 15u) << 8);
    *y = (p[1] >> 4) | (p[2] << 4);
}

bool gamepad_set_cal(gamepad_cal_t cal[4], bool right, const uint8_t data[9]) {
    uint16_t v[6];
    for (int i = 0; i < 3; ++i) unpack(data + i * 3, &v[i*2], &v[i*2+1]);
    int c = right ? 0 : 2, lo = right ? 2 : 4, hi = right ? 4 : 0;
    gamepad_cal_t tmp[2];
    for (int i = 0; i < 2; ++i) {
        if (v[lo+i] < 128 || v[hi+i] < 128 || v[c+i] < v[lo+i] ||
            v[c+i] + v[hi+i] > 4095) return false;
        tmp[i] = (gamepad_cal_t){v[c+i], v[c+i] - v[lo+i], v[c+i] + v[hi+i]};
    }
    memcpy(cal + (right ? 2 : 0), tmp, sizeof(tmp));
    return true;
}

static uint16_t normalize(uint16_t raw, const gamepad_cal_t *c) {
    // Small hardware noise dead zone; re-scale remaining travel continuously.
    const int dead = 48;
    int d = (int)raw - c->center;
    int span = d < 0 ? c->center - c->min : c->max - c->center;
    int a = d < 0 ? -d : d;
    if (a <= dead || span <= dead) return 0x800;
    if (a > span) a = span;
    int v = (a - dead) * 0x700 / (span - dead);
    return (uint16_t)(0x800 + (d < 0 ? -v : v));
}

bool gamepad_parse(gamepad_kind_t kind, const uint8_t *r, size_t len,
                   const gamepad_cal_t cal[4], gamepad_state_t *st) {
    if (kind == PAD_NONE || !len) return false;
    gamepad_state_t next;
    gamepad_neutral(&next);
    const uint8_t *sticks = NULL;
    if (kind <= PAD_SWITCH_RIGHT) {
        if ((r[0] != 0x30 && r[0] != 0x21) || len < 13) return false;
        memcpy(next.buttons, r + 3, 3);
        sticks = r + 6;
        next.axes = 15;
    } else if (r[0] == 5 && len >= 17) {
        memcpy(next.buttons, r + 5, 3);
        sticks = r + 11;
        next.axes = 15;
    } else if ((kind == PAD_SWITCH2_PRO && r[0] == 9 && len >= 12) ||
               (kind == PAD_SWITCH2_LEFT && r[0] == 7 && len >= 9) ||
               (kind == PAD_SWITCH2_RIGHT && r[0] == 8 && len >= 9)) {
        uint8_t a = r[3], b = r[4];
        if (kind != PAD_SWITCH2_LEFT) {
            next.buttons[0] = ((a & 1) ? 4 : 0) | ((a & 2) ? 8 : 0) |
                ((a & 4) ? 1 : 0) | ((a & 8) ? 2 : 0) |
                ((a & 16) ? 64 : 0) | ((a & 32) ? 128 : 0);
            next.buttons[1] = ((a & 64) ? 2 : 0) | ((a & 128) ? 4 : 0);
        }
        if (kind == PAD_SWITCH2_PRO || kind == PAD_SWITCH2_LEFT) {
            uint8_t l = kind == PAD_SWITCH2_LEFT ? a : b;
            next.buttons[2] = (l & 1) | ((l & 2) ? 4 : 0) |
                ((l & 4) ? 8 : 0) | ((l & 8) ? 2 : 0) |
                ((l & 16) ? 64 : 0) | ((l & 32) ? 128 : 0);
            next.buttons[1] |= ((l & 64) ? 1 : 0) | ((l & 128) ? 8 : 0);
        }
        if (kind == PAD_SWITCH2_PRO) {
            next.buttons[1] |= ((r[5] & 1) ? 16 : 0) | ((r[5] & 2) ? 32 : 0);
            sticks = r + 6;
            next.axes = 15;
        } else {
            next.buttons[1] |= (b & 1) ? (kind == PAD_SWITCH2_LEFT ? 32 : 16) : 0;
            unpack(r + 6, &next.axis[kind == PAD_SWITCH2_LEFT ? 0 : 2],
                           &next.axis[kind == PAD_SWITCH2_LEFT ? 1 : 3]);
            next.axes = kind == PAD_SWITCH2_LEFT ? 3 : 12;
        }
    } else return false;
    if (sticks) {
        unpack(sticks, &next.axis[0], &next.axis[1]);
        unpack(sticks + 3, &next.axis[2], &next.axis[3]);
    }
    if (kind == PAD_SWITCH_LEFT || kind == PAD_SWITCH2_LEFT) {
        next.axes = 3;
        next.buttons[0] = 0; next.buttons[1] &= 0x29;
    } else if (kind == PAD_SWITCH_RIGHT || kind == PAD_SWITCH2_RIGHT) {
        next.axes = 12;
        next.buttons[2] = 0; next.buttons[1] &= 0x16;
    }
    // C/GL/GR and SL/SR have no counterpart in the emulated Switch 1 pad.
    next.buttons[0] &= 0xcf; next.buttons[1] &= 0x3f; next.buttons[2] &= 0xcf;
    for (int i = 0; i < 4; ++i)
        next.axis[i] = (next.axes & (1u << i)) ? normalize(next.axis[i], &cal[i]) : 0x800;
    *st = next;
    return true;
}

void gamepad_merge(gamepad_state_t *dst, const gamepad_state_t *src) {
    for (int i = 0; i < 3; ++i) dst->buttons[i] |= src->buttons[i];
    for (int i = 0; i < 4; ++i) {
        int a = (int)dst->axis[i] - 0x800, b = (int)src->axis[i] - 0x800;
        if (b < 0) b = -b;
        if (a < 0) a = -a;
        if ((src->axes & (1u << i)) && b > a) dst->axis[i] = src->axis[i];
    }
    dst->axes |= src->axes;
}

uint16_t gamepad_axis_keys(uint16_t analog, bool negative, bool positive) {
    if (negative && positive) return 0x800;
    if (negative) return 0;
    if (positive) return 0xfff;
    return analog;
}
