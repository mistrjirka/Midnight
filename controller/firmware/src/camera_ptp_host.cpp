/* Canon/Midnight read-only PTP poller over the ESP32-S3 USB OTG host.
 * Disabled until HOST START, because its USB PHY is shared with USB JTAG.
 * It never sends a camera operation that can move the mount or shutter.
 * GPL-2.0-or-later.
 */
#include "camera_ptp_host.h"
#include "ptp_wire.h"
#include "mount_mailbox_wire.h"
#include "mount_coordinator.h"
#include "goto_loop.h"
#include "goto_motors.h"
#include <esp_system.h>
#include <Arduino.h>
#include <cstring>
#include <cstdarg>
#include <atomic>
#include <cstdio>
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
static constexpr uint16_t OP_CAPTURE_STILL=0x9B33;
static constexpr uint16_t OP_MOUNT_MAILBOX=0x9B34;
static constexpr uint16_t OP_MOUNT_PROGRESS=0x9B35;
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
static std::atomic<const char*> state{"OFF"};
static uint32_t tid=0,lastPoll=0;
static std::atomic<bool> sessionOpened{false};
static bool claimed=false;
static std::atomic<uint32_t> queuedPhotoId{0},activePhotoId{0};
static std::atomic<CameraPhotoState> photoStatus{CameraPhotoState::IDLE};
static std::atomic<uint32_t> lastIssuedPhotoId{0};
static uint32_t photoStartedAt=0;
static constexpr uint32_t CAPTURE_DEADLINE_MS=75000;
static MountCoordinator navigation;
static GoToLoop gotoLoop;
static GoToMotorPort gotoMotors;
static std::atomic<bool> cancelRequested{false},linkLostRequested{false};
static uint32_t lastProgressSeq=0,lastProgressError=0,lastProgressStep=0;
static uint32_t lastProgressAttempt=0;
static uint8_t lastProgressPhase=255,lastProgressStage=255;
static CameraMountMailbox lastMailbox={};
static bool mailboxValid=false;
static CameraMountView mountPublished={};
static CameraPointing recent={};
static uint32_t recentTime=0;
static PtpWireReader wireReader;
static constexpr size_t LOG_COUNT=32;
struct LogRecord {uint32_t ms;char message[92];};
static LogRecord logRecords[LOG_COUNT]={};
static size_t logHead=0,logUsed=0;
static std::atomic<uint32_t> txRequests{0},rxPackets{0},ptpErrors{0};
static portMUX_TYPE logMutex=portMUX_INITIALIZER_UNLOCKED;
static void hostLog(const char *fmt, ...) {
    char line[92]={};
    va_list args;
    va_start(args,fmt);
    vsnprintf(line,sizeof(line),fmt,args);
    va_end(args);
    portENTER_CRITICAL(&logMutex);
    logRecords[logHead].ms=millis();
    memcpy(logRecords[logHead].message,line,sizeof(line));
    logHead=(logHead+1)%LOG_COUNT;
    if(logUsed<LOG_COUNT)logUsed++;
    portEXIT_CRITICAL(&logMutex);
}

static portMUX_TYPE mutex=portMUX_INITIALIZER_UNLOCKED;
static void publishMountSnapshot(){
    CameraMountView snapshot={};
    snapshot.mailbox=lastMailbox;
    snapshot.mailboxValid=mailboxValid;
    snapshot.navigation=navigation.state();
    snapshot.targetActive=navigation.targetActive();
    snapshot.targetRa=navigation.targetRa();
    snapshot.targetDec=navigation.targetDec();
    snapshot.lastPhotoRa=navigation.lastPhotoRa();
    snapshot.lastPhotoDec=navigation.lastPhotoDec();
    snapshot.photoState=photoStatus.load();
    snapshot.lastRequestId=lastIssuedPhotoId.load();
    snapshot.loopPhase=gotoLoop.phase();
    snapshot.loopStage=gotoLoop.stage();
    snapshot.loopFault=gotoLoop.failure();
    snapshot.loopErrorE4=gotoLoop.errorE4();
    snapshot.loopCorrections=gotoLoop.steps();
    snapshot.autoMotorReady=gotoMotors.ready();
    snapshot.nextPulseValid=gotoLoop.nextPulse(snapshot.nextPulse);
    portENTER_CRITICAL(&mutex);
    mountPublished=snapshot;
    portEXIT_CRITICAL(&mutex);
}

