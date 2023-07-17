// SPDX-FileCopyrightText: 2023 ThingPulse Ltd., https://thingpulse.com
// SPDX-License-Identifier: MIT

#pragma once

#include <WiFi.h>
#include <nvs_flash.h>

extern "C" {
#include <esp_wifi.h>
}


#include "settings.h"

static void startWiFi() {
  if (SSID == nullptr || WIFI_PWD == nullptr) {
    wifi_config_t current_conf;
    if (WiFi.enableSTA(true) && esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf) == ESP_OK) {
      log_i("Connecting to WiFi '%s'...", current_conf.sta.ssid);
    } else {
      log_w("Cannot determine WiFi configuration!");
    }
    WiFi.begin();
  } else {
    WiFi.begin(SSID, WIFI_PWD);
  }
}

void waitWifiStarted(uint16_t max_delay_sec) {
  const static uint32_t dly = 200;   // [ms]
  uint16_t retry = max_delay_sec * 1000 / dly;

  while (WiFi.status() != WL_CONNECTED) {
    if (retry == 0) {
      esp_restart();
    }
    retry--;
    log_i(".");
    delay(200);
  }
  log_i("...done. IP: %s, WiFi RSSI: %d.", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void stopWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true, false);
  }
}

static void setModemSleep() {
  WiFi.setSleep(true);
  if (!setCpuFrequencyMhz(40)) {
    log_w("40 MHz not a valid frequency!");
  } else {
    // Use this if 40Mhz is not supported
    setCpuFrequencyMhz(80);
  }
}

static void wakeModemSleep() {
  setCpuFrequencyMhz(240);
  WiFi.setSleep(false);
}
