// Exercise the actual config-mode NCM glue and lwIP IPv4/UDP receive path.
// Only the USB transport and hardware are replaced by host stubs.
#include <assert.h>
#include "../src/config_mode.c"
#include "lwip/inet_chksum.h"

uint32_t test_now;
static uint8_t frame_out[1600];
static uint16_t frame_len;
static unsigned renew_calls;
static bool tx_available = true;
settings_t g_settings;
bool usb_config_mode;
bool settings_save(void) { return true; }
void pico_get_unique_board_id(pico_unique_board_id_t *id) { memset(id,0,sizeof(*id)); }
bool tud_rhport_init(uint8_t rhport, const tusb_rhport_init_t *init) { (void)rhport; (void)init; return true; }
void watchdog_reboot(uint32_t pc,uint32_t sp,uint32_t ms) { (void)pc; (void)sp; (void)ms; }

bool tud_mounted(void) { return true; }
bool tud_suspended(void) { return false; }
bool tud_network_can_xmit(uint16_t n) { (void)n; return tx_available; }
void tud_network_xmit(void *ref, uint16_t arg) { frame_len = tud_network_xmit_cb(frame_out, ref, arg); }
void tud_network_recv_renew(void) { renew_calls++; }
void tud_task_ext(uint32_t timeout, bool isr) { (void)timeout; (void)isr; test_now++; }
uint32_t sys_now(void) { return test_now; }
void led_set_rgb(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; }

static void bootrequest(uint8_t *eth, uint8_t type, bool large) {
    size_t payload = large ? 700 : 300;
    memset(eth, 0, 14 + 20 + 8 + payload);
    memset(eth, 255, 6); eth[6] = 2; eth[11] = 42; eth[12] = 8;
    uint8_t *ip = eth + 14, *udp = ip + 20, *r = udp + 8;
    ip[0] = 0x45; ip[2] = (20+8+payload) >> 8; ip[3] = 20+8+payload;
    ip[8] = 64; ip[9] = 17; memset(ip+16, 255, 4);
    uint16_t sum = inet_chksum(ip, 20); memcpy(ip+10, &sum, 2);
    udp[1] = 68; udp[3] = 67; udp[4] = (payload+8) >> 8; udp[5] = payload+8;
    r[0] = 1; r[1] = 1; r[2] = 6; r[4] = 33; r[28] = 2; r[33] = 42;
    memcpy(r+236, "\x63\x82\x53\x63", 4);
    size_t opt = large ? 600 : 240;
    r[opt++] = 53; r[opt++] = 1; r[opt++] = type;
    if (type == 3) { r[opt++] = 50; r[opt++] = 4; memcpy(r+opt,"\xc0\xa8\x07\x10",4); opt += 4; }
    r[opt] = 255;
}

int main(void) {
    lwip_init();
    assert(netif_add(&netif_data,&our_ip,&our_mask,&our_ip,NULL,netif_init_cb,ethernet_input));
    netif_set_default(&netif_data); netif_set_up(&netif_data);
    assert(dhcpd_init());
    uint8_t eth[1600];
    for (int type = 1; type <= 3; type += 2) {
        for (int large = 0; large <= 1; ++large) {
            bootrequest(eth, type, large);
            frame_len = 0;
            assert(tud_network_recv_cb(eth, 14+20+8+(large ? 700 : 300)));
            assert(!tud_network_recv_cb(eth, 100)); // backpressure preserves queued packet
            service_traffic();
#ifdef TEST_DHCP_BEFORE_FIX
            assert(!received_frame && frame_len == 0);
            puts("PASS: original lwIP configuration reproduces DHCP drop before UDP callback");
            return 0;
#endif
            assert(!received_frame && frame_len == 14+20+8+300);
            assert(frame_out[14+20+8+242] == (type == 1 ? 2 : 5));
            assert(frame_out[14+20+8+4] == 33);
            assert(!memcmp(frame_out, "\xff\xff\xff\xff\xff\xff", 6));
        }
    }
    assert(diag_stage == 2);
    unsigned prev_renew = renew_calls; service_traffic(); assert(renew_calls > prev_renew);
    // Large chained pool buffers: copying only into the first payload corrupts memory.
    memset(eth, 0xa5, sizeof(eth));
    assert(tud_network_recv_cb(eth, sizeof(eth)));
    assert(received_frame->next);
    uint8_t copied[1600]; assert(pbuf_copy_partial(received_frame,copied,1600,0) == 1600);
    assert(!memcmp(eth,copied,1600)); tud_network_init_cb(); assert(!received_frame);
    // Failed allocation must leave the receiver able to retry.
    struct pbuf *held[64]; unsigned n = 0;
    while (n < 64 && (held[n] = pbuf_alloc(PBUF_RAW, 1500, PBUF_POOL))) ++n;
    assert(n < 64 && !tud_network_recv_cb(eth, 100));
    while (n) pbuf_free(held[--n]);
    service_traffic(); assert(tud_network_recv_cb(eth, 100)); tud_network_init_cb();
    struct pbuf *p = pbuf_alloc(PBUF_RAW,100,PBUF_RAM); assert(p);
    tx_available = false; assert(linkoutput_fn(&netif_data,p) == ERR_MEM); pbuf_free(p);
    puts("PASS: actual lwIP/NCM DISCOVER-OFFER/REQUEST-ACK from 0.0.0.0, long requests, chained pbufs, backpressure/OOM, TX timeout");
}
