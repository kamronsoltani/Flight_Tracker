#pragma once
// Phone control over Wi‑Fi (HTTP). BLE overflows IRAM on classic ESP32 + HUB75.
// Open http://<panel-ip>/ on the same network (Berkeley-IoT / hotspot).

#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>

struct PanelControl {
  uint8_t brightness = 180;
  bool powerOn = true;
  bool schedEnabled = false;
  int schedOffStartMin = 22 * 60;
  int schedOffEndMin = 7 * 60;
};

static PanelControl gCtrl;
static Preferences gPrefs;
static WebServer gWeb(80);

static int parseHHMM(const String& s) {
  int colon = s.indexOf(':');
  if (colon < 1) return -1;
  int hh = s.substring(0, colon).toInt();
  int mm = s.substring(colon + 1).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
  return hh * 60 + mm;
}

static bool scheduleWantsOff() {
  if (!gCtrl.schedEnabled) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm ti;
  localtime_r(&now, &ti);
  int mins = ti.tm_hour * 60 + ti.tm_min;
  int a = gCtrl.schedOffStartMin;
  int b = gCtrl.schedOffEndMin;
  if (a == b) return false;
  if (a < b) return mins >= a && mins < b;
  return mins >= a || mins < b;
}

static bool panelShouldShow() {
  if (scheduleWantsOff()) return false;
  return gCtrl.powerOn;
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

static void savePanelControl() {
  gPrefs.begin("ft", false);
  gPrefs.putUChar("bri", gCtrl.brightness);
  gPrefs.putBool("on", gCtrl.powerOn);
  gPrefs.putBool("sch", gCtrl.schedEnabled);
  gPrefs.putInt("schA", gCtrl.schedOffStartMin);
  gPrefs.putInt("schB", gCtrl.schedOffEndMin);
  gPrefs.end();
}

static void loadPanelControl() {
  gPrefs.begin("ft", true);
  gCtrl.brightness = gPrefs.getUChar("bri", 180);
  gCtrl.powerOn = gPrefs.getBool("on", true);
  gCtrl.schedEnabled = gPrefs.getBool("sch", false);
  gCtrl.schedOffStartMin = gPrefs.getInt("schA", 22 * 60);
  gCtrl.schedOffEndMin = gPrefs.getInt("schB", 7 * 60);
  gPrefs.end();
}

static String panelStatusLine() {
  char buf[192];
  int a = gCtrl.schedOffStartMin, b = gCtrl.schedOffEndMin;
  snprintf(buf, sizeof(buf),
           "on=%d bri=%u show=%d sched=%d %02d:%02d-%02d:%02d ip=%s",
           gCtrl.powerOn ? 1 : 0,
           (unsigned)gCtrl.brightness,
           panelShouldShow() ? 1 : 0,
           gCtrl.schedEnabled ? 1 : 0,
           a / 60, a % 60, b / 60, b % 60,
           WiFi.localIP().toString().c_str());
  return String(buf);
}

static void controlLog(const String& msg) {
  Serial.println(msg);
}

// Shared by Serial + HTTP. Returns true if handled.
static bool handlePanelCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return false;

  if (cmd.equalsIgnoreCase("on")) {
    gCtrl.powerOn = true;
    savePanelControl();
    applyPanelOutput();
    controlLog(panelStatusLine());
    return true;
  }
  if (cmd.equalsIgnoreCase("off")) {
    gCtrl.powerOn = false;
    savePanelControl();
    applyPanelOutput();
    controlLog(panelStatusLine());
    return true;
  }
  if (cmd.equalsIgnoreCase("status") || cmd.equalsIgnoreCase("?")) {
    controlLog(panelStatusLine());
    return true;
  }
  if (cmd.equalsIgnoreCase("help")) {
    controlLog("cmds: on | off | b:0-255 | sched:HH:MM-HH:MM | sched:off | status");
    return true;
  }
  if (cmd.startsWith("b:") || cmd.startsWith("B:")) {
    int bval = constrain(cmd.substring(2).toInt(), 0, 255);
    gCtrl.brightness = (uint8_t)bval;
    savePanelControl();
    applyPanelOutput();
    controlLog(panelStatusLine());
    return true;
  }
  if (cmd.startsWith("sched:") || cmd.startsWith("SCHED:")) {
    String body = cmd.substring(6);
    body.trim();
    if (body.equalsIgnoreCase("off") || body.equalsIgnoreCase("disable")) {
      gCtrl.schedEnabled = false;
      savePanelControl();
      applyPanelOutput();
      controlLog(panelStatusLine());
      return true;
    }
    int dash = body.indexOf('-');
    if (dash > 0) {
      int a = parseHHMM(body.substring(0, dash));
      int b = parseHHMM(body.substring(dash + 1));
      if (a >= 0 && b >= 0) {
        gCtrl.schedOffStartMin = a;
        gCtrl.schedOffEndMin = b;
        gCtrl.schedEnabled = true;
        savePanelControl();
        applyPanelOutput();
        controlLog(panelStatusLine());
        return true;
      }
    }
    controlLog("bad sched — use sched:22:00-07:00 or sched:off");
    return true;
  }
  return false;
}

