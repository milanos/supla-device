/*
 Copyright (C) AC SOFTWARE SP. Z O.O.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef SRC_SUPLA_NETWORK_ESP_WIFI_H_
#define SRC_SUPLA_NETWORK_ESP_WIFI_H_

#include <Arduino.h>

#include <supla/log_wrapper.h>

#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>

// workaround for incompatible names in ESP8266 and ESP32 boards
#define WIFI_MODE_AP  WIFI_AP
#define WIFI_MODE_STA WIFI_STA

#else
#include <WiFi.h>
#endif

#include "../supla_lib_config.h"
#include "netif_wifi.h"

// TODO(klew): change logs to supla_log

namespace Supla {
class ESPWifi : public Supla::Wifi {
 public:
  ESPWifi(const char *wifiSsid = nullptr,
          const char *wifiPassword = nullptr,
          unsigned char *ip = nullptr)
      : Wifi(wifiSsid, wifiPassword, ip) {
  }


  bool isReady() override {
    return WiFi.status() == WL_CONNECTED;
  }

  // TODO(klew): add handling of custom local ip
  void setup() override {
    if (!wifiConfigured) {
      // ESP32 requires setHostname to be called before begin...
      WiFi.setHostname(hostname);
      wifiConfigured = true;
#ifdef ARDUINO_ARCH_ESP8266
      gotIpEventHandler =
        WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
            (void)(event);
            Serial.print(F("local IP: "));
            Serial.println(WiFi.localIP());
            Serial.print(F("subnetMask: "));
            Serial.println(WiFi.subnetMask());
            Serial.print(F("gatewayIP: "));
            Serial.println(WiFi.gatewayIP());
            int rssi = WiFi.RSSI();
            Serial.print(F("Signal strength (RSSI): "));
            Serial.print(rssi);
            Serial.println(F(" dBm"));
            });
      disconnectedEventHandler = WiFi.onStationModeDisconnected(
          [](const WiFiEventStationModeDisconnected &event) {
          (void)(event);
          Serial.println(F("WiFi station disconnected"));
          });
#else
      WiFiEventId_t event_gotIP = WiFi.onEvent(
          [](WiFiEvent_t event, WiFiEventInfo_t info) {
          Serial.print(F("local IP: "));
          Serial.println(WiFi.localIP());
          Serial.print(F("subnetMask: "));
          Serial.println(WiFi.subnetMask());
          Serial.print(F("gatewayIP: "));
          Serial.println(WiFi.gatewayIP());
          int rssi = WiFi.RSSI();
          Serial.print(F("Signal Strength (RSSI): "));
          Serial.print(rssi);
          Serial.println(F(" dBm"));
          },
          WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

      (void)(event_gotIP);

      WiFiEventId_t event_disconnected = WiFi.onEvent(
          [](WiFiEvent_t event, WiFiEventInfo_t info) {
          Serial.println(F("WiFi Station disconnected"));
            // ESP32 doesn't reconnect automatically after lost connection
            WiFi.reconnect();
          },
          WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

      (void)(event_disconnected);
#endif
    } else {
      if (mode == Supla::DEVICE_MODE_CONFIG) {
        WiFi.mode(WIFI_MODE_AP);
      } else {
        WiFi.mode(WIFI_MODE_STA);
      }
      Serial.println(F("WiFi: resetting WiFi connection"));
      Disconnect();
      WiFi.disconnect();
    }

    if (mode == Supla::DEVICE_MODE_CONFIG) {
      SUPLA_LOG_INFO("WiFi: enter config mode with SSID: \"%s\"", hostname);
      WiFi.mode(WIFI_MODE_AP);
      WiFi.softAP(hostname, nullptr, 6);

    } else {
      SUPLA_LOG_INFO("WiFi: establishing connection with SSID: \"%s\"", ssid);
      WiFi.mode(WIFI_MODE_STA);
      WiFi.begin(ssid, password);
      // ESP8266 requires setHostname to be called after begin...
      WiFi.setHostname(hostname);
    }

    yield();
  }

  // DEPRECATED, use setSSLEnabled instead
  void enableSSL(bool value) {
    setSSLEnabled(value);
  }

  void fillStateData(TDSC_ChannelState *channelState) override {
    channelState->Fields |= SUPLA_CHANNELSTATE_FIELD_IPV4 |
                            SUPLA_CHANNELSTATE_FIELD_MAC |
                            SUPLA_CHANNELSTATE_FIELD_WIFIRSSI |
                            SUPLA_CHANNELSTATE_FIELD_WIFISIGNALSTRENGTH;
    channelState->IPv4 = WiFi.localIP();
    WiFi.macAddress(channelState->MAC);
    int rssi = WiFi.RSSI();
    channelState->WiFiRSSI = rssi;
    if (rssi > -50) {
      channelState->WiFiSignalStrength = 100;
    } else if (rssi <= -100) {
      channelState->WiFiSignalStrength = 0;
    } else {
      channelState->WiFiSignalStrength = 2 * (rssi + 100);
    }
  }

  bool getMacAddr(uint8_t *out) override {
    WiFi.macAddress(out);
    return true;
  }

  void uninit() override {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
  }

 protected:
  bool wifiConfigured = false;

#ifdef ARDUINO_ARCH_ESP8266
  WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
#endif
};

};  // namespace Supla

#endif  // SRC_SUPLA_NETWORK_ESP_WIFI_H_
