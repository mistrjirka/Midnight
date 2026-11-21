/* Midnight OG Star Tracker RA-only bridge.
 * ESP32-S3 USB serial <-> tracker Wi-Fi. No motor starts on boot.
 * GPL-2.0-or-later.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include "camera_ptp_host.h"
#include "axis2_hbridge.h"
#if defined(MIDNIGHT_HIL_SIMULATION)
#include "goto_hil.h"
#endif
#include <cstring>
#include <cstdlib>

namespace {
constexpr char kDefaultSsidPrefix[] = "OG Star Tracker";
constexpr char kDefaultPassword[] = "password123"; // Documented OG factory default.
constexpr uint32_t kMaxSlewMs = 1000;
constexpr uint32_t kLinkRetryMs = 6000;
constexpr uint32_t kWifiTimeoutMs = 9000;
constexpr uint32_t kHttpTimeoutMs = 1400;
constexpr char kTrackerUrl[] = "http://192.168.4.1";
Preferences prefs;
WebServer telemetryServer(8081);
String ssidPrefix = kDefaultSsidPrefix;
String password = kDefaultPassword;
String joinedSsid;
uint32_t reconnectAt = 0, stopAt = 0;
bool needStop = false;
uint32_t lastCameraStopCommand = 0;
bool lastWifiConnected = false;
char line[192];
size_t lineSize = 0;

struct Pointing {
  uint32_t seq = 0, ra = 0, at = 0;
  int32_t dec = 0;
  uint16_t matches = 0, fov = 0;
  uint8_t source = 0;
  bool valid = false;
} pointing;

bool wifiReady() { return WiFi.status() == WL_CONNECTED; }

void reportPoint() {
  CameraPointing camera={};
  uint32_t capturedAt=0;
  if(cameraHostLatest(camera,capturedAt) && uint32_t(millis()-capturedAt)<30000) {
    Serial.printf("CAMERA_POINT %lu %ld %u %u %lu source=%u age_ms=%lu\n",
                  (unsigned long)camera.ra,(long)camera.dec,
                  camera.matches,camera.fovCdeg,(unsigned long)camera.sequence,
                  camera.source,(unsigned long)(millis()-capturedAt+camera.ageAtReadMs));
    return;
  }
  if (!pointing.valid) {
    Serial.println("POINT NONE");
    return;
  }
  Serial.printf("POINT %lu %ld %u %u %lu %lu\n",
                (unsigned long)pointing.ra, (long)pointing.dec,
                (unsigned)pointing.matches, (unsigned)pointing.fov,
                (unsigned long)pointing.seq, (unsigned long)pointing.at);
}
void reconnect() {
  if (wifiReady()) return;
  WiFi.disconnect(false, false);
  delay(350); // Wait for the disconnect event before the active scan.
  const int count = WiFi.scanNetworks(false, true, false, 180);
  int best = -1, bestRssi = -200;
  for (int i = 0; i < count; ++i) {
    const String found = WiFi.SSID(i);
    if (found.startsWith(ssidPrefix) && WiFi.RSSI(i) > bestRssi) {
      best = i;
      bestRssi = WiFi.RSSI(i);
    }
  }
  if (best < 0) {
    joinedSsid = "";
    WiFi.scanDelete();
    Serial.printf("ERR TRACKER_SSID_NOT_FOUND (%d networks scanned)\n",count);
    return;
  }
  joinedSsid = WiFi.SSID(best);
  WiFi.scanDelete();
  Serial.printf("WIFI CONNECTING %s\n", joinedSsid.c_str());
  WiFi.begin(joinedSsid.c_str(), password.c_str());
  const uint32_t deadline = millis() + kWifiTimeoutMs;
  while (!wifiReady() && (int32_t)(deadline - millis()) > 0) {
    delay(50);
  }
  if (wifiReady()) {
    Serial.printf("WIFI READY %s IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
  } else Serial.println("ERR WIFI_UNAVAILABLE");
}

bool get(const String &path, String &out) {
  if (!wifiReady()) {
    out = "WIFI_DISCONNECTED";
    return false;
  }
  HTTPClient http;
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(String(kTrackerUrl) + path)) {
    out = "HTTP_INIT_FAILED";
    return false;
  }
  int status = http.GET();
  if (status == 200) out = http.getString();
  else out = String("HTTP_") + status;
  http.end();
  return status == 200;
}
void request(const String &path) {
  String body;
  const bool ok = get(path, body);
  Serial.printf("%s %s\n", ok ? "OK" : "ERR", body.c_str());
}

// Some tracker firmware has no automatic stop on Wi-Fi loss.
// Always attempt /stopslew until acknowledged. No autonomous movements.
void stopSlew() {
  String result;
  if (get("/stopslew", result)) {
    needStop = false;
    Serial.println("OK SLEW_STOPPED");
  } else {
    Serial.printf("ERR STOP_RETRY %s\n", result.c_str());
    stopAt = millis() + 250;
  }
}
bool integer(const char *s, long minimum, long maximum, long *out) {
  if (!s || !*s) return false;
  char *end = nullptr;
  const long value = strtol(s, &end, 10);
  if (!end || *end || value < minimum || value > maximum) return false;
  *out = value;
  return true;
}
#if defined(MIDNIGHT_HIL_SIMULATION)
static GoToHil simulation;
static int hexDigit(char c){
    if(c>='0' && c<='9')return c-'0';
    if(c>='a' && c<='f')return c-'a'+10;
    if(c>='A' && c<='F')return c-'A'+10;
    return -1;
}
static bool parseHex32(const char *hex,uint8_t packet[32]){
    if(!hex || strlen(hex)!=64)return false;
    for(int i=0;i<32;i++){
        const int hi=hexDigit(hex[2*i]),lo=hexDigit(hex[2*i+1]);
        if(hi<0 || lo<0)return false;
        packet[i]=(uint8_t)(hi*16+lo);
    }
    return true;
}
static void printSimStatus(){
    const GoToLoop &loop=simulation.loop();
    Serial.printf("SIM STATUS phase=%s stage=%u error_e4=%lu corrections=%lu "
                  "ra=%lu dec=%ld drift_e_q16=%ld drift_n_q16=%ld "
                  "drift_valid=%u calibrated=%u fault=%u now=%lu\n",
         loop.status(true),(unsigned)loop.stage(),
         (unsigned long)loop.errorE4(),(unsigned long)loop.steps(),
         (unsigned long)loop.lastRa(),(long)loop.lastDec(),
         (long)loop.driftEastQ16(),(long)loop.driftNorthQ16(),
         loop.driftMeasured()?1u:0u,loop.calibrated()?1u:0u,
         (unsigned)loop.failure(),(unsigned long)simulation.clock());
}
static void handleSim(char *save){
    const char *sub=strtok_r(nullptr," \t",&save);
    if(!sub){Serial.println("SIM ERR COMMAND_REQUIRED");return;}
    if(!strcmp(sub,"RESET")){
        simulation.reset();
        Serial.println("SIM OK RESET");
    }else if(!strcmp(sub,"STATUS")){
        printSimStatus();
    }else if(!strcmp(sub,"MAILBOX")){
        uint8_t data[32]={};
        const char *hex=strtok_r(nullptr," \t",&save);
        if(!parseHex32(hex,data)){Serial.println("SIM ERR INVALID_MNC1_HEX");return;}
        if(simulation.mailbox(data)){
            Serial.println("SIM OK MAILBOX");
            printSimStatus();
        }else{
            Serial.printf("SIM ERR %s\n",simulation.lastError());
        }
    }else if(!strcmp(sub,"POINT")){
        const char *idStr=strtok_r(nullptr," \t",&save);
        const char *hex=strtok_r(nullptr," \t",&save);
        uint8_t data[32]={};
        long id=0;
        if(!integer(idStr,1,2147483647L,&id) || !parseHex32(hex,data)){
            Serial.println("SIM ERR INVALID_MNT1");
            return;
        }
        if(simulation.pointing((uint32_t)id,data)){
            Serial.println("SIM OK POINT");
            printSimStatus();
        }else{
            Serial.printf("SIM ERR %s\n",simulation.lastError());
        }
    }else if(!strcmp(sub,"NEXT")){
        const HilAction action=simulation.next();
        if(action.type==HilActionType::CAMERA_SHOT){
            Serial.printf("SIM PHOTO id=%lu\n",(unsigned long)action.requestId);
        }else if(action.type==HilActionType::RA_SLEW ||
                 action.type==HilActionType::AXIS2_SLEW){
            if(action.motor.axis==0){
                const int ogDirection=action.motor.direction>0?0:1;
                Serial.printf("SIM MOTOR axis=0 direction=%d duration_ms=%lu "
                              "dryrun_http=/startslew?speed=400&direction=%d "
                              "dryrun_stop=/stopslew\n",
                              action.motor.direction,
                              (unsigned long)action.motor.duration_ms,ogDirection);
            }else{
                Serial.printf("SIM MOTOR axis=1 direction=%d duration_ms=%lu "
                              "in1=%d in2=%d stop_in1=0 stop_in2=0\n",
                              action.motor.direction,
                              (unsigned long)action.motor.duration_ms,
                              action.motor.direction>0?1:0,
                              action.motor.direction<0?1:0);
            }
        }else{
            Serial.printf("SIM IDLE phase=%s\n",
                          simulation.loop().status(true));
        }
    }else if(!strcmp(sub,"TIME")){
        const char *arg=strtok_r(nullptr," \t",&save);
        long ms=0;
        if(!integer(arg,0,1200000,&ms)){
            Serial.println("SIM ERR INVALID_TIME");
            return;
        }
        simulation.advance((uint32_t)ms);
        Serial.printf("SIM TIME now=%lu\n",(unsigned long)simulation.clock());
    }else if(!strcmp(sub,"ACK")){
        const bool ok=simulation.stopAck(true);
        Serial.println(ok?"SIM OK STOP_CONFIRMED":"SIM ERR NO_MOTOR_ACTIVE");
    }else if(!strcmp(sub,"ACKFAIL")){
        simulation.stopAck(false);
        Serial.println("SIM ERR MOTOR_STOP_FAILED");
    }else if(!strcmp(sub,"CANCEL")){
        simulation.cancel();
        Serial.println("SIM OK CANCEL");
    }else{
        Serial.println("SIM ERR UNKNOWN");
    }
}
#endif

void handleLine(char *command) {
  char *save = nullptr;
  char *cmd = strtok_r(command, " \t", &save);
  if (!cmd) return;
#if defined(MIDNIGHT_HIL_SIMULATION)
  // This firmware intentionally cannot issue any real motor commands.
  if(!strcmp(cmd,"TRACK") || !strcmp(cmd,"SLEW") ||
     !strcmp(cmd,"STOP") || !strcmp(cmd,"AXIS2")){
      Serial.println("ERR HIL_MOTORS_HARD_LOCKED");
      return;
  }
  if(!strcmp(cmd,"SIM")){
      handleSim(save);
      return;
  }
#endif
  if (!strcmp(cmd, "PING")) Serial.println("OK MIDNIGHT_BRIDGE_V1");
  else if (!strcmp(cmd, "HELP")) Serial.println(
    "COMMANDS: PING WIFI SCAN HOST STATUS|LOG CAMERA STATUS|MEASURE|TARGET "
    "VERSION POSITION POINT GETPOINT TRACK 0|1 [0|1] [rate] "
    "SLEW 0|1 duration_ms [speed_2_to_400] STOP");
  else if (!strcmp(cmd, "SCAN")) {
    WiFi.disconnect(false, false);
    delay(350);
    const int n=WiFi.scanNetworks(false,true,false,180);
    Serial.printf("SCAN COUNT %d\n",n);
    for(int i=0;i<n && i<30;++i)
        Serial.printf("SCAN %s RSSI %ld\n",WiFi.SSID(i).c_str(),(long)WiFi.RSSI(i));
    WiFi.scanDelete();
    reconnectAt=millis()+2000;
  }
  else if (!strcmp(cmd, "HOST")) {
    const char *arg=strtok_r(nullptr," \t",&save);
    if(arg && !strcmp(arg,"START") && !cameraHostRunning()) {
      Serial.println("HOST STARTING - native USB JTAG may disconnect");
      Serial.flush();
      cameraHostStart();
      Serial.printf("HOST %s\n",cameraHostState());
    } else if(arg && !strcmp(arg,"STATUS")) {
      Serial.printf("HOST %s\n",cameraHostState());
    } else if(arg && !strcmp(arg,"LOG")) {
      Serial.print(cameraHostDiagnostics());
    } else Serial.println("ERR HOST_USE_START_STATUS_OR_LOG");
  }
  else if (!strcmp(cmd, "CAMERA")) {
    const char *arg=strtok_r(nullptr," \t",&save);
    if(arg && !strcmp(arg,"MEASURE")){
      uint32_t requestId=0;
      if(cameraHostRequestMeasure(requestId))
        Serial.printf("OK CAMERA_PHOTO_QUEUED id=%lu\n",(unsigned long)requestId);
      else Serial.println("ERR CAMERA_NOT_READY_OR_BUSY");
    }else if(arg && (!strcmp(arg,"STATUS") || !strcmp(arg,"TARGET"))){
      CameraMountView view={};
      cameraHostMountView(view);
      Serial.printf("CAMERA host=%s mailbox=%s state=%s photo=%s id=%lu\n",
                    cameraHostState(),view.mailboxValid?"OK":"NO",
                    cameraHostNavigationStatus(view.navigation),
                    cameraHostPhotoStatus(view.photoState),
                    (unsigned long)view.lastRequestId);
      Serial.printf("GOTO phase=%s stage=%u error_e4=%lu corrections=%lu "
                    "fault=%s motor_ready=%u pulse_axis=%d pulse_dir=%d pulse_ms=%lu\n",
                    cameraHostLoopStatus(view.loopPhase,view.autoMotorReady),
                    (unsigned)view.loopStage,(unsigned long)view.loopErrorE4,
                    (unsigned long)view.loopCorrections,
                    cameraHostLoopFault(view.loopFault),
                    view.autoMotorReady?1u:0u,
                    view.nextPulseValid?view.nextPulse.axis:-1,
                    view.nextPulseValid?view.nextPulse.direction:0,
                    (unsigned long)(view.nextPulseValid?view.nextPulse.duration_ms:0));
      if(view.mailboxValid){
        Serial.printf("CAMERA target_seq=%lu command=%u target=%lu,%ld "
                      "requested=%lu completed=%lu capture_state=%u point_seq=%lu\n",
                      (unsigned long)view.mailbox.commandSequence,
                      view.mailbox.command,(unsigned long)view.mailbox.targetRa,
                      (long)view.mailbox.targetDec,
                      (unsigned long)view.mailbox.requestedId,
                      (unsigned long)view.mailbox.completedId,
                      view.mailbox.captureState,
                      (unsigned long)view.mailbox.pointSequence);
      }
    }else Serial.println("ERR CAMERA_USE_STATUS_TARGET_OR_MEASURE");
  }
  else if (!strcmp(cmd,"AXIS2")){
    const char *action=strtok_r(nullptr," \t",&save);
    if(action && !strcmp(action,"STATUS")){
        Serial.printf("AXIS2 configured=%u moving=%u\n",
                      axis2HBridgeConfigured()?1u:0u,
                      axis2HBridgeMoving()?1u:0u);
    }else if(action && !strcmp(action,"STOP")){
        axis2HBridgeStop();
        Serial.println("OK AXIS2_OUTPUTS_LOW");
    }else if(action && !strcmp(action,"PULSE")){
        long direction,ms;
        if(!integer(strtok_r(nullptr," \t",&save),-1,1,&direction) ||
           direction==0 ||
           !integer(strtok_r(nullptr," \t",&save),50,1000,&ms)){
            Serial.println("ERR AXIS2_ARGUMENTS");
        }else if(!axis2HBridgeConfigured()){
            Serial.println("ERR AXIS2_UNCONFIGURED");
        }else if(axis2HBridgePulse((int)direction,(uint32_t)ms)){
            Serial.printf("OK AXIS2_PULSE dir=%ld ms=%ld local_watchdog=on\n",
                          direction,ms);
        }else Serial.println("ERR AXIS2_BUSY_OR_ESTOP");
    }else Serial.println("ERR AXIS2_USE_STATUS_STOP_OR_PULSE");
  }
  else if (!strcmp(cmd, "WIFI")) {
    Serial.printf("WIFI %s SSID %s\n", wifiReady() ? "READY" : "DISCONNECTED",
                  (joinedSsid.isEmpty()?"(none)":joinedSsid.c_str()));
  }
  else if (!strcmp(cmd, "STATUS")) request("/status");
  else if (!strcmp(cmd, "VERSION")) request("/version");
  else if (!strcmp(cmd, "POSITION")) request("/getCurrentPosition");
  else if (!strcmp(cmd, "GETPOINT")) reportPoint();
  else if (!strcmp(cmd, "POINT")) {
    long ra, dec, matched, fov;
    if (!integer(strtok_r(nullptr," \t",&save),0,3599999,&ra) ||
        !integer(strtok_r(nullptr," \t",&save),-900000,900000,&dec) ||
        !integer(strtok_r(nullptr," \t",&save),8,255,&matched) ||
        !integer(strtok_r(nullptr," \t",&save),1,18000,&fov) ||
        strtok_r(nullptr," \t",&save)) {
      Serial.println("ERR POINT_ARGUMENTS"); return;
    }
    pointing = {pointing.seq+1, (uint32_t)ra, millis(), (int32_t)dec,
                (uint16_t)matched, (uint16_t)fov, 4, true};
    Serial.println("OK POINT_RECEIVED_SERIAL_TEST");
  }
  else if (!strcmp(cmd, "TRACK")) {
    long on, dir = 0, rate = 15956;
    if (!integer(strtok_r(nullptr," \t",&save),0,1,&on)) {
      Serial.println("ERR TRACK_ARGUMENTS"); return;
    }
    if (on) {
      const char *d = strtok_r(nullptr," \t",&save);
      const char *r = strtok_r(nullptr," \t",&save);
      if ((d && !integer(d,0,1,&dir)) ||
          (r && !integer(r,1,1000000,&rate)) ||
          strtok_r(nullptr," \t",&save)) {
        Serial.println("ERR TRACK_ARGUMENTS"); return;
      }
    } else if (strtok_r(nullptr," \t",&save)) {
      Serial.println("ERR TRACK_ARGUMENTS"); return;
    }
    if (needStop) { Serial.println("ERR SLEW_STOP_PENDING"); return; }
    if (!on) request("/off");
    else request(String("/on?direction=")+dir+"&trackingSpeed="+rate);
  }
  else if (!strcmp(cmd, "SLEW")) {
    long dir, ms, speed = 400;
    if (!integer(strtok_r(nullptr," \t",&save),0,1,&dir) ||
        !integer(strtok_r(nullptr," \t",&save),1,kMaxSlewMs,&ms)) {
      Serial.println("ERR SLEW_ARGUMENTS"); return;
    }
    const char *s = strtok_r(nullptr," \t",&save);
    if ((s && !integer(s,2,400,&speed)) ||
        strtok_r(nullptr," \t",&save)) {
      Serial.println("ERR SLEW_ARGUMENTS"); return;
    }
    if (needStop) { Serial.println("ERR SLEW_STOP_PENDING"); return; }
    // A lost HTTP reply does not mean the tracker ignored the start command.
    // Arm the STOP before the first request, including failure/timeout paths.
    needStop = true;
    stopAt = millis() + ms;
    String output;
    if (get(String("/startslew?speed=")+speed+"&direction="+dir,output)) {
      Serial.printf("OK SLEW_REQUESTED %ld ms (best-effort stop)\n",ms);
    } else {
      Serial.printf("ERR SLEW_START %s (sending STOP anyway)\n",output.c_str());
      stopAt = 0;
      stopSlew();
    }
  }
  else if (!strcmp(cmd, "STOP")) {
    cameraHostCancelGoTo();
    needStop = true;
    stopAt = 0;
    stopSlew();
  } else Serial.println("ERR UNKNOWN_COMMAND");
}
static void startTelemetryServer() {
  telemetryServer.on("/diagnostics",HTTP_GET,[](){
    telemetryServer.send(200,"text/plain",cameraHostDiagnostics());
  });
  telemetryServer.on("/goto/stop",HTTP_POST,[](){
    cameraHostCancelGoTo();
    needStop=true;stopAt=0;
    telemetryServer.send(202,"application/json",
      "{\"accepted\":true,\"navigation\":\"STOP_REQUESTED\"}");
  });
  telemetryServer.on("/mount",HTTP_GET,[](){
    CameraMountView view={};
    cameraHostMountView(view);
    String response=String("{\"camera_host\":\"")+cameraHostState()
      +"\",\"mailbox_valid\":"+(view.mailboxValid?"true":"false")
      +",\"navigation\":\""+cameraHostNavigationStatus(view.navigation)
      +"\",\"photo_status\":\""+cameraHostPhotoStatus(view.photoState)
      +"\",\"photo_id\":"+String(view.lastRequestId)
      +",\"target_valid\":"+(view.targetActive?"true":"false")
      +",\"target_ra_e4\":"+String(view.targetRa)
      +",\"target_dec_e4\":"+String(view.targetDec)
      +",\"last_photo_ra_e4\":"+String(view.lastPhotoRa)
      +",\"last_photo_dec_e4\":"+String(view.lastPhotoDec)
      +",\"motor_control_enabled\":false}";
    response.remove(response.length()-1); // replace closing brace with details
    response+=",\"goto_phase\":\"";
    response+=cameraHostLoopStatus(view.loopPhase,view.autoMotorReady);
    response+="\",\"goto_stage\":"+String((unsigned)view.loopStage);
    response+=",\"goto_fault\":\"";
    response+=cameraHostLoopFault(view.loopFault);
    response+="\",\"error_e4\":"+String(view.loopErrorE4);
    response+=",\"corrections\":"+String(view.loopCorrections);
    response+=",\"next_pulse_axis\":"+String(view.nextPulseValid?view.nextPulse.axis:-1);
    response+=",\"next_pulse_direction\":"+String(view.nextPulseValid?view.nextPulse.direction:0);
    response+=",\"next_pulse_ms\":"+String(view.nextPulseValid?view.nextPulse.duration_ms:0);
    response+=",\"motor_ready\":"+
      String(view.autoMotorReady?"true":"false")+"}";
    telemetryServer.send(200,"application/json",response);
  });
  telemetryServer.on("/measure",HTTP_POST,[](){
    uint32_t requestId=0;
    if(!cameraHostRequestMeasure(requestId)){
      telemetryServer.send(409,"application/json",
        "{\"accepted\":false,\"reason\":\"CAMERA_NOT_READY_OR_BUSY\"}");
      return;
    }
    telemetryServer.send(202,"application/json",
      String("{\"accepted\":true,\"request_id\":")+requestId+"}");
  });
  telemetryServer.on("/health",HTTP_GET,[](){
    String content=String("{\"device\":\"midnight-esp32s3\",\"host\":\"")
        +cameraHostState()+"\",\"tracker_wifi\":"
        +(wifiReady()?"true":"false")+"}";
    telemetryServer.send(200,"application/json",content);
  });
  telemetryServer.on("/pointing",HTTP_GET,[](){
    CameraPointing cp={};
    uint32_t receivedAt=0;
    bool present=cameraHostLatest(cp,receivedAt);
    const uint32_t age=millis()-receivedAt;
    String response=String("{\"available\":")+(present?"true":"false");
    if(present) {
      response+=",\"ra_e4\":"+String(cp.ra);
      response+=",\"dec_e4\":"+String(cp.dec);
      response+=",\"matched\":"+String(cp.matches);
      response+=",\"fov_cdeg\":"+String(cp.fovCdeg);
      response+=",\"source\":"+String(cp.source);
      response+=",\"sequence\":"+String(cp.sequence);
      response+=",\"age_ms\":"+String(age+cp.ageAtReadMs);
      response+=",\"live_and_fresh\":"+String(usableLivePointing(cp) && age+cp.ageAtReadMs<(cp.source==5?65000u:10000u)?"true":"false");
    }
    telemetryServer.send(200,"application/json",response+"}");
  });
  telemetryServer.begin();
}
}
void setup() {
  // Outputs default LOW. If pin macros are unset this configures nothing.
  axis2HBridgeBegin();
  Serial.begin(115200);
  delay(700);
  Serial.println("MIDNIGHT_BRIDGE_V1 BOOT");
  prefs.begin("midnight", true);
  ssidPrefix = prefs.getString("ssid", kDefaultSsidPrefix);
  password = prefs.getString("password", kDefaultPassword);
  prefs.end();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  startTelemetryServer();
  reconnectAt = 0;
}
void loop() {
  telemetryServer.handleClient();
  const bool wifiUp=wifiReady();
  if(lastWifiConnected && !wifiUp)cameraHostGoToLinkLost();
  lastWifiConnected=wifiUp;
  /* A STOP button on the camera cancels navigation and forces any pending
   * manual RA slew to stop. No new mount motion is ever triggered by USB. */
  CameraMountView camera={};
  if(cameraHostMountView(camera) &&
     camera.mailbox.command==2 &&
     camera.mailbox.commandSequence!=lastCameraStopCommand){
    lastCameraStopCommand=camera.mailbox.commandSequence;
    cameraHostCancelGoTo();
    needStop=true;stopAt=0;
    Serial.println("CAMERA STOP_REQUESTED");
  }
  if (needStop && (int32_t)(millis() - stopAt) >= 0) {
    stopSlew();
  }
  if (!wifiReady() && (int32_t)(millis()-reconnectAt)>=0) {
    reconnectAt = millis() + kLinkRetryMs + kWifiTimeoutMs;
    reconnect();
  }
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[lineSize] = 0;
      handleLine(line);
      lineSize = 0;
    } else if (ch >= 32 && ch <= 126) {
      if (lineSize+1 < sizeof(line)) line[lineSize++] = (char)ch;
      else { lineSize = 0; Serial.println("ERR LINE_TOO_LONG"); }
    }
  }
  delay(10);
}
