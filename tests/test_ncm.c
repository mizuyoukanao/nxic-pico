// Exercise the actual NCM class driver and USB descriptors. Only USB hardware
// transfers are mocked, so GET_NTB_PARAMETERS tests the response Windows sees.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "class/net/ncm.h"

extern bool usb_config_mode;
uint8_t tud_network_mac_address[6] = {2, 0x4e, 0x58, 0x12, 0x34, 0x56};
static ntb_parameters_t parameters;
static uint8_t *rx_buffer, *tx_buffer;
static uint16_t rx_capacity, tx_size;
static bool busy[256];
static unsigned received;
static uint8_t rx_frame[1514];

void pico_get_unique_board_id_string(char *dst, unsigned len) { snprintf(dst,len,"0123456789ABCDEF"); }
tusb_speed_t tud_speed_get(void) { return TUSB_SPEED_FULL; }
bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t *ep) {
    assert(port == 0);
    assert(ep->bEndpointAddress == 0x81 && ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT);
    return true;
}
bool usbd_open_edpt_pair(uint8_t port, const uint8_t *desc, uint8_t count, uint8_t type,
                        uint8_t *out, uint8_t *in) {
    assert(port == 0 && count == 2 && type == TUSB_XFER_BULK);
    for (int i = 0; i < 2; ++i) {
        const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)desc;
        assert(ep->bmAttributes.xfer == TUSB_XFER_BULK && tu_edpt_packet_size(ep) == 64);
        if (tu_edpt_dir(ep->bEndpointAddress)) *in = ep->bEndpointAddress;
        else *out = ep->bEndpointAddress;
        desc += ep->bLength;
    }
    assert(*out == 2 && *in == 0x82);
    return true;
}
bool usbd_edpt_busy(uint8_t port, uint8_t ep) { assert(port == 0); return busy[ep]; }
bool usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t *buffer, uint16_t n, bool isr) {
    assert(port == 0 && !isr && !busy[ep]); busy[ep] = true;
    if (ep == 2) { rx_buffer = buffer; rx_capacity = n; }
    else if (ep == 0x82) { tx_buffer = buffer; tx_size = n; }
    else assert(ep == 0x81);
    return true;
}
bool tud_control_xfer(uint8_t port, const tusb_control_request_t *r, void *buffer, uint16_t n) {
    assert(port == 0);
    if (r->bRequest == NCM_GET_NTB_PARAMETERS) {
        assert(n == sizeof(parameters)); memcpy(&parameters,buffer,n);
    } else assert(r->bRequest == TUSB_REQ_GET_INTERFACE && n == 1 && *(uint8_t *)buffer == 1);
    return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t *r) {
    assert(port == 0 && r->bRequest == TUSB_REQ_SET_INTERFACE); return true;
}
bool tud_network_recv_cb(const uint8_t *src, uint16_t n) {
    assert(n == sizeof(rx_frame)); memcpy(rx_frame,src,n); received++; return true;
}
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t n) { memcpy(dst,ref,n); return n; }

static bool valid_divisor(unsigned divisor, unsigned size, unsigned remainder) {
    return divisor >= 4 && !(divisor & (divisor - 1)) && divisor <= size && remainder < divisor;
}
static bool windows_accepts_ntb(void) {
    return valid_divisor(parameters.wNdbInDivisor,parameters.dwNtbInMaxSize,parameters.wNdbInPayloadRemainder) &&
           valid_divisor(parameters.wNdbOutDivisor,parameters.dwNtbOutMaxSize,parameters.wNdbOutPayloadRemainder);
}
static void complete(uint8_t ep, uint32_t n) {
    busy[ep] = false; assert(netd_xfer_cb(0,ep,XFER_RESULT_SUCCESS,n));
}
static void check_tx(unsigned expected_count) {
    assert(tx_buffer && tx_size);
    const nth16_t *nth = (const nth16_t *)tx_buffer;
    assert(nth->dwSignature == NTH16_SIGNATURE && nth->wBlockLength == tx_size);
    assert(nth->wNdpIndex % parameters.wNdbInAlignment == 0);
    const ndp16_t *ndp = (const ndp16_t *)(tx_buffer + nth->wNdpIndex);
    assert(ndp->dwSignature == NDP16_SIGNATURE_NCM0);
    const ndp16_datagram_t *d = (const ndp16_datagram_t *)(ndp+1);
    unsigned n = 0;
    while (d[n].wDatagramIndex) {
        assert(n < expected_count);
        assert(d[n].wDatagramIndex % parameters.wNdbInDivisor == parameters.wNdbInPayloadRemainder);
        assert(d[n].wDatagramIndex + d[n].wDatagramLength <= tx_size);
        n++;
    }
    assert(n == expected_count);
}

