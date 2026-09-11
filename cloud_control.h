#pragma once
// Remote panel control via HiveMQ MQTT (works from phone on cellular / other Wi‑Fi).
// Topics: ft/{MQTT_DEVICE_ID}/cmd|status|online
// Pause MQTT around ADS-B HTTPS (call mqttPauseForFetch / mqttResumeAfterFetch).

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

struct CloudControl {
  uint8_t brightness = 180;
  bool powerOn = true;
  bool schedEnabled = false;
  int onHour = 7, onMin = 0;
  int offHour = 23, offMin = 0;
};

static CloudControl gCtrl;
static Preferences gPrefs;
static WiFiClientSecure gMqttTls;
static PubSubClient gMqtt(gMqttTls);
static bool gMqttPaused = false;
static bool gMqttWantPublish = false;
static unsigned long gMqttLastAttempt = 0;
static unsigned long gLastSchedApply = 0;

static String mqttTopic(const char* leaf) {
  return String("ft/") + MQTT_DEVICE_ID + "/" + leaf;
}

static void saveCloudControl() {
  gPrefs.begin("ft", false);
  gPrefs.putUChar("bri", gCtrl.brightness);
  gPrefs.putBool("on", gCtrl.powerOn);
  gPrefs.putBool("sch", gCtrl.schedEnabled);
  gPrefs.putInt("onH", gCtrl.onHour);
  gPrefs.putInt("onM", gCtrl.onMin);
  gPrefs.putInt("offH", gCtrl.offHour);
  gPrefs.putInt("offM", gCtrl.offMin);
  gPrefs.end();
}

static void loadCloudControl() {
  gPrefs.begin("ft", true);
  gCtrl.brightness = gPrefs.getUChar("bri", 180);
  gCtrl.powerOn = gPrefs.getBool("on", true);
  gCtrl.schedEnabled = gPrefs.getBool("sch", false);
  gCtrl.onHour = gPrefs.getInt("onH", 7);
  gCtrl.onMin = gPrefs.getInt("onM", 0);
  gCtrl.offHour = gPrefs.getInt("offH", 23);
  gCtrl.offMin = gPrefs.getInt("offM", 0);
  gPrefs.end();
}

static bool scheduleWantsOn() {
  if (!gCtrl.schedEnabled) return gCtrl.powerOn;
  time_t now = time(nullptr);
  if (now < 1700000000) return gCtrl.powerOn;  // NTP not ready yet
  struct tm ti;
  localtime_r(&now, &ti);
  int mins = ti.tm_hour * 60 + ti.tm_min;
  int a = gCtrl.onHour * 60 + gCtrl.onMin;
  int b = gCtrl.offHour * 60 + gCtrl.offMin;
  if (a == b) return gCtrl.powerOn;
  if (a < b) return mins >= a && mins < b;   // e.g. 07:00–23:00
  return mins >= a || mins < b;              // overnight window
}

static bool panelShouldShow() {
  return scheduleWantsOn();
}

static void applyPanelOutput() {
  if (!dma_display) return;
  if (panelShouldShow()) {
    dma_display->setBrightness8(gCtrl.brightness);
  } else {
    dma_display->setBrightness8(0);
    dma_display->clearScreen();
  }
}

static void publishStatus() {
  if (!gMqtt.connected()) {
    gMqttWantPublish = true;
    return;
  }
  JsonDocument doc;
  doc["on"] = gCtrl.powerOn;
  doc["brightness"] = gCtrl.brightness;
  doc["schedEnabled"] = gCtrl.schedEnabled;
  doc["onHour"] = gCtrl.onHour;
  doc["onMin"] = gCtrl.onMin;
  doc["offHour"] = gCtrl.offHour;
  doc["offMin"] = gCtrl.offMin;
  doc["filterFlags"] = 0;
  doc["watchOnly"] = false;
  doc["fw"] = FIRMWARE_VERSION;
  doc["staIp"] = WiFi.localIP().toString();
  doc["heap"] = ESP.getFreeHeap();
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm ti;
    localtime_r(&now, &ti);
    char tbuf[20];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &ti);
    doc["time"] = tbuf;
  }
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0) gMqtt.publish(mqttTopic("status").c_str(), buf, true);
  gMqttWantPublish = false;
}

// Deferred flags set in MQTT callback — never publish/heavy work inside callback.
static volatile bool gCmdDirty = false;
static char gCmdBuf[256];

