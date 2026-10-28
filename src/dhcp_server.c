#include "dhcp_server.h"
#include <stdbool.h>
#include <string.h>

static const uint8_t server[4] = {192, 168, 7, 1};
static const uint8_t client[4] = {192, 168, 7, 16};
static const uint8_t mask[4] = {255, 255, 255, 0};
static const uint8_t cookie[4] = {99, 130, 83, 99};

size_t dhcp_server_reply(const uint8_t *req, size_t len,
                         uint8_t *reply, size_t cap) {
    if (len < 240 || cap < 300 || req[0] != 1 || req[1] != 1 ||
        req[2] != 6 || memcmp(req + 236, cookie, 4) != 0) return 0;
    // Relays are not part of the point-to-point USB network.
    static const uint8_t zero[4] = {0};
    if (memcmp(req + 24, zero, 4) != 0) return 0;
    uint8_t type = 0;
    const uint8_t *requested = NULL, *selected = NULL;
    bool end = false;
    for (size_t i = 240; i < len;) {
        uint8_t opt = req[i++];
        if (opt == 255) { end = true; break; }
        if (opt == 0) continue;
        if (i == len) return 0;
        size_t n = req[i++];
        if (n > len - i) return 0;
        if (opt == 53) {
            if (n != 1 || type) return 0;
            type = req[i];
        } else if (opt == 50) {
            if (n != 4 || requested) return 0;
            requested = req + i;
        } else if (opt == 54) {
            if (n != 4 || selected) return 0;
            selected = req + i;
        }
        i += n;
    }
    if (!end || (type != 1 && type != 3)) return 0;
    if (selected && memcmp(selected, server, 4) != 0) return 0;
    bool nak = false;
    if (type == 3) {
        const uint8_t *target = requested ? requested : req + 12;
        if (memcmp(target, zero, 4) == 0) return 0;
        nak = memcmp(target, client, 4) != 0;
    }
    memset(reply, 0, 300);
    reply[0] = 2; reply[1] = 1; reply[2] = 6;
    memcpy(reply + 4, req + 4, 4); // xid
    reply[10] = 0x80; // broadcast, including NAK/INIT-REBOOT clients
    if (!nak) {
        memcpy(reply + 12, req + 12, 4); // ciaddr for renewal
        memcpy(reply + 16, client, 4);
        memcpy(reply + 20, server, 4);
    }
    memcpy(reply + 28, req + 28, 16);
    memcpy(reply + 236, cookie, 4);
    size_t i = 240;
    reply[i++] = 53; reply[i++] = 1;
    reply[i++] = nak ? 6 : (type == 1 ? 2 : 5);
    reply[i++] = 54; reply[i++] = 4;
    memcpy(reply + i, server, 4); i += 4;
    if (!nak) {
        reply[i++] = 51; reply[i++] = 4; // lease: 86400 s
        reply[i++] = 0; reply[i++] = 1; reply[i++] = 0x51; reply[i++] = 0x80;
        reply[i++] = 1; reply[i++] = 4;
        memcpy(reply + i, mask, 4); i += 4;
        // No router/DNS: this link must not replace the PC's internet route.
    }
    reply[i] = 255;
    return 300; // BOOTP minimum, padded after END
}
