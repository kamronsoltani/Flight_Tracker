# Cloud dashboard + OTA setup

Control the panel from your phone on **eduroam / cellular** (no SoftAP). Firmware updates pull from **GitHub Releases** over HTTPS.

## 1. HiveMQ Cloud (free)

1. Create a cluster at [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/)
2. Create an MQTT credential (username + password)
3. Note:
   - Host: `xxxx.s1.eu.hivemq.cloud`
   - MQTT TLS port: **8883** (ESP32)
   - WebSocket TLS port: **8884** (dashboard) → URL like `wss://xxxx.s1.eu.hivemq.cloud:8884/mqtt`

## 2. Device secrets

```bash
cp secrets.example.h secrets.h
```

Edit `secrets.h`: Wi‑Fi, MQTT host/user/pass, `MQTT_DEVICE_ID`, and `OTA_MANIFEST_URL` (raw GitHub URL to `ota/manifest.json`).

## 3. Arduino libraries

Install via Library Manager:

- **PubSubClient** (Nick O’Leary)
- Existing: ArduinoJson, ESP32-HUB75-MatrixPanel-I2S-DMA

Flash **once over USB** with the new firmware.

## 4. GitHub Pages dashboard

```bash
cp web/config.example.js web/config.js
```

Edit `web/config.js` with the same MQTT user/pass, `deviceId`, and `wss://…:8884/mqtt`.

Enable Pages on this repo (Settings → Pages → Deploy from branch → `/web` or root `web`).

Or push `web/` to a `gh-pages` branch. Open the same URL on a **phone or computer** — one dashboard for both.

`https://YOUR_USER.github.io/Flight_Tracker/`

(If Pages serves from `/web`, use that path.)

Dashboard features: live map, watchlist (track specific callsigns), layout/theme customization (saved in the browser), filters, schedule, OTA.

**Tip:** For a public repo, use a HiveMQ user restricted to topic `ft/YOUR_DEVICE_ID/#` so credentials in `config.js` can’t control other devices.

## 5. Cloud OTA (no USB)

1. In Arduino IDE: **Sketch → Export Compiled Binary**
2. Create a GitHub Release (e.g. `v1.0.1`) and upload `Flight_Tracker.ino.bin`
3. Update `ota/manifest.json`:

```json
{
  "version": "1.0.1",
  "url": "https://github.com/YOUR_USER/Flight_Tracker/releases/download/v1.0.1/Flight_Tracker.ino.bin",
  "notes": "…"
}
```

4. Bump `#define FIRMWARE_VERSION` in `secrets.h` **in the binary you exported** so the device knows what it is running (the running firmware’s version is compiled in; the manifest version must be **different/newer** to trigger update).

5. On the dashboard click **Check & OTA update**. The panel downloads the `.bin` over HTTPS and reboots.

Serial (USB) also accepts: `ota` · `on` · `off` · `b:128`

## Topics

| Topic | Direction |
|---|---|
| `ft/{id}/status` | ESP → dash |
| `ft/{id}/flights` | ESP → dash |
| `ft/{id}/online` | ESP → dash (`1`/`0`, retained) |
| `ft/{id}/cmd` | dash → ESP |
| `ft/{id}/ota` | ESP → dash (progress text) |