static void handleCmdJson(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("MQTT cmd JSON error");
    return;
  }

  bool changed = false;
  if (!doc["brightness"].isNull()) {
    int b = constrain((int)doc["brightness"], 0, 255);
    gCtrl.brightness = (uint8_t)b;
    changed = true;
  }
  if (!doc["on"].isNull()) {
    gCtrl.powerOn = doc["on"].as<bool>();
    changed = true;
  }
  if (!doc["schedEnabled"].isNull()) {
    gCtrl.schedEnabled = doc["schedEnabled"].as<bool>();
    changed = true;
  }
  if (!doc["onHour"].isNull()) {
    gCtrl.onHour = constrain((int)doc["onHour"], 0, 23);
    changed = true;
  }
  if (!doc["onMin"].isNull()) {
    gCtrl.onMin = constrain((int)doc["onMin"], 0, 59);
    changed = true;
  }
  if (!doc["offHour"].isNull()) {
    gCtrl.offHour = constrain((int)doc["offHour"], 0, 23);
    changed = true;
  }
  if (!doc["offMin"].isNull()) {
    gCtrl.offMin = constrain((int)doc["offMin"], 0, 59);
    changed = true;
  }

  // Serial-style fallbacks also accepted as strings in "cmd"
  if (doc["cmd"].is<const char*>()) {
    String c = doc["cmd"].as<const char*>();
    c.trim();
    if (c.equalsIgnoreCase("on")) { gCtrl.powerOn = true; gCtrl.schedEnabled = false; changed = true; }
    else if (c.equalsIgnoreCase("off")) { gCtrl.powerOn = false; gCtrl.schedEnabled = false; changed = true; }
    else if (c.startsWith("b:") || c.startsWith("B:")) {
      gCtrl.brightness = (uint8_t)constrain(c.substring(2).toInt(), 0, 255);
      changed = true;
    }
  }

  if (changed) {
    saveCloudControl();
    applyPanelOutput();
    Serial.printf("Ctrl: on=%d bri=%u sched=%d %02d:%02d-%02d:%02d show=%d\n",
                  gCtrl.powerOn ? 1 : 0, (unsigned)gCtrl.brightness,
                  gCtrl.schedEnabled ? 1 : 0,
                  gCtrl.onHour, gCtrl.onMin, gCtrl.offHour, gCtrl.offMin,
                  panelShouldShow() ? 1 : 0);
    gMqttWantPublish = true;
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length >= sizeof(gCmdBuf)) length = sizeof(gCmdBuf) - 1;
  memcpy(gCmdBuf, payload, length);
  gCmdBuf[length] = '\0';
  if (strstr(topic, "/cmd")) {
    gCmdDirty = true;
  }
}

static bool mqttConnectNow() {
  if (WiFi.status() != WL_CONNECTED) return false;
  gMqttTls.setInsecure();
  gMqtt.setServer(MQTT_HOST, MQTT_PORT);
  gMqtt.setCallback(mqttCallback);
  gMqtt.setBufferSize(512);
  gMqtt.setKeepAlive(30);

  String cid = String("ft-") + MQTT_DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("MQTT connecting %s:%d ...\n", MQTT_HOST, MQTT_PORT);
  if (!gMqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS,
                     mqttTopic("online").c_str(), 1, true, "0")) {
    Serial.printf("MQTT failed rc=%d\n", gMqtt.state());
    return false;
  }
  gMqtt.publish(mqttTopic("online").c_str(), "1", true);
  gMqtt.subscribe(mqttTopic("cmd").c_str(), 1);
  Serial.println("MQTT OK");
  publishStatus();
  return true;
}

static void mqttPauseForFetch() {
  gMqttPaused = true;
  if (gMqtt.connected()) {
    gMqtt.disconnect();
  }
  gMqttTls.stop();
  delay(50);
}

static void mqttResumeAfterFetch() {
  gMqttPaused = false;
  gMqttLastAttempt = 0;  // allow immediate reconnect
}

static void mqttLoop() {
  if (gMqttPaused) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (!gMqtt.connected()) {
    if (millis() - gMqttLastAttempt < 8000) return;
    gMqttLastAttempt = millis();
    mqttConnectNow();
    return;
  }

  gMqtt.loop();

  if (gCmdDirty) {
    gCmdDirty = false;
    handleCmdJson(gCmdBuf);
  }
  if (gMqttWantPublish) publishStatus();

  // Re-apply schedule every 30s (midnight / window edges)
  if (millis() - gLastSchedApply > 30000UL) {
    gLastSchedApply = millis();
    applyPanelOutput();
  }
}

// USB Serial: on | off | b:128 | status
static bool handleSerialPanelCommand(String cmd) {
  cmd.trim();
  if (!cmd.length()) return false;
  if (cmd.equalsIgnoreCase("on")) {
    gCtrl.powerOn = true;
    gCtrl.schedEnabled = false;
    saveCloudControl();
    applyPanelOutput();
    gMqttWantPublish = true;
    return true;
  }
  if (cmd.equalsIgnoreCase("off")) {
    gCtrl.powerOn = false;
    gCtrl.schedEnabled = false;
    saveCloudControl();
    applyPanelOutput();
    gMqttWantPublish = true;
    return true;
  }
  if (cmd.startsWith("b:") || cmd.startsWith("B:")) {
    gCtrl.brightness = (uint8_t)constrain(cmd.substring(2).toInt(), 0, 255);
    saveCloudControl();
    applyPanelOutput();
    gMqttWantPublish = true;
    Serial.printf("Brightness %u\n", (unsigned)gCtrl.brightness);
    return true;
  }
  if (cmd.equalsIgnoreCase("status") || cmd.equalsIgnoreCase("?")) {
    Serial.printf("on=%d bri=%u sched=%d %02d:%02d-%02d:%02d show=%d mqtt=%d\n",
                  gCtrl.powerOn ? 1 : 0, (unsigned)gCtrl.brightness,
                  gCtrl.schedEnabled ? 1 : 0,
                  gCtrl.onHour, gCtrl.onMin, gCtrl.offHour, gCtrl.offMin,
                  panelShouldShow() ? 1 : 0,
                  gMqtt.connected() ? 1 : 0);
    return true;
  }
  return false;
}

static void setupCloudControl() {
  loadCloudControl();
  configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  applyPanelOutput();
  Serial.printf("Cloud control ready — dashboard topic ft/%s/cmd\n", MQTT_DEVICE_ID);
}
