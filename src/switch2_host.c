// Switch 2 controllers use a separate vendor BULK interface for commands.
// HID SET_REPORT cannot activate them. Own only this interface; TinyUSB's
// HID driver receives button/stick reports on interface 0 as usual.
#include <string.h>
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "gamepad.h"
#include "switch2_host.h"

typedef struct {
    uint8_t dev, itf, ep_in, ep_out, stage, attempts;
    bool configured, waiting, rx_pending, tx_pending;
    uint32_t deadline;
    uint8_t rx[64] TU_ATTR_ALIGNED(4);
    uint8_t tx[64] TU_ATTR_ALIGNED(4);
} command_itf_t;
static command_itf_t commands[CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1];

static bool driver_init(void) { memset(commands, 0, sizeof(commands)); return true; }
static bool driver_deinit(void) { return true; }

static uint16_t driver_open(uint8_t rhport, uint8_t dev,
                            const tusb_desc_interface_t *itf, uint16_t max_len) {
    (void)rhport;
    uint16_t vid, pid;
    tuh_vid_pid_get(dev, &vid, &pid);
    gamepad_kind_t kind = gamepad_identify(vid, pid);
    if (kind < PAD_SWITCH2_PRO || itf->bInterfaceClass != 0xff ||
        itf->bInterfaceNumber != 1 || itf->bAlternateSetting ||
        itf->bNumEndpoints != 2 || dev >= TU_ARRAY_SIZE(commands)) return 0;
    uint16_t used = sizeof(*itf);
    const uint8_t *p = (const uint8_t *)itf + used;
    uint8_t in = 0, out = 0;
    const tusb_desc_endpoint_t *endpoints[2];
    for (int i = 0; i < 2; ++i) {
        if (used + sizeof(tusb_desc_endpoint_t) > max_len ||
            p[0] != sizeof(tusb_desc_endpoint_t) || p[1] != TUSB_DESC_ENDPOINT) return 0;
        const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)p;
        if (ep->bmAttributes.xfer != TUSB_XFER_BULK || tu_edpt_packet_size(ep) != 64) return 0;
        if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) in = ep->bEndpointAddress;
        else out = ep->bEndpointAddress;
        endpoints[i] = ep;
        p += ep->bLength; used += ep->bLength;
    }
    if (!in || !out) return 0;
    for (int i = 0; i < 2; ++i) if (!tuh_edpt_open(dev, endpoints[i])) return 0;
    command_itf_t *c = &commands[dev];
    memset(c, 0, sizeof(*c));
    c->dev = dev; c->itf = itf->bInterfaceNumber; c->ep_in = in; c->ep_out = out;
    return used;
}

static bool driver_config(uint8_t dev, uint8_t itf) {
    commands[dev].configured = true;
    usbh_driver_set_config_complete(dev, itf);
    return true;
}

static bool driver_xfer(uint8_t dev, uint8_t ep, xfer_result_t result, uint32_t len) {
    command_itf_t *c = &commands[dev];
    if (ep == c->ep_out) {
        c->tx_pending = false;
        if (result != XFER_RESULT_SUCCESS) c->deadline = 0;
    } else if (ep == c->ep_in) {
        c->rx_pending = false;
        uint8_t subcmd = c->stage == 0 ? 0x0d : 0x0a;
        if (c->waiting && result == XFER_RESULT_SUCCESS && len >= 8 &&
            c->rx[0] == 3 && c->rx[1] == 1 && c->rx[2] == 0 &&
            c->rx[3] == subcmd && c->rx[5] == 0xf8) {
            c->stage++; c->waiting = false; c->attempts = 0; c->deadline = 0;
        }
    }
    return true;
}

static void driver_close(uint8_t dev) { memset(&commands[dev], 0, sizeof(commands[dev])); }

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *count) {
    static const usbh_class_driver_t driver = {
        .name = "Switch2 commands", .init = driver_init, .deinit = driver_deinit,
        .open = driver_open, .set_config = driver_config,
        .xfer_cb = driver_xfer, .close = driver_close
    };
    *count = 1;
    return &driver;
}

void switch2_host_task(uint32_t now) {
    for (size_t i = 1; i < TU_ARRAY_SIZE(commands); ++i) {
        command_itf_t *c = &commands[i];
        if (!c->configured) continue;
        if (!c->rx_pending)
            c->rx_pending = usbh_edpt_xfer(c->dev, c->ep_in, c->rx, sizeof(c->rx));
        if (c->stage >= 2 || c->tx_pending || (int32_t)(now - c->deadline) < 0) continue;
        if (c->attempts >= 4) {
            c->stage = 0; c->attempts = 0; c->waiting = false;
            c->deadline = now + 1000;
            continue;
        }
        memset(c->tx, 0, sizeof(c->tx));
        c->tx[0] = 3; c->tx[1] = 0x91;
        c->tx[3] = c->stage == 0 ? 0x0d : 0x0a;
        c->tx[5] = c->stage == 0 ? 8 : 4;
        c->tx[8] = c->stage == 0 ? 1 : 5; // USB enable / common input report
        if (c->stage == 0) c->tx[10] = 2; // locally administered host address
        c->tx_pending = usbh_edpt_xfer(c->dev, c->ep_out, c->tx, 8 + c->tx[5]);
        if (c->tx_pending) {
            c->waiting = true; c->attempts++; c->deadline = now + 300;
        }
    }
}
