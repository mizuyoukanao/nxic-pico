// Actual HID callbacks, initialisation tasks and mapping; USB calls are mocked.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "pico/time.h"
#include "hid_input.h"
#include "mapping.h"
#include "settings.h"

uint32_t test_now;
settings_t g_settings;
static uint8_t protocols[16][2];
static uint8_t last_rid, last_body[64], last_dev, last_idx;
static uint8_t *bulk_rx, *bulk_tx;
static uint16_t bulk_len;
static unsigned sends, receives;
static bool can_send = true, can_receive = true, can_bulk = true;
static const uint16_t products[] = {0,0x2009,0x200e,0x2069,0,0,0x2067};
bool tuh_vid_pid_get(uint8_t dev,uint16_t *vid,uint16_t *pid) {
    *vid = 0x057e; *pid = products[dev]; return true;
}
uint8_t tuh_hid_interface_protocol(uint8_t dev,uint8_t idx) {
    (void)idx; return dev == 4 ? HID_ITF_PROTOCOL_KEYBOARD : dev == 5 ? HID_ITF_PROTOCOL_MOUSE : 0;
}
uint8_t tuh_hid_get_protocol(uint8_t dev,uint8_t idx) { return protocols[dev][idx]; }
bool tuh_hid_set_protocol(uint8_t dev,uint8_t idx,uint8_t proto) { protocols[dev][idx]=proto; return true; }
bool tuh_hid_receive_report(uint8_t dev,uint8_t idx) { (void)dev; (void)idx; receives++; return can_receive; }
bool tuh_hid_receive_ready(uint8_t dev,uint8_t idx) { (void)dev; (void)idx; return true; }
bool tuh_hid_send_ready(uint8_t dev,uint8_t idx) { (void)dev; (void)idx; return can_send; }
bool tuh_hid_send_report(uint8_t dev,uint8_t idx,uint8_t rid,const void *body,uint16_t n) {
    last_dev=dev;last_idx=idx;last_rid=rid;memcpy(last_body,body,n);sends++;return true;
}
bool tuh_edpt_open(uint8_t dev,const tusb_desc_endpoint_t *ep) { (void)dev;(void)ep;return true; }
void usbh_driver_set_config_complete(uint8_t dev,uint8_t itf) { (void)dev;(void)itf; }
bool usbh_edpt_xfer_with_callback(uint8_t dev,uint8_t ep,uint8_t *buffer,uint16_t n,tuh_xfer_cb_t cb,uintptr_t user) {
    (void)dev;(void)cb;(void)user;
    if (!can_bulk) return false;
    if (ep == 0x82) bulk_rx=buffer; else { assert(ep==2);bulk_tx=buffer;bulk_len=n; }
    return true;
}
static void pack(uint8_t *p,unsigned x,unsigned y) { p[0]=x;p[1]=(x>>8)|(y<<4);p[2]=y>>4; }
static unsigned axis(const controller_state_t *s,int i) {
    const uint8_t *p=s->stick+(i/2)*3;
    return i%2 ? (p[1]>>4)|(p[2]<<4) : p[0]|((p[1]&15)<<8);
}
static void test_switch1_init(void) {
    hid_input_init();
    for (int d=0;d<16;++d) for(int i=0;i<2;++i) protocols[d][i]=HID_PROTOCOL_REPORT;
    tuh_hid_mount_cb(1,0,NULL,0);
    for (int stage=0;stage<8;++stage) {
        unsigned prev=sends;hid_input_task();assert(sends==prev+1 && last_dev==1);
        uint8_t ack[64]={0};
        if(stage<4) {
            static const uint8_t cmds[]={1,2,3,2};
            assert(last_rid==0x80 && last_body[0]==cmds[stage]);
            ack[0]=0x81;ack[1]=cmds[stage];ack[3]=3;
        } else if(stage==4) {
            assert(last_rid==0x80 && last_body[0]==4);test_now+=20;continue;
        } else {
            assert(last_rid==1);
            ack[0]=0x21;ack[13]=0x80;
            pack(ack+6,0x800,0x800);pack(ack+9,0x800,0x800);
            ack[14]=stage==5 ? 3 : 0x10;
            if(stage==5) assert(last_body[9]==3 && last_body[10]==0x30);
            else {
                uint16_t addr=stage==6 ? 0x603d : 0x6046;
                assert(last_body[9]==0x10 && last_body[10]==(addr&255) && last_body[14]==9);
                ack[15]=addr;ack[16]=addr>>8;ack[19]=9;
                // Valid factory calibration, same nominal center and travel.
                pack(ack+20,stage==6 ? 0x700 : 0x800,stage==6 ? 0x700 : 0x800);
                pack(ack+23,stage==6 ? 0x800 : 0x700,stage==6 ? 0x800 : 0x700);
                pack(ack+26,0x700,0x700);
            }
        }
        tuh_hid_report_received_cb(1,0,ack,sizeof(ack));
    }
    unsigned prev=sends;hid_input_task();assert(prev==sends);
}
static void test_mixed_input(void) {
    memset(&g_settings,255,sizeof(g_settings));
    g_settings.yaw_counts_per_360=8000;g_settings.pitch_counts_per_360=8000;
    g_settings.pitch_limit_deg=80;g_settings.invert_yaw=0;g_settings.invert_pitch=0;
    g_settings.key_btn_a=HID_KEY_L;g_settings.key_ls_right=HID_KEY_D;g_settings.key_ls_left=HID_KEY_A;
    tuh_hid_mount_cb(4,0,NULL,0);tuh_hid_mount_cb(5,0,NULL,0);tuh_hid_mount_cb(3,0,NULL,0);
    assert(protocols[4][0]==HID_PROTOCOL_BOOT && protocols[5][0]==HID_PROTOCOL_BOOT);
    assert(protocols[3][0]==HID_PROTOCOL_REPORT); // never force pad into BOOT
    uint8_t pad[64]={0x30};pad[3]=4;pack(pad+6,2300,2048);pack(pad+9,2048,2048);
    tuh_hid_report_received_cb(1,0,pad,64);
    uint8_t pad2[64]={9};pad2[4]=16;pack(pad2+6,2048,2048);pack(pad2+9,2400,2048);
    tuh_hid_report_received_cb(3,0,pad2,64);
    uint8_t kb[8]={0,0,HID_KEY_L};tuh_hid_report_received_cb(4,0,kb,8);
    uint8_t mouse[3]={MOUSE_BUTTON_LEFT,10,0};tuh_hid_report_received_cb(5,0,mouse,3);
    controller_state_t s;mapping_get_state(&s);
    assert(s.btn[0]==(4|8|128) && (s.btn[2]&64));
    assert(axis(&s,0)>2048 && axis(&s,0)<2300 && axis(&s,2)>2048);
    assert(s.imu[0][5]!=0); // mouse aiming remains active while pads are held
    kb[2]=HID_KEY_D;tuh_hid_report_received_cb(4,0,kb,8);mapping_get_state(&s);assert(axis(&s,0)==4095);
    kb[3]=HID_KEY_A;tuh_hid_report_received_cb(4,0,kb,8);mapping_get_state(&s);assert(axis(&s,0)==2048);
    memset(kb,0,8);tuh_hid_report_received_cb(4,0,kb,8);mapping_get_state(&s);assert(axis(&s,0)>2048 && axis(&s,0)<2300);
    tuh_hid_umount_cb(1,0);mapping_get_state(&s);assert(axis(&s,0)==2048 && !(s.btn[0]&4));
    test_now+=501;hid_input_task();mapping_get_state(&s);assert(axis(&s,2)==2048 && !(s.btn[2]&64));
    // Malformed keyboard packets never become button presses.
    kb[2]=HID_KEY_L;tuh_hid_report_received_cb(4,0,kb,7);assert(!hid_key_down(HID_KEY_L));
    unsigned prev=receives;can_receive=false;hid_input_task();can_receive=true;hid_input_task();assert(receives>prev);
    tuh_hid_umount_cb(3,0);tuh_hid_umount_cb(4,0);tuh_hid_umount_cb(5,0);
}
static void test_grip(void) {
    uint8_t r[64]={0x81,1,0,1};
    tuh_hid_mount_cb(2,0,NULL,0);hid_input_task();assert(last_rid==0x80 && last_body[0]==1);
    tuh_hid_report_received_cb(2,0,r,64);
    r[0]=0x30;r[3]=0xff;r[4]=0xff;r[5]=0xff;
    pack(r+6,2400,2048);pack(r+9,4095,4095);
    tuh_hid_report_received_cb(2,0,r,64);
    gamepad_state_t state;hid_gamepad_state(&state);assert(state.axes==3 && !state.buttons[0] && state.axis[2]==2048);
    tuh_hid_mount_cb(2,1,NULL,0);hid_input_task();assert(last_idx==1);
    r[0]=0x81;r[1]=1;r[3]=2;tuh_hid_report_received_cb(2,1,r,64);
    r[0]=0x30;r[3]=8;r[4]=0;r[5]=0;
    pack(r+6,4095,4095);pack(r+9,2048,2400);tuh_hid_report_received_cb(2,1,r,64);
    hid_gamepad_state(&state);assert(state.axes==15 && state.axis[0]<2400 && state.axis[3]>2048 && state.buttons[0]==8);
    tuh_hid_umount_cb(2,0);hid_gamepad_state(&state);assert(state.axis[0]==2048 && state.axis[3]>2048);
    tuh_hid_umount_cb(2,1);
}
static void test_switch2_bulk(void) {
    uint8_t count;const usbh_class_driver_t *drv=usbh_app_driver_get_cb(&count);assert(count==1);assert(drv->init());
    const uint8_t descriptor[]={9,4,1,0,2,255,0,0,0,7,5,2,2,64,0,0,7,5,0x82,2,64,0,0};
    assert(!drv->open(1,1,(const tusb_desc_interface_t *)descriptor,sizeof(descriptor)));
    assert(drv->open(1,3,(const tusb_desc_interface_t *)descriptor,sizeof(descriptor))==sizeof(descriptor));
    assert(drv->set_config(3,1));
    can_bulk=false;hid_input_task();assert(!bulk_tx);can_bulk=true;hid_input_task();
    assert(bulk_tx && bulk_rx && bulk_len==16 && bulk_tx[0]==3 && bulk_tx[1]==0x91 && bulk_tx[3]==0x0d && bulk_tx[8]==1);
    drv->xfer_cb(3,2,XFER_RESULT_SUCCESS,bulk_len);
    uint8_t wrong[]={3,1,0,0x0a,0,0xf8,0,0};memcpy(bulk_rx,wrong,8);drv->xfer_cb(3,0x82,XFER_RESULT_SUCCESS,8);
    hid_input_task();assert(bulk_tx[3]==0x0d); // unrelated ACK cannot advance state
    wrong[3]=0x0d;memcpy(bulk_rx,wrong,8);drv->xfer_cb(3,0x82,XFER_RESULT_SUCCESS,8);
    hid_input_task();assert(bulk_len==12 && bulk_tx[3]==0x0a && bulk_tx[8]==5);
    drv->xfer_cb(3,2,XFER_RESULT_SUCCESS,12);wrong[3]=0x0a;memcpy(bulk_rx,wrong,8);
    drv->xfer_cb(3,0x82,XFER_RESULT_SUCCESS,8);hid_input_task();
    drv->close(3);bulk_tx=NULL;test_now+=2000;hid_input_task();assert(!bulk_tx);
    assert(drv->deinit());
}
int main(void) {
    test_switch1_init();test_mixed_input();test_grip();test_switch2_bulk();
    puts("PASS: USB host handshakes, charging-grip pair, Switch2 BULK activation/retry, actual mixed mapping, unplug/timeout release");
}