int main(void) {
    usb_config_mode = true;
    const tusb_desc_device_t *dev = (const tusb_desc_device_t *)tud_descriptor_device_cb();
    assert(dev->bDeviceClass == TUSB_CLASS_MISC && dev->bDeviceSubClass == 2 && dev->bDeviceProtocol == 1);
    const uint8_t *cfg = tud_descriptor_configuration_cb(0);
    const tusb_desc_configuration_t *config = (const tusb_desc_configuration_t *)cfg;
    assert(config->bNumInterfaces == 2);
    const tusb_desc_interface_t *control = NULL;
    unsigned alt0 = 0, alt1 = 0, mac_index = 0;
    for (unsigned i = config->bLength; i < config->wTotalLength; i += cfg[i]) {
        assert(cfg[i] && i + cfg[i] <= config->wTotalLength);
        if (cfg[i+1] == TUSB_DESC_INTERFACE) {
            const tusb_desc_interface_t *itf = (const tusb_desc_interface_t *)(cfg+i);
            if (itf->bInterfaceClass == TUSB_CLASS_CDC) { control = itf; assert(itf->bNumEndpoints == 1); }
            if (itf->bInterfaceClass == TUSB_CLASS_CDC_DATA) {
                assert(itf->bInterfaceNumber == 1 && itf->bInterfaceProtocol == 1);
                if (!itf->bAlternateSetting) { alt0++; assert(!itf->bNumEndpoints); }
                else { alt1++; assert(itf->bAlternateSetting == 1 && itf->bNumEndpoints == 2); }
            }
        } else if (cfg[i+1] == TUSB_DESC_CS_INTERFACE && cfg[i+2] == CDC_FUNC_DESC_ETHERNET_NETWORKING) {
            mac_index = cfg[i+3];
        }
    }
    assert(control && alt0 == 1 && alt1 == 1 && mac_index);
    const uint16_t *mac = tud_descriptor_string_cb(mac_index,0x0409);
    assert((mac[0] & 255) == 26);
    for (int i = 1; i <= 12; ++i) assert((mac[i] >= '0' && mac[i] <= '9') || (mac[i] >= 'A' && mac[i] <= 'F'));
    netd_init();
    unsigned remaining = config->wTotalLength - ((const uint8_t *)control - cfg);
    assert(netd_open(0,control,remaining) == remaining);
    tusb_control_request_t request = {.bmRequestType=0xa1,.bRequest=NCM_GET_NTB_PARAMETERS,.wLength=sizeof(parameters)};
    assert(netd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    assert(parameters.wLength == 28 && parameters.bmNtbFormatsSupported == 1);
#ifdef TEST_NCM_BEFORE_FIX
    assert(!windows_accepts_ntb());
    puts("PASS: original NCM GET_NTB_PARAMETERS fails Windows divisor validation (Code 10)");
    return 0;
#else
    assert(windows_accepts_ntb());
#endif
    // Enter the data interface's active alternate setting, as Windows does.
    request = (tusb_control_request_t){.bmRequestType=1,.bRequest=TUSB_REQ_SET_INTERFACE,.wIndex=1,.wValue=1};
    assert(netd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    request.bmRequestType=0x81;request.bRequest=TUSB_REQ_GET_INTERFACE;
    assert(netd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    assert(rx_buffer && rx_capacity >= 2048);
    complete(0x81,16);complete(0x81,8); // speed and link notifications
    // Include Ethernet lengths needing 0/1/2/3 bytes of padding. Exercise an
    // aggregated NTB too: advertised alignment must match actual transfers.
    uint8_t frame[1514]; memset(frame,0x5a,sizeof(frame));
    assert(tud_network_can_xmit(61));tud_network_xmit(frame,61);check_tx(1);
    assert(!tud_network_can_xmit(62)); // one NTB buffer: backpressure while in flight
    complete(0x82,tx_size);
    request = (tusb_control_request_t){.bmRequestType=1,.bRequest=TUSB_REQ_SET_INTERFACE,.wIndex=1,.wValue=0};
    assert(netd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    assert(tud_network_can_xmit(62));tud_network_xmit(frame,62);
    assert(tud_network_can_xmit(63));tud_network_xmit(frame,63);
    assert(tud_network_can_xmit(65));tud_network_xmit(frame,65);
    request.wValue=1;assert(netd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&request));
    assert(tud_network_can_xmit(66));tud_network_xmit(frame,66);check_tx(4);complete(0x82,tx_size);
    if (busy[0x82] && !tx_size) complete(0x82,0);
    assert(tud_network_can_xmit(sizeof(frame)));tud_network_xmit(frame,sizeof(frame));check_tx(1);
    complete(0x82,tx_size);
    // Host -> device: construct a conforming 4-byte-aligned NTB.
    memset(rx_buffer,0,rx_capacity);
    nth16_t nth = {NTH16_SIGNATURE,12,0,28+sizeof(frame),12};
    ndp16_t ndp = {NDP16_SIGNATURE_NCM0,16,0};
    ndp16_datagram_t d = {28,sizeof(frame)};
    memcpy(rx_buffer,&nth,12);memcpy(rx_buffer+12,&ndp,8);memcpy(rx_buffer+20,&d,4);memcpy(rx_buffer+28,frame,sizeof(frame));
    complete(2,nth.wBlockLength);
    assert(received == 1 && !memcmp(frame,rx_frame,sizeof(frame)));
    assert(netd_deinit());
    puts("PASS: NCM USB descriptors, Windows NTB parameters, alternate setting, notifications, aligned TX aggregation and RX");
}
