#pragma once
// Copy this file to secrets.h and fill in real values.
// secrets.h is gitignored — never commit passwords.

// Campus / home Wi‑Fi (Berkeley-IoT per-device key)
#define WIFI_SSID       "Berkeley-IoT"
#define WIFI_PASSWORD   "YOUR_WIFI_KEYS_PASSWORD"

// HiveMQ Cloud (free): https://www.hivemq.com/mqtt-cloud-broker/
// Cluster → MQTT Credentials + WebSocket port 8884 for the dashboard
#define MQTT_HOST       "YOUR_CLUSTER.s1.eu.hivemq.cloud"
#define MQTT_PORT       8883
#define MQTT_USER       "YOUR_MQTT_USERNAME"
#define MQTT_PASS       "YOUR_MQTT_PASSWORD"
#define MQTT_DEVICE_ID  "kamron-flighttracker"

// Bump this when you publish a new firmware binary
#define FIRMWARE_VERSION   "1.0.0"

// Raw GitHub URL to ota/manifest.json in THIS repo
#define OTA_MANIFEST_URL   "https://raw.githubusercontent.com/YOUR_GITHUB_USER/Flight_Tracker/main/ota/manifest.json"
