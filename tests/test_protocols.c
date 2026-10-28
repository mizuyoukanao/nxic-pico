#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dhcp_server.h"
#include "gamepad.h"

static void pack(uint8_t *p, unsigned x, unsigned y) {
    p[0] = x; p[1] = (x >> 8) | (y << 4); p[2] = y >> 4;
}
static void test_gamepads(void) {
    gamepad_cal_t cal[4]; gamepad_default_cal(cal);
    gamepad_state_t a, b;
    uint8_t r[64] = {0x30};
    pack(r + 6, 2048 + 100, 2048); pack(r + 9, 2048, 2048);
    r[3] = 8; r[4] = 16; r[5] = 0x80;
    assert(gamepad_identify(0x057e, 0x200e) == PAD_SWITCH_GRIP);
    assert(gamepad_identify(0x057e, 0x2069) == PAD_SWITCH2_PRO);
    assert(gamepad_identify(0x057e, 0x2067) == PAD_SWITCH2_LEFT);
    assert(gamepad_identify(0x057e, 0x2066) == PAD_SWITCH2_RIGHT);
    assert(gamepad_identify(0xabcd, 0x2009) == PAD_NONE);
    assert(gamepad_parse(PAD_SWITCH_PRO, r, 64, cal, &a));
    assert(a.buttons[0] == 8 && a.buttons[1] == 16 && a.buttons[2] == 128);
    assert(a.axis[0] > 2048 && a.axis[0] < 2200); // fine analog, not digital
    assert(a.axis[1] == 2048 && a.axes == 15);
    uint16_t prev = 2048;
    for (unsigned x = 2048; x <= 4095; ++x) {
        pack(r + 6, x, 2048);
        assert(gamepad_parse(PAD_SWITCH_PRO, r, 64, cal, &a));
        assert(a.axis[0] >= prev && a.axis[0] <= 0xf00);
        prev = a.axis[0];
    }
    for (int kind = PAD_SWITCH_PRO; kind <= PAD_SWITCH_RIGHT; ++kind) {
        assert(gamepad_parse(kind, r, 64, cal, &a));
        assert(!gamepad_parse(kind, r, 12, cal, &a));
    }
    // Joy-Con pair: unused axes and buttons must not leak from the other half.
    pack(r + 6, 0xf00, 2048); pack(r + 9, 2048, 0x100);
    assert(gamepad_parse(PAD_SWITCH_LEFT, r, 64, cal, &a));
    assert(gamepad_parse(PAD_SWITCH_RIGHT, r, 64, cal, &b));
    assert(a.axes == 3 && b.axes == 12 && a.buttons[0] == 0 && b.buttons[2] == 0);
    gamepad_merge(&a, &b);
    assert(a.axes == 15 && a.axis[0] == 0xf00 && a.axis[3] == 0x100);
    assert(a.buttons[0] == 8 && a.buttons[2] == 128);
    assert(gamepad_axis_keys(2100, false, false) == 2100);
    assert(gamepad_axis_keys(2100, true, false) == 0);
    assert(gamepad_axis_keys(2100, false, true) == 4095);
    assert(gamepad_axis_keys(2100, true, true) == 2048);
    gamepad_neutral(&b); gamepad_neutral(&a); gamepad_merge(&a, &b);
    assert(a.axis[0] == 2048 && a.buttons[0] == 0);

    // Native Switch 2 packets, then the common 0x05 format.
    memset(r, 0, sizeof(r)); r[0] = 9;
    r[3] = 0xff; r[4] = 0xff; r[5] = 0xff;
    pack(r + 6, 0xf00, 0x800); pack(r + 9, 0x800, 0x100);
    assert(gamepad_parse(PAD_SWITCH2_PRO, r, 64, cal, &a));
    assert(a.buttons[0] == 0xcf && a.buttons[1] == 0x3f && a.buttons[2] == 0xcf);
    assert(a.axis[0] == 0xf00 && a.axis[3] == 0x100);
    r[0] = 7;
    assert(gamepad_parse(PAD_SWITCH2_LEFT, r, 64, cal, &a));
    assert(a.buttons[0] == 0 && a.buttons[1] == 0x29 && a.buttons[2] == 0xcf);
    r[0] = 8;
    assert(gamepad_parse(PAD_SWITCH2_RIGHT, r, 64, cal, &a));
    assert(a.buttons[0] == 0xcf && a.buttons[1] == 0x16 && a.buttons[2] == 0);
    r[0] = 5; r[5] = 0xff; r[6] = 0xff; r[7] = 0xff;
    pack(r + 11, 0xf00, 0x800); pack(r + 14, 0x800, 0x100);
    for (int kind = PAD_SWITCH2_PRO; kind <= PAD_SWITCH2_RIGHT; ++kind) {
        assert(gamepad_parse(kind, r, 64, cal, &a));
        assert(a.axes == (kind == PAD_SWITCH2_PRO ? 15 : kind == PAD_SWITCH2_LEFT ? 3 : 12));
        assert(!gamepad_parse(kind, r, 16, cal, &a));
    }
    // Every native button individually: detect swapped Nintendo mappings.
    static const uint8_t out_byte[24] = {0,0,0,0,0,0,1,1,2,2,2,2,2,2,1,1,1,1,3,3,3,3,3,3};
    static const uint8_t out_mask[24] = {4,8,1,2,64,128,2,4,1,4,8,2,64,128,1,8,16,32,0,0,0,0,0,0};
    for (int bit = 0; bit < 24; ++bit) {
        memset(r, 0, sizeof(r)); r[0] = 9; r[3 + bit/8] = 1u << (bit%8);
        pack(r + 6, 0x800, 0x800); pack(r + 9, 0x800, 0x800);
        assert(gamepad_parse(PAD_SWITCH2_PRO, r, 64, cal, &a));
        for (int i = 0; i < 3; ++i) assert(a.buttons[i] == (out_byte[bit] == i ? out_mask[bit] : 0));
    }
    uint8_t calibration[9];
    pack(calibration, 1000, 1100); pack(calibration + 3, 1900, 2000); pack(calibration + 6, 900, 1000);
    assert(gamepad_set_cal(cal, false, calibration));
    assert(cal[0].center == 1900 && cal[0].min == 1000 && cal[0].max == 2900);
    pack(calibration, 2100, 2200); pack(calibration + 3, 900, 1000); pack(calibration + 6, 1000, 1100);
    assert(gamepad_set_cal(cal, true, calibration));
    assert(cal[2].center == 2100 && cal[2].min == 1200 && cal[2].max == 3100);
    memset(calibration, 255, 9); assert(!gamepad_set_cal(cal, false, calibration));
}

