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
void handleLine(char *command) {
  char *save = nullptr;
  char *cmd = strtok_r(command, " \t", &save);
  if (!cmd) return;
  if (!strcmp(cmd, "PING")) Serial.println("OK MIDNIGHT_BRIDGE_V1");
  else if (!strcmp(cmd, "HELP")) Serial.println(
    "COMMANDS: PING WIFI SCAN HOST STATUS VERSION POSITION POINT GETPOINT "
    "TRACK 0|1 [0|1] [rate] SLEW 0|1 duration_ms [speed_2_to_400] STOP");
  else if (!strcmp(cmd, "SCAN")) {
    WiFi.disconnect(false, false);
    delay(350);
    const int n=WiFi.scanNetworks(false,true,false,180);
    Serial.printf("SCAN COUNT %d\n",n);
    for(int i=0;i<n && i<30;++i)
        Serial.printf("SCAN %s RSSI %d\n",WiFi.SSID(i).c_str(),WiFi.RSSI(i));
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
    } else Serial.println("ERR HOST_USE_START_OR_STATUS");
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
    String output;
    if (get(String("/startslew?speed=")+speed+"&direction="+dir,output)) {
      needStop = true;
      stopAt = millis() + ms;
      Serial.printf("OK SLEW_STARTED %ld ms (will stop automatically)\n",ms);
    } else Serial.printf("ERR SLEW_START %s\n", output.c_str());
  }
  else if (!strcmp(cmd, "STOP")) {
    needStop = true;
    stopAt = 0;
    stopSlew();
  } else Serial.println("ERR UNKNOWN_COMMAND");
}
static void startTelemetryServer() {
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
      response+=",\"live_and_fresh\":"+String(usableLivePointing(cp) && age+cp.ageAtReadMs<10000?"true":"false");
    }
    telemetryServer.send(200,"application/json",response+"}");
  });
  telemetryServer.begin();
}
}
void setup() {
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
