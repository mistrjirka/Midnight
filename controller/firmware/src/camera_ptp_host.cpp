/* Canon/Midnight read-only PTP poller over the ESP32-S3 USB OTG host.
 * Disabled until HOST START, because its USB PHY is shared with USB JTAG.
 * It never sends a camera operation that can move the mount or shutter.
 * GPL-2.0-or-later.
 */
#include "camera_ptp_host.h"
#include <Arduino.h>
#include <cstring>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "usb/usb_host.h"
#include "esp_err.h"

#if !SOC_USB_OTG_SUPPORTED
#error "The pointing host requires an ESP32-S3 with USB OTG"
#endif

extern "C" void USBHostBoardInit(void);
namespace {
static constexpr uint16_t OP_OPEN_SESSION=0x1002;
static constexpr uint16_t OP_MIDNIGHT_POINT=0x9B32;
static constexpr uint16_t RESPONSE_OK=0x2001;
static constexpr uint32_t POLL_INTERVAL_MS=1500;
static constexpr uint32_t TRANSFER_TIMEOUT_MS=2000;
static usb_host_client_handle_t client=nullptr;
static usb_device_handle_t device=nullptr;
static usb_transfer_t *outTransfer=nullptr,*inTransfer=nullptr;
static uint8_t bulkOut=0,bulkIn=0,iface=0,alternate=0;
static volatile bool gone=false;
static volatile bool transferDone=false;
static volatile bool transferInFlight=false;
static volatile bool running=false;
static volatile const char *state="OFF";
static uint32_t tid=0,lastPoll=0;
static bool sessionOpened=false,claimed=false;
static CameraPointing recent={};
static uint32_t recentTime=0;
static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;

static void setState(const char *s) { state=s; }
static uint16_t u16(const uint8_t*p) {return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t u32(const uint8_t*p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|
           (uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static void w16(uint8_t*p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
static void w32(uint8_t*p,uint32_t v){
    for(unsigned i=0;i<4;i++)p[i]=uint8_t(v>>(i*8));
}
static void complete(usb_transfer_t*) {
    transferDone=true;
    transferInFlight=false;
}
static bool transfer(usb_transfer_t *item, uint32_t timeout=TRANSFER_TIMEOUT_MS) {
    if(gone || !item || transferInFlight) return false;
    transferDone=false;
    if (usb_host_transfer_submit(item)!=ESP_OK) return false;
    transferInFlight=true;
    uint32_t start=millis();
    while(!transferDone && !gone && uint32_t(millis()-start)<timeout)
        usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
    if(!transferDone) {
        /* A delayed callback may arrive after a timeout. Do not free or
         * reuse the DMA buffer while it is still owned by the driver. */
        usb_host_endpoint_halt(device,item->bEndpointAddress);
        usb_host_endpoint_flush(device,item->bEndpointAddress);
        uint32_t flushed=millis();
        while(!transferDone && uint32_t(millis()-flushed)<300)
            usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        return false;
    }
    return item->status==USB_TRANSFER_STATUS_COMPLETED;
}
static bool writeCmd(uint16_t opcode,uint32_t txn,bool param=false,uint32_t argument=0) {
    if(!outTransfer)return false;
    uint8_t *p=outTransfer->data_buffer;
    w32(p,param?16:12);w16(p+4,1);w16(p+6,opcode);w32(p+8,txn);
    if(param)w32(p+12,argument);
    outTransfer->num_bytes=param?16:12;
    return transfer(outTransfer);
}
static bool readPtp(uint16_t expectedType,uint16_t expectedCode,
                    uint32_t txn,uint8_t *payload=nullptr,size_t expectedLength=0) {
    if(!inTransfer)return false;
    inTransfer->num_bytes=256; // Full-speed bulk IN requests must be MPS-aligned.
    if(!transfer(inTransfer))return false;
    size_t n=inTransfer->actual_num_bytes;
    const uint8_t *p=inTransfer->data_buffer;
    if(n<12 || u32(p)!=n || u16(p+4)!=expectedType ||
       u16(p+6)!=expectedCode || u32(p+8)!=txn ||
       n!=12+expectedLength)return false;
    if(expectedLength && payload)memcpy(payload,p+12,expectedLength);
    return true;
}
static bool execute(uint16_t opcode,bool hasParam=false,uint32_t argument=0,
                    uint8_t *reply=nullptr,size_t len=0) {
    const uint32_t transaction=++tid;
    if(!writeCmd(opcode,transaction,hasParam,argument))return false;
    if(len && !readPtp(2,opcode,transaction,reply,len))return false;
    return readPtp(3,RESPONSE_OK,transaction);
}
static bool findImageInterface(const usb_config_desc_t *cfg) {
    if(!cfg || cfg->wTotalLength<cfg->bLength)return false;
    int currentInterface=-1,alt=0;
    bool ptp=false;
    uint8_t epIn=0,epOut=0;
    const uint8_t *p=reinterpret_cast<const uint8_t *>(cfg);
    size_t off=0;
    while(off+2<=cfg->wTotalLength) {
        uint8_t size=p[off],kind=p[off+1];
        if(size<2 || off+size>cfg->wTotalLength)break;
        if(kind==4 && size>=9) {
            if(ptp && epIn && epOut) {
                iface=(uint8_t)currentInterface;alternate=(uint8_t)alt;
                bulkIn=epIn;bulkOut=epOut;return true;
            }
            currentInterface=p[off+2];alt=p[off+3];
            ptp=(p[off+5]==6 && p[off+6]==1 && p[off+7]==1);
            epIn=epOut=0;
        } else if(kind==5 && size>=7 && ptp && (p[off+3]&3)==2) {
            uint8_t addr=p[off+2];
            if(addr&0x80)epIn=addr;
            else epOut=addr;
        }
        off+=size;
    }
    if(ptp && epIn && epOut) {
        iface=(uint8_t)currentInterface;alternate=(uint8_t)alt;
        bulkIn=epIn;bulkOut=epOut;return true;
    }
    return false;
}
static void disconnect() {
    /* Do not free a DMA buffer until its completion callback has returned.
     * A disconnected camera can leave a canceled transfer pending briefly. */
    if(transferInFlight)return;
    if(outTransfer) usb_host_transfer_free(outTransfer);
    if(inTransfer) usb_host_transfer_free(inTransfer);
    outTransfer=inTransfer=nullptr;
    if(claimed && device)usb_host_interface_release(client,device,iface);
    if(device)usb_host_device_close(client,device);
    device=nullptr;claimed=false;sessionOpened=false;gone=false;
    setState("AWAIT_CAMERA");
}
static void newDevice(uint8_t address) {
    if(device) return;
    usb_device_handle_t dev=nullptr;
    if(usb_host_device_open(client,address,&dev)!=ESP_OK)return;
    const usb_config_desc_t *cfg=nullptr;
    if(usb_host_get_active_config_descriptor(dev,&cfg)!=ESP_OK ||
       !findImageInterface(cfg)){
        usb_host_device_close(client,dev);
        setState("NOT_A_PTP_CAMERA");return;
    }
    if(usb_host_interface_claim(client,dev,iface,alternate)!=ESP_OK) {
        usb_host_device_close(client,dev);setState("PTP_CLAIM_FAILED");return;
    }
    claimed=true;device=dev;gone=false;
    if(usb_host_transfer_alloc(256,0,&outTransfer)!=ESP_OK ||
       usb_host_transfer_alloc(256,0,&inTransfer)!=ESP_OK) {
        disconnect();setState("NO_USB_BUFFERS");return;
    }
    for(usb_transfer_t *t:{outTransfer,inTransfer}) {
        t->device_handle=device;
        t->callback=complete;
        t->context=nullptr;
    }
    outTransfer->bEndpointAddress=bulkOut;
    inTransfer->bEndpointAddress=bulkIn;
    setState("PTP_OPENING");
    sessionOpened=false;
}
static void event(const usb_host_client_event_msg_t *msg,void*) {
    if(msg->event==USB_HOST_CLIENT_EVENT_NEW_DEV)
        newDevice(msg->new_dev.address);
    if(msg->event==USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl==device)
        gone=true;
}
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
static bool filter(const usb_device_desc_t *,uint8_t *config) {
    *config=1;return true;
}
#endif
static void daemon(void*) {
    for(;;){
        uint32_t flags=0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100),&flags);
        if(flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            usb_host_device_free_all();
    }
}
static void clientTask(void*) {
    for(;;) {
        usb_host_client_handle_events(client,pdMS_TO_TICKS(40));
        if(gone){
            if(!transferInFlight)disconnect();
            continue;
        }
        if(transferInFlight)continue;
        if(!device)continue;
        if(!sessionOpened){
            if(execute(OP_OPEN_SESSION,true,1)) {
                sessionOpened=true;setState("PTP_OPEN");
            } else {setState("PTP_OPEN_ERROR");delay(800);}
            continue;
        }
        if(uint32_t(millis()-lastPoll)<POLL_INTERVAL_MS)continue;
        lastPoll=millis();
        uint8_t bytes[32]={};
        if(!execute(OP_MIDNIGHT_POINT,false,0,bytes,sizeof(bytes))) {
            setState("PTP_READ_ERROR");
            continue;
        }
        CameraPointing p={};
        if(parseCameraPointing(bytes,sizeof(bytes),p) && p.valid) {
            portENTER_CRITICAL(&mutex);
            recent=p;recentTime=millis();
            portEXIT_CRITICAL(&mutex);
            setState("POINT_READY");
        } else setState("NO_VERIFIED_POINT");
    }
}
}
void cameraHostStart(){
    if(running)return;
    USBHostBoardInit(); // Board-specific VBUS enable, if implemented.
    usb_host_config_t config={};
    config.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    config.enum_filter_cb=filter;
#endif
    if(usb_host_install(&config)!=ESP_OK){setState("HOST_INSTALL_FAILED");return;}
    usb_host_client_config_t cc={};
    cc.is_synchronous=false;cc.max_num_event_msg=5;
    cc.async.client_event_callback=event;
    if(usb_host_client_register(&cc,&client)!=ESP_OK){
        setState("HOST_CLIENT_FAILED");return;
    }
    xTaskCreate(daemon,"mid_usb_daemon",4096,nullptr,2,nullptr);
    xTaskCreate(clientTask,"mid_ptp",6144,nullptr,3,nullptr);
    running=true;setState("AWAIT_CAMERA");
}
bool cameraHostRunning(){return running;}
const char *cameraHostState(){return (const char*)state;}
bool cameraHostLatest(CameraPointing &out,uint32_t &at){
    portENTER_CRITICAL(&mutex);
    out=recent;at=recentTime;
    portEXIT_CRITICAL(&mutex);
    return out.valid;
}