static size_t request(uint8_t *r, uint8_t type) {
    memset(r, 0, 1500); r[0] = 1; r[1] = 1; r[2] = 6;
    r[4] = 42; r[28] = 2; r[33] = 55;
    memcpy(r + 236, "\x63\x82\x53\x63", 4);
    r[240] = 0; // PAD must consume exactly one byte
    r[241] = 53; r[242] = 1; r[243] = type; r[244] = 255;
    return 245;
}
static void test_dhcp(void) {
    uint8_t r[1500], out[300]; size_t n = request(r, 1);
    assert(dhcp_server_reply(r, n, out, sizeof(out)) == 300);
    assert(out[242] == 2 && out[4] == 42 && out[33] == 55);
    assert(!memcmp(out + 16, "\xc0\xa8\x07\x10", 4));
    // No nonexistent router or DNS is advertised.
    for (size_t i = 240; out[i] != 255; i += 2 + out[i+1]) assert(out[i] != 3 && out[i] != 6);
    n = request(r, 3);
    r[244] = 50; r[245] = 4; memcpy(r+246, "\xc0\xa8\x07\x10", 4); r[250] = 255; n = 251;
    assert(dhcp_server_reply(r, n, out, sizeof(out)) == 300 && out[242] == 5);
    r[249] = 99;
    assert(dhcp_server_reply(r, n, out, sizeof(out)) == 300 && out[242] == 6);
    r[249] = 16; r[250] = 54; r[251] = 4;
    memcpy(r+252, "\xc0\xa8\x07\x02", 4); r[256] = 255;
    assert(!dhcp_server_reply(r, 257, out, sizeof(out))); // selected other server
    n = request(r, 3); memcpy(r+12, "\xc0\xa8\x07\x10", 4);
    assert(dhcp_server_reply(r, n, out, sizeof(out)) == 300 && out[242] == 5);
    assert(!memcmp(r+12, out+12, 4));
    n = request(r, 1);
    // Option 53 beyond old 320-byte request limit.
    memset(r+240, 0, 180); r[420] = 53; r[421] = 1; r[422] = 1; r[423] = 255;
    assert(dhcp_server_reply(r, 424, out, sizeof(out)) == 300);
    n = request(r, 1); r[236] = 0; assert(!dhcp_server_reply(r,n,out,sizeof(out)));
    n = request(r, 1); r[242] = 100; assert(!dhcp_server_reply(r,n,out,sizeof(out)));
    n = request(r, 1); assert(!dhcp_server_reply(r,n,out,299));
    for (size_t len = 0; len < 245; ++len) assert(!dhcp_server_reply(r,len,out,sizeof(out)));
    n = request(r, 8); assert(!dhcp_server_reply(r,n,out,sizeof(out)));
    // Deterministic malformed-packet corpus: lengths, options and report IDs.
    uint32_t seed = 7; gamepad_cal_t cal[4]; gamepad_default_cal(cal); gamepad_state_t state;
    for (unsigned t = 0; t < 20000; ++t) {
        for (size_t i = 0; i < sizeof(r); ++i) { seed = seed*1664525u+1013904223u; r[i] = seed >> 24; }
        dhcp_server_reply(r, t % sizeof(r), out, sizeof(out));
        for (int k = PAD_NONE; k <= PAD_SWITCH2_RIGHT; ++k) gamepad_parse(k,r,t%65,cal,&state);
    }
}
int main(void) { test_gamepads(); test_dhcp(); puts("PASS: gamepad decoding/mixing/calibration and DHCP protocol + 20000 malformed packets"); }