static const char PANEL_UI[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>FlightTracker</title>
<style>
:root{--bg:#0e1419;--fg:#e8eef2;--muted:#8a9aa8;--accent:#e07a2f;--line:#24303a}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font-family:ui-monospace,Menlo,Consolas,monospace;
background:radial-gradient(900px 500px at 10% -10%,#1a2833,var(--bg));color:var(--fg);padding:1.2rem}
h1{font-size:1.05rem;margin:0 0 .4rem;letter-spacing:.04em}
p{color:var(--muted);font-size:.85rem;line-height:1.4;margin:0 0 1rem}
.row{display:flex;flex-wrap:wrap;gap:.5rem;margin-bottom:.7rem}
button,input{font:inherit;border:1px solid var(--line);background:#152028;color:var(--fg);
border-radius:6px;padding:.7rem .9rem}
button{cursor:pointer}
button.primary{background:var(--accent);border-color:transparent;color:#111;font-weight:600}
input[type=range]{width:min(100%,280px)}
#status{margin-top:1rem;padding:.75rem;border:1px solid var(--line);border-radius:8px;
color:var(--muted);font-size:.8rem;white-space:pre-wrap;min-height:3rem}
</style>
</head>
<body>
<h1>FLIGHTTRACKER</h1>
<p>Same Wi‑Fi as the panel. Settings are saved on the device.</p>
<div class="row">
<button class="primary" onclick="go('on')">On</button>
<button onclick="go('off')">Off</button>
<button onclick="refresh()">Status</button>
</div>
<div class="row">
<label>Brightness <span id="bv">180</span></label>
</div>
<div class="row">
<input id="bri" type="range" min="0" max="255" value="180"
 oninput="document.getElementById('bv').textContent=this.value"
 onchange="go('b:'+this.value)"/>
</div>
<div class="row">
<button onclick="go('sched:22:00-07:00')">Night 22:00–07:00</button>
<button onclick="go('sched:off')">Clear schedule</button>
</div>
<div id="status">…</div>
<script>
async function go(cmd){
  const r=await fetch('/cmd?c='+encodeURIComponent(cmd));
  document.getElementById('status').textContent=await r.text();
}
async function refresh(){
  const r=await fetch('/status');
  const t=await r.text();
  document.getElementById('status').textContent=t;
  const m=t.match(/bri=(\d+)/); if(m){
    document.getElementById('bri').value=m[1];
    document.getElementById('bv').textContent=m[1];
  }
}
refresh();
</script>
</body>
</html>
)HTML";

static void handleRoot() {
  gWeb.send_P(200, "text/html", PANEL_UI);
}

static void handleStatus() {
  gWeb.send(200, "text/plain", panelStatusLine());
}

static void handleCmd() {
  String c = gWeb.hasArg("c") ? gWeb.arg("c") : "";
  if (!handlePanelCommand(c)) {
    gWeb.send(400, "text/plain", "unknown command — try on/off/b:128/sched:22:00-07:00/status");
    return;
  }
  gWeb.send(200, "text/plain", panelStatusLine());
}

static void startPanelWeb() {
  gWeb.on("/", handleRoot);
  gWeb.on("/status", handleStatus);
  gWeb.on("/cmd", handleCmd);
  gWeb.onNotFound([]() { gWeb.send(404, "text/plain", "not found"); });
  gWeb.begin();

  if (MDNS.begin("flighttracker")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("Control UI: http://flighttracker.local/  (or http://" +
                   WiFi.localIP().toString() + "/)");
  } else {
    Serial.println("Control UI: http://" + WiFi.localIP().toString() + "/");
  }
}

// Kept name so loop() still compiles after dropping BLE.
static void pollPanelBleCommands() {
  gWeb.handleClient();
}

static void setupPanelTime() {
  configTzTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
}

static void setupPanelControl() {
  loadPanelControl();
  setupPanelTime();
  startPanelWeb();
}