static void setState(const char *s) {
    const char* previous=state.exchange(s,std::memory_order_acq_rel);
    if(strcmp(previous,s))hostLog("state %s -> %s",previous,s);
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
    transferInFlight=true; // Set before submit because callback may complete immediately.
    const esp_err_t submit=usb_host_transfer_submit(item);
    if (submit!=ESP_OK) {
        transferInFlight=false;
        hostLog("usb_submit ep=%02X error=%d",item->bEndpointAddress,submit);
        ptpErrors++;
        return false;
    }
    uint32_t start=millis();
    while(!transferDone && !gone && uint32_t(millis()-start)<timeout)
        usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
    if(!transferDone) {
        hostLog("usb_timeout ep=%02X elapsed_ms=%u",item->bEndpointAddress,
                (unsigned)(millis()-start));
        ptpErrors++;
        /* A delayed callback may arrive after a timeout. Do not free or
         * reuse the DMA buffer while it is still owned by the driver. */
        usb_host_endpoint_halt(device,item->bEndpointAddress);
        usb_host_endpoint_flush(device,item->bEndpointAddress);
        uint32_t flushed=millis();
        while(!transferDone && uint32_t(millis()-flushed)<300)
            usb_host_client_handle_events(client,pdMS_TO_TICKS(20));
        return false;
    }
    if(item->status!=USB_TRANSFER_STATUS_COMPLETED) {
        hostLog("usb_error ep=%02X status=%d",item->bEndpointAddress,item->status);
        ptpErrors++;
        return false;
    }
    rxPackets++;
    return true;
}
static bool writeCmd(uint16_t opcode,uint32_t txn,bool param=false,uint32_t argument=0) {
    if(!outTransfer)return false;
    uint8_t *p=outTransfer->data_buffer;
    w32(p,param?16:12);w16(p+4,1);w16(p+6,opcode);w32(p+8,txn);
    if(param)w32(p+12,argument);
    outTransfer->num_bytes=param?16:12;
    return transfer(outTransfer);
}
/* USB bulk transfers may split or combine PTP containers.
 * Preserve trailing bytes for the following PTP response. */
