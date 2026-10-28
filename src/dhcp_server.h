#ifndef NXIC_DHCP_SERVER_H_
#define NXIC_DHCP_SERVER_H_
#include <stddef.h>
#include <stdint.h>

// Single USB peer: 192.168.7.1/24 leases 192.168.7.16. Returns reply length,
// or zero for malformed/unrelated requests. Replies are broadcast to port 68.
size_t dhcp_server_reply(const uint8_t *req, size_t len,
                         uint8_t *reply, size_t cap);
#endif