static bool readPtp(uint16_t expectedType,uint16_t expectedCode,
                    uint32_t txn,uint8_t *payload=nullptr,size_t expectedLength=0) {
    if(!inTransfer)return false;
    const uint32_t started=millis();
    for(int attempts=0;attempts<8 && millis()-started<5000;attempts++) {
        PtpContainer parsed={};
        const PtpWireReader::Result available=wireReader.next(parsed);
        if(available==PtpWireReader::BAD_FRAME) {
            hostLog("ptp_invalid_header txn=%lu",(unsigned long)txn);
            ptpErrors++;
            return false;
        }
        if(available==PtpWireReader::FRAME_READY) {
            if(parsed.type!=expectedType || parsed.code!=expectedCode ||
               parsed.transaction!=txn || parsed.payloadSize!=expectedLength) {
                hostLog("ptp_unexpected got=%u/%04X/%lu/%u expected=%u/%04X/%lu/%u",
                        parsed.type,parsed.code,(unsigned long)parsed.transaction,
                        (unsigned)parsed.payloadSize,expectedType,expectedCode,
                        (unsigned long)txn,(unsigned)expectedLength);
                ptpErrors++;
                return false;
            }
            if(payload && expectedLength)memcpy(payload,parsed.payload,expectedLength);
            return true;
        }
        inTransfer->num_bytes=256; // Bulk IN transfer length must be MPS aligned.
        if(!transfer(inTransfer))return false;
        const int count=inTransfer->actual_num_bytes;
        if(count<=0 || count>256 ||
           !wireReader.feed(inTransfer->data_buffer,(size_t)count)) {
            hostLog("ptp_bad_chunk bytes=%d",count);
            ptpErrors++;
            return false;
        }
    }
    hostLog("ptp_fragment_timeout txn=%lu buffered=%u",
            (unsigned long)txn,(unsigned)wireReader.buffered());
    ptpErrors++;
    return false;
}
static bool execute(uint16_t opcode,bool hasParam=false,uint32_t argument=0,
                    uint8_t *reply=nullptr,size_t len=0) {
    const uint32_t transaction=++tid;
    txRequests++;
    if(!writeCmd(opcode,transaction,hasParam,argument)) {
        hostLog("ptp_command_write_failed op=%04X txn=%lu",opcode,(unsigned long)transaction);
        return false;
    }
    if(len && !readPtp(2,opcode,transaction,reply,len)) {
        hostLog("ptp_data_failed op=%04X txn=%lu",opcode,(unsigned long)transaction);
        return false;
    }
    if(!readPtp(3,RESPONSE_OK,transaction)) {
        hostLog("ptp_response_failed op=%04X txn=%lu",opcode,(unsigned long)transaction);
        return false;
    }
    return true;
}
/* The controller sends a small, versioned status update to Canon. The
 * camera only displays it; this opcode cannot move or fire anything. */
static bool sendProgress(uint16_t opcode,const uint32_t params[5]){
    if(!outTransfer || !params)return false;
    const uint32_t transaction=++tid;
    txRequests++;
    uint8_t *p=outTransfer->data_buffer;
    w32(p,32);w16(p+4,1);w16(p+6,opcode);w32(p+8,transaction);
    for(unsigned i=0;i<5;i++)w32(p+12+4*i,params[i]);
    outTransfer->num_bytes=32;
    if(!transfer(outTransfer))return false;
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
    wireReader.reset();
    if(outTransfer) usb_host_transfer_free(outTransfer);
    if(inTransfer) usb_host_transfer_free(inTransfer);
    outTransfer=inTransfer=nullptr;
    if(claimed && device)usb_host_interface_release(client,device,iface);
    if(device)usb_host_device_close(client,device);
    device=nullptr;claimed=false;sessionOpened=false;gone=false;
    if(activePhotoId.exchange(0) || queuedPhotoId.exchange(0))
        photoStatus=CameraPhotoState::FAILED;
    mailboxValid=false;
    lastProgressSeq=0;
    lastProgressPhase=255;
    lastProgressStage=255;
    navigation.linkFailed();
    gotoLoop.fault(GoToFault::CONNECTION);
    gotoMotors.emergencyStop();
    publishMountSnapshot();
    setState("AWAIT_CAMERA");
}
static void newDevice(uint8_t address) {
    hostLog("usb_attach addr=%u",address);
    if(device) return;
    usb_device_handle_t dev=nullptr;
    if(usb_host_device_open(client,address,&dev)!=ESP_OK)return;
    const usb_config_desc_t *cfg=nullptr;
    if(usb_host_get_active_config_descriptor(dev,&cfg)!=ESP_OK ||
       !findImageInterface(cfg)){
        hostLog("usb_non_ptp addr=%u",address);
        usb_host_device_close(client,dev);
        setState("NOT_A_PTP_CAMERA");return;
    }
    if(usb_host_interface_claim(client,dev,iface,alternate)!=ESP_OK) {
        usb_host_device_close(client,dev);setState("PTP_CLAIM_FAILED");return;
    }
    claimed=true;device=dev;gone=false;
    wireReader.reset();
    const usb_device_desc_t *desc=nullptr;
    if(usb_host_get_device_descriptor(dev,&desc)==ESP_OK && desc)
        hostLog("usb_ptp vid=%04X pid=%04X interface=%u IN=%02X OUT=%02X",
                desc->idVendor,desc->idProduct,iface,bulkIn,bulkOut);
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
    if(msg->event==USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl==device) {
        hostLog("usb_disconnected");
        gone=true;
    }
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
        if(cancelRequested.exchange(false)){
            gotoMotors.emergencyStop();
            gotoLoop.cancel();
            if(queuedPhotoId.exchange(0)){
                photoStatus=CameraPhotoState::FAILED;
                hostLog("goto_queued_photo_cancelled");
            }
            hostLog("goto_cancelled");
        }
        if(linkLostRequested.exchange(false)){
            gotoMotors.emergencyStop();
            gotoLoop.fault(GoToFault::CONNECTION);
            hostLog("goto_tracker_link_lost");
        }
        gotoLoop.tick(millis());
        if(gotoLoop.pulseInProgress()){
            const GoToMotorStatus motor=gotoMotors.poll(millis());
            if(motor==GoToMotorStatus::STOP_CONFIRMED)
                gotoLoop.pulseStopped(true,millis());
            else if(motor==GoToMotorStatus::FAILED ||
                    motor==GoToMotorStatus::UNAVAILABLE){
                gotoLoop.pulseStopped(false,millis());
                gotoMotors.emergencyStop();
            }
        }
        if(gotoLoop.phase()==GoToPhase::FAULT)
            gotoMotors.emergencyStop();
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
        /* Only this task owns the PTP transaction counter and bulk endpoints.
         * HTTP/serial requests enqueue a photo but never touch USB directly. */
        const uint32_t photo=queuedPhotoId.exchange(0);
        if(photo){
            const bool accepted=execute(OP_CAPTURE_STILL,true,photo);
            if(accepted){
                activePhotoId=photo;
                photoStartedAt=millis();
                photoStatus=CameraPhotoState::REQUESTED;
                hostLog("camera_capture_accepted id=%lu",(unsigned long)photo);
            }else{
                photoStatus=CameraPhotoState::FAILED;
                hostLog("camera_capture_rejected id=%lu",(unsigned long)photo);
            }
            navigation.photoSent(photo,accepted);
            if(!accepted && gotoLoop.photoId()==photo &&
               gotoLoop.phase()==GoToPhase::WAIT_PHOTO)
                gotoLoop.fault(GoToFault::BAD_PHOTO);
            publishMountSnapshot();
        }
        if(uint32_t(millis()-lastPoll)<POLL_INTERVAL_MS)continue;
        lastPoll=millis();

        uint8_t bytes[32]={};
        if(execute(OP_MIDNIGHT_POINT,false,0,bytes,sizeof(bytes))) {
            CameraPointing p={};
            if(parseCameraPointing(bytes,sizeof(bytes),p) && p.valid) {
                portENTER_CRITICAL(&mutex);
                const bool newPoint=!recent.valid || recent.sequence!=p.sequence;
                recent=p;recentTime=millis();
                portEXIT_CRITICAL(&mutex);
                if(newPoint)hostLog("point seq=%lu ra=%lu dec=%ld matches=%u src=%u age=%lu",
                    (unsigned long)p.sequence,(unsigned long)p.ra,(long)p.dec,
                    p.matches,p.source,(unsigned long)p.ageAtReadMs);
                navigation.observePoint(p);
                setState("POINT_READY");
            } else {
                if(p.valid)hostLog("point_rejected format_or_bounds");
                setState("NO_VERIFIED_POINT");
            }
        }else setState("PTP_READ_ERROR");

        uint8_t stateBytes[32]={};
        if(execute(OP_MOUNT_MAILBOX,false,0,stateBytes,sizeof(stateBytes))){
            CameraMountMailbox latest={};
            if(parseMountMailbox(stateBytes,sizeof(stateBytes),latest)){
                const uint32_t oldCmd=mailboxValid?lastMailbox.commandSequence:0;
                lastMailbox=latest;
                mailboxValid=true;
                navigation.observeMailbox(latest);
                if(latest.commandSequence!=oldCmd && latest.commandSequence){
                    // Every new target explicitly resets the previous feedback
                    // history. A Stop is always high priority.
                    gotoMotors.emergencyStop();
                    if(latest.command==1 && latest.targetValid){
                        gotoLoop.start(latest.targetRa,latest.targetDec,millis());
                    }else if(latest.command==2){
                        gotoLoop.cancel();
                    }else{
                        gotoLoop.fault(GoToFault::INVALID_TARGET);
                    }
                    hostLog("camera_command seq=%lu cmd=%u ra=%lu dec=%ld",
                        (unsigned long)latest.commandSequence,latest.command,
                        (unsigned long)latest.targetRa,(long)latest.targetDec);
                    if(latest.command==2){
                        hostLog("camera_stop_navigation");
                        if(queuedPhotoId.exchange(0)){
                            photoStatus=CameraPhotoState::FAILED;
                            hostLog("camera_cancelled_unstarted_capture");
                        }
                    }
                }
                const uint32_t active=activePhotoId.load();
                if(active && latest.completedId==active &&
                   latest.captureState==3){
                    hostLog("camera_capture_failed id=%lu",(unsigned long)active);
                    activePhotoId=0;
                    photoStatus=CameraPhotoState::FAILED;
                }else if(active && latest.completedId==active &&
                         latest.captureState==2){
                    CameraPointing point={};
                    uint32_t at=0;
                    if(cameraHostLatest(point,at) && point.source==5 &&
                       point.sequence==latest.pointSequence &&
                       usableLivePointing(point) &&
                       uint32_t(millis()-at)+point.ageAtReadMs<65000){
                        if(gotoLoop.phase()==GoToPhase::WAIT_PHOTO &&
                           gotoLoop.photoId()==active){
                            if(!gotoLoop.finishPhoto(active,point,millis()))
                                hostLog("goto_photo_rejected id=%lu fault=%u",
                                        (unsigned long)active,(unsigned)gotoLoop.failure());
                        }
                        activePhotoId=0;
                        photoStatus=CameraPhotoState::COMPLETE;
                        hostLog("camera_capture_complete id=%lu seq=%lu",
                            (unsigned long)active,(unsigned long)point.sequence);
                    }
                }
            }else{
                hostLog("camera_mailbox_invalid");
                ptpErrors++;
            }
        }else{
            hostLog("camera_mailbox_ptp_failed");
        }
        if(activePhotoId && uint32_t(millis()-photoStartedAt)>CAPTURE_DEADLINE_MS){
            hostLog("camera_capture_timeout id=%lu",
                    (unsigned long)activePhotoId.load());
            activePhotoId=0;
            photoStatus=CameraPhotoState::TIMED_OUT;
            navigation.linkFailed();
            gotoLoop.fault(GoToFault::PHOTO_TIMEOUT);
        }
        // An armed motor adapter must provide stop acknowledgement before
        // feedback is permitted. This firmware has no axis-2 adapter, so
        // ready() is always false, even if the camera sends GoTo.
        if(gotoLoop.needsPulse() && gotoMotors.ready()){
            mount_pulse pulse={};
            if(gotoLoop.nextPulse(pulse) && gotoMotors.pulse(pulse,millis())){
                gotoLoop.pulseStarted(millis());
                hostLog("goto_pulse axis=%d dir=%d ms=%lu",
                        pulse.axis,pulse.direction,(unsigned long)pulse.duration_ms);
            }else gotoLoop.fault(GoToFault::MOTOR_START);
        }
        if(gotoLoop.needsPhoto() && !activePhotoId && !queuedPhotoId){
            uint32_t id=0;
            if(cameraHostRequestMeasure(id)){
                gotoLoop.beginPhoto(id,millis());
                navigation.photoQueued(id);
                hostLog("goto_requested_still id=%lu stage=%u",
                        (unsigned long)id,(unsigned)gotoLoop.stage());
            }
        }
        /* Report progress back to the camera. The operation is idempotent
         * for each camera command sequence, and gets rate-limited if its
         * firmware does not implement status reception. */
        const uint32_t now=millis();
        if(mailboxValid && lastMailbox.commandSequence){
            const uint8_t phase=(uint8_t)gotoLoop.phase();
            const uint8_t stage=(uint8_t)gotoLoop.stage();
            const uint32_t error=gotoLoop.errorE4();
            const uint32_t steps=gotoLoop.steps();
            const bool changed=lastProgressSeq!=lastMailbox.commandSequence ||
                               lastProgressPhase!=phase ||
                               lastProgressStage!=stage ||
                               lastProgressStep!=steps ||
                               (error>lastProgressError?error-lastProgressError:
                                                      lastProgressError-error)>150;
            if(changed && (lastProgressAttempt==0 ||
                           uint32_t(now-lastProgressAttempt)>=3000)){
                lastProgressAttempt=now;
                const uint32_t params[5]={
                    uint32_t(phase)|(gotoMotors.ready()?0x100u:0u),
                    error,steps,stage,lastMailbox.commandSequence
                };
                if(sendProgress(OP_MOUNT_PROGRESS,params)){
                    lastProgressSeq=lastMailbox.commandSequence;
                    lastProgressPhase=phase;lastProgressStage=stage;
                    lastProgressStep=steps;lastProgressError=error;
                    hostLog("camera_progress phase=%u stage=%u err=%lu steps=%lu",
                            phase,stage,(unsigned long)error,(unsigned long)steps);
                }else{
                    hostLog("camera_progress_ptp_failed");
                }
            }
        }
        publishMountSnapshot();
    }
}
}
void cameraHostStart(){
    if(running)return;
    hostLog("usb_host_requested");
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
const char *cameraHostState(){return state.load(std::memory_order_acquire);}
bool cameraHostLatest(CameraPointing &out,uint32_t &at){
    portENTER_CRITICAL(&mutex);
    out=recent;at=recentTime;
    portEXIT_CRITICAL(&mutex);
    return out.valid;
}

String cameraHostDiagnostics(){
    LogRecord copy[LOG_COUNT]={};
    size_t head=0,used=0;
    portENTER_CRITICAL(&logMutex);
    memcpy(copy,logRecords,sizeof(copy));
    head=logHead; used=logUsed;
    portEXIT_CRITICAL(&logMutex);
    String result;
    result.reserve(4600);
    result+=String("Midnight PTP host=")+cameraHostState()+
            " uptime_ms="+String(millis())+
            " requests="+String((unsigned long)txRequests.load())+
            " usb_transfers="+String((unsigned long)rxPackets.load())+
            " errors="+String((unsigned long)ptpErrors.load())+"\n";
    for(size_t i=0;i<used;i++){
        const auto &item=copy[(head+LOG_COUNT-used+i)%LOG_COUNT];
        result+=String(item.ms)+" "+item.message+"\n";
    }
    return result;
}

bool cameraHostRequestMeasure(uint32_t &id){
    id=0;
    if(!running || !sessionOpened.load() || activePhotoId.load() ||
       queuedPhotoId.load())return false;
    const CameraPhotoState status=photoStatus.load();
    if(status==CameraPhotoState::QUEUED ||
       status==CameraPhotoState::REQUESTED)return false;
    uint32_t candidate=esp_random();
    if(!candidate)candidate=1;
    if(candidate==lastIssuedPhotoId.load())candidate++;
    uint32_t expected=0;
    if(!queuedPhotoId.compare_exchange_strong(expected,candidate))return false;
    lastIssuedPhotoId=candidate;
    photoStatus=CameraPhotoState::QUEUED;
    id=candidate;
    return true;
}
bool cameraHostMountView(CameraMountView &view){
    portENTER_CRITICAL(&mutex);
    view=mountPublished;
    portEXIT_CRITICAL(&mutex);
    return view.mailboxValid;
}
const char *cameraHostPhotoStatus(CameraPhotoState p){
    switch(p){
        case CameraPhotoState::IDLE:return "IDLE";
        case CameraPhotoState::QUEUED:return "QUEUED";
        case CameraPhotoState::REQUESTED:return "REQUESTED";
        case CameraPhotoState::COMPLETE:return "COMPLETE";
        case CameraPhotoState::FAILED:return "FAILED";
        case CameraPhotoState::TIMED_OUT:return "TIMED_OUT";
    }
    return "INVALID";
}
const char *cameraHostNavigationStatus(MountNavState p){
    switch(p){
        case MountNavState::IDLE:return "IDLE";
        case MountNavState::REQUEST_PHOTO:return "REQUEST_PHOTO";
        case MountNavState::QUEUED:return "PHOTO_QUEUED";
        case MountNavState::WAIT_PHOTO:return "WAIT_PHOTO";
        case MountNavState::WAIT_POINT:return "WAIT_POINT";
        case MountNavState::MOTORS_UNAVAILABLE:return "MOTORS_UNAVAILABLE";
        case MountNavState::FAILED:return "FAILED";
        case MountNavState::STOPPED:return "STOPPED";
    }
    return "INVALID";
}

const char *cameraHostLoopStatus(GoToPhase phase,bool motors){
    switch(phase){
        case GoToPhase::IDLE:return "IDLE";
        case GoToPhase::NEED_PHOTO:return "NEED_PHOTO";
        case GoToPhase::WAIT_PHOTO:return "WAIT_PHOTO";
        case GoToPhase::NEED_MOTOR:return motors?"NEED_PULSE":"WAIT_MOTORS";
        case GoToPhase::MOVING:return "MOVING";
        case GoToPhase::SETTLING:return "SETTLING";
        case GoToPhase::ARRIVED:return "ARRIVED";
        case GoToPhase::STOPPED:return "STOPPED";
        case GoToPhase::FAULT:return "FAULT";
    }
    return "INVALID";
}
const char *cameraHostLoopFault(GoToFault code){
    switch(code){
        case GoToFault::NONE:return "NONE";
        case GoToFault::INVALID_TARGET:return "INVALID_TARGET";
        case GoToFault::INVALID_POSITION:return "INVALID_POSITION";
        case GoToFault::BAD_PHOTO:return "BAD_PHOTO";
        case GoToFault::DRIFT_SAMPLE:return "DRIFT_SAMPLE";
        case GoToFault::PHOTO_TIMEOUT:return "PHOTO_TIMEOUT";
        case GoToFault::MOTOR_START:return "MOTOR_START";
        case GoToFault::MOTOR_STOP:return "MOTOR_STOP";
        case GoToFault::MOTOR_TIMEOUT:return "MOTOR_TIMEOUT";
        case GoToFault::CALIBRATION:return "CALIBRATION";
        case GoToFault::BAD_AXES:return "BAD_AXES";
        case GoToFault::TOO_FAR:return "TOO_FAR";
        case GoToFault::NO_PROGRESS:return "NO_PROGRESS";
        case GoToFault::MAX_STEPS:return "MAX_STEPS";
        case GoToFault::TOTAL_TIMEOUT:return "TOTAL_TIMEOUT";
        case GoToFault::CONNECTION:return "CONNECTION";
    }
    return "UNKNOWN";
}

void cameraHostCancelGoTo(){cancelRequested=true;}
void cameraHostGoToLinkLost(){linkLostRequested=true;}
