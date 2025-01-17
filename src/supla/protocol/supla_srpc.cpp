/*
 * Copyright (C) AC SOFTWARE SP. Z O.O
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <SuplaDevice.h>
#include <supla-common/srpc.h>
#include <supla/log_wrapper.h>
#include <supla/network/network.h>
#include <supla/time.h>
#include <supla/tools.h>
#include <supla/network/client.h>

#include <string.h>

#include "supla_srpc.h"


Supla::Protocol::SuplaSrpc::SuplaSrpc(SuplaDeviceClass *sdc, int version)
    : Supla::Protocol::ProtocolLayer(sdc), version(version) {
  client = Supla::ClientBuilder();
  client->setDebugLogs(true);
}

Supla::Protocol::SuplaSrpc::~SuplaSrpc() {
  delete client;
  client = nullptr;
}

bool Supla::Protocol::SuplaSrpc::onLoadConfig() {
  auto cfg = Supla::Storage::ConfigInstance();

  if (cfg == nullptr) {
    return false;
  }

  bool configComplete = true;
  char buf[256] = {};

  // Supla protocol specific config
  if (cfg->isSuplaCommProtocolEnabled()) {
    memset(buf, 0, sizeof(buf));
    if (cfg->getSuplaServer(buf) && strlen(buf) > 0) {
      sdc->setServer(buf);
    } else {
      SUPLA_LOG_INFO("Config incomplete: missing server");
      configComplete = false;
    }
    setServerPort(cfg->getSuplaServerPort());

    memset(buf, 0, sizeof(buf));
    if (cfg->getEmail(buf) && strlen(buf) > 0) {
      sdc->setEmail(buf);
    } else {
      SUPLA_LOG_INFO("Config incomplete: missing email");
      configComplete = false;
    }

    memset(buf, 0, sizeof(buf));
    if (cfg->getAuthKey(buf)) {
      sdc->setAuthKey(buf);
    }

    bool usePublicServer = false;
    if (Supla::Channel::reg_dev.ServerName[0] != '\0') {
      const int serverLength = strlen(Supla::Channel::reg_dev.ServerName);
      const char suplaPublicServerSuffix[] = ".supla.org";
      const int suplaPublicServerSuffixLength = strlen(suplaPublicServerSuffix);

      if (serverLength > suplaPublicServerSuffixLength) {
        if (strncmpInsensitive(Supla::Channel::reg_dev.ServerName +
                                   serverLength - suplaPublicServerSuffixLength,
                               suplaPublicServerSuffix,
                               suplaPublicServerSuffixLength) == 0) {
          usePublicServer = true;
        }
      }
    }

    // Public Supla server use different root CA for server certificate
    // validation then CA used for private servers
    if (!usePublicServer) {
      suplaCACert = supla3rdPartyCACert;
    }

    cfg->getUInt8("security_level", &securityLevel);
    if (securityLevel > 2) {
      securityLevel = 0;
    }

    client->setSSLEnabled(true);
    SUPLA_LOG_DEBUG("Security level: %d", securityLevel);
    switch (securityLevel) {
      default:
      case 0: {
        // in case of default security level it is required to use Supla CA
        // certificate. It should be set on application level before
        // SuplaDevice.begin() is called.
        // If it is null, we just assign "SUPLA" as a certificate value, which
        // will of course fail the certificate validation (which is intended).
        if (suplaCACert == nullptr) {
          SUPLA_LOG_ERROR(
              "Supla CA ceritificate is selected, but it is not set. "
              "Connection will fail");
          auto cert = new char[6];
          strncpy(cert, "SUPLA", 6);  // Some dummy value for CA cert
          suplaCACert = cert;
        }
        client->setCACert(suplaCACert);
        break;
      }
      case 1: {
        // custom CA from Config
        int len = cfg->getCustomCASize();
        if (len > 0) {
          len++;
          auto cert = new char[len];
          cfg->getCustomCA(cert, len);
          client->setCACert(cert);
        } else {
          SUPLA_LOG_ERROR(
              "Custom CA is selected, but certificate is"
              " missing in config. Connect will fail");
          auto cert = new char[6];
          strncpy(cert, "SUPLA", 6);  // some dummy value
          client->setCACert(cert);
        }
        break;
      }
      case 2: {
        // Skip certificate validation (INSECURE)
        break;
      }
    }
  }

  return configComplete;
}

void Supla::Protocol::SuplaSrpc::onInit() {
  TsrpcParams srpcParams;
  srpc_params_init(&srpcParams);
  srpcParams.data_read = &Supla::dataRead;
  srpcParams.data_write = &Supla::dataWrite;
  srpcParams.on_remote_call_received = &Supla::messageReceived;
  srpcParams.user_params = this;

  srpc = srpc_init(&srpcParams);

  // Set Supla protocol interface version
  srpc_set_proto_version(srpc, version);

  SUPLA_LOG_INFO("Using Supla protocol version %d", version);
}

void *Supla::Protocol::SuplaSrpc::getSrpcPtr() {
  return srpc;
}

_supla_int_t Supla::dataRead(void *buf, _supla_int_t count, void *userParams) {
  auto srpcLayer = reinterpret_cast<Supla::Protocol::SuplaSrpc*>(userParams);
  return srpcLayer->client->read(reinterpret_cast<uint8_t*>(buf), count);
}

_supla_int_t Supla::dataWrite(void *buf, _supla_int_t count, void *userParams) {
  auto srpcLayer = reinterpret_cast<Supla::Protocol::SuplaSrpc *>(userParams);
  _supla_int_t r =
      srpcLayer->client->write(reinterpret_cast<uint8_t *>(buf), count);
  if (r > 0) {
    srpcLayer->updateLastSentTime();
  }
  return r;
}

void Supla::messageReceived(void *srpc,
                            unsigned _supla_int_t rrId,
                            unsigned _supla_int_t callType,
                            void *userParam,
                            unsigned char protoVersion) {
  (void)(rrId);
  (void)(callType);
  (void)(protoVersion);
  TsrpcReceivedData rd;
  int8_t getDataResult;

  Supla::Protocol::SuplaSrpc *suplaSrpc =
      reinterpret_cast<Supla::Protocol::SuplaSrpc *>(userParam);

  suplaSrpc->updateLastResponseTime();

  if (SUPLA_RESULT_TRUE == (getDataResult = srpc_getdata(srpc, &rd, 0))) {
    switch (rd.call_type) {
      case SUPLA_SDC_CALL_VERSIONERROR:
        suplaSrpc->onVersionError(rd.data.sdc_version_error);
        break;
      case SUPLA_SD_CALL_REGISTER_DEVICE_RESULT:
        suplaSrpc->onRegisterResult(rd.data.sd_register_device_result);
        break;
      case SUPLA_SD_CALL_CHANNEL_SET_VALUE: {
        auto element = Supla::Element::getElementByChannelNumber(
            rd.data.sd_channel_new_value->ChannelNumber);
        if (element) {
          int actionResult =
              element->handleNewValueFromServer(rd.data.sd_channel_new_value);
          if (actionResult != -1) {
            srpc_ds_async_set_channel_result(
                srpc,
                rd.data.sd_channel_new_value->ChannelNumber,
                rd.data.sd_channel_new_value->SenderID,
                actionResult);
          }
        } else {
          SUPLA_LOG_DEBUG(
              "Error: couldn't find element for a requested channel [%d]",
              rd.data.sd_channel_new_value->ChannelNumber);
        }
        break;
      }
      case SUPLA_SDC_CALL_SET_ACTIVITY_TIMEOUT_RESULT:
        suplaSrpc->onSetActivityTimeoutResult(
            rd.data.sdc_set_activity_timeout_result);
        break;
      case SUPLA_CSD_CALL_GET_CHANNEL_STATE: {
        TDSC_ChannelState state;
        memset(&state, 0, sizeof(TDSC_ChannelState));
        state.ReceiverID = rd.data.csd_channel_state_request->SenderID;
        state.ChannelNumber = rd.data.csd_channel_state_request->ChannelNumber;
        Supla::Network::Instance()->fillStateData(&state);
        suplaSrpc->getSdc()->fillStateData(&state);
        auto element = Supla::Element::getElementByChannelNumber(
            rd.data.csd_channel_state_request->ChannelNumber);
        if (element) {
          element->handleGetChannelState(&state);
        }
        srpc_csd_async_channel_state_result(srpc, &state);
        break;
      }
      case SUPLA_SDC_CALL_PING_SERVER_RESULT:
        break;

      case SUPLA_DCS_CALL_GET_USER_LOCALTIME_RESULT: {
        suplaSrpc->onGetUserLocaltimeResult(rd.data.sdc_user_localtime_result);
        break;
      }
      case SUPLA_SD_CALL_DEVICE_CALCFG_REQUEST: {
        TDS_DeviceCalCfgResult result;
        result.ReceiverID = rd.data.sd_device_calcfg_request->SenderID;
        result.ChannelNumber = rd.data.sd_device_calcfg_request->ChannelNumber;
        result.Command = rd.data.sd_device_calcfg_request->Command;
        result.Result = SUPLA_CALCFG_RESULT_NOT_SUPPORTED;
        result.DataSize = 0;
        SUPLA_LOG_DEBUG(
            "CALCFG CMD received: senderId %d, ch %d, cmd %d, suauth %d, "
            "datatype %d, datasize %d, ",
            rd.data.sd_device_calcfg_request->SenderID,
            rd.data.sd_device_calcfg_request->ChannelNumber,
            rd.data.sd_device_calcfg_request->Command,
            rd.data.sd_device_calcfg_request->SuperUserAuthorized,
            rd.data.sd_device_calcfg_request->DataType,
            rd.data.sd_device_calcfg_request->DataSize);

        if (rd.data.sd_device_calcfg_request->SuperUserAuthorized != 1) {
          result.Result = SUPLA_CALCFG_RESULT_UNAUTHORIZED;
        } else {
          if (rd.data.sd_device_calcfg_request->ChannelNumber == -1) {
            // calcfg with channel == -1 are for whole device, so we route
            // it to SuplaDeviceClass instance
            result.Result = suplaSrpc->getSdc()->handleCalcfgFromServer(
                rd.data.sd_device_calcfg_request);
          } else {
            auto element = Supla::Element::getElementByChannelNumber(
                rd.data.sd_device_calcfg_request->ChannelNumber);
            if (element) {
              result.Result = element->handleCalcfgFromServer(
                  rd.data.sd_device_calcfg_request);
            } else {
              SUPLA_LOG_ERROR(
                  "Error: couldn't find element for a requested channel [%d]",
                  rd.data.sd_channel_new_value->ChannelNumber);
            }
          }
        }
        srpc_ds_async_device_calcfg_result(srpc, &result);
        break;
      }
      case SUPLA_SD_CALL_GET_CHANNEL_CONFIG_RESULT: {
        TSD_ChannelConfig *result = rd.data.sd_channel_config;
        if (result) {
          auto element =
              Supla::Element::getElementByChannelNumber(result->ChannelNumber);
          if (element) {
            element->handleChannelConfig(result);
          } else {
            SUPLA_LOG_DEBUG(
                "Error: couldn't find element for a requested channel [%d]",
                result->ChannelNumber);
          }
        }
        break;
      }
      case SUPLA_SD_CALL_CHANNELGROUP_SET_VALUE: {
        TSD_SuplaChannelGroupNewValue *groupNewValue =
            rd.data.sd_channelgroup_new_value;
        if (groupNewValue) {
          auto element = Supla::Element::getElementByChannelNumber(
              groupNewValue->ChannelNumber);
          if (element) {
            TSD_SuplaChannelNewValue newValue = {};
            newValue.SenderID = 0;
            newValue.ChannelNumber = groupNewValue->ChannelNumber;
            newValue.DurationMS = groupNewValue->DurationMS;
            memcpy(
                newValue.value, groupNewValue->value, SUPLA_CHANNELVALUE_SIZE);
            element->handleNewValueFromServer(&newValue);
          } else {
            SUPLA_LOG_DEBUG(
                "Error: couldn't find element for a requested channel [%d]",
                rd.data.sd_channel_new_value->ChannelNumber);
          }
        }
        break;
      }
      default:
        SUPLA_LOG_WARNING("Received unknown message from server!");
        break;
    }

    srpc_rd_free(&rd);

  } else if (getDataResult == SUPLA_RESULT_DATA_ERROR) {
    SUPLA_LOG_WARNING("DATA ERROR!");
  }
}

void Supla::Protocol::SuplaSrpc::onVersionError(
    TSDC_SuplaVersionError *versionError) {
  (void)(versionError);
  sdc->status(STATUS_PROTOCOL_VERSION_ERROR, "Protocol version error");
  SUPLA_LOG_ERROR("Protocol version error. Server min: %d; Server version: %d",
                  versionError->server_version_min,
                  versionError->server_version);

  disconnect();

  lastIterateTime = millis();
  waitForIterate = 15000;
}

void Supla::Protocol::SuplaSrpc::onRegisterResult(
    TSD_SuplaRegisterDeviceResult *registerDeviceResult) {
  uint32_t serverActivityTimeout = 0;

  switch (registerDeviceResult->result_code) {
    // OK scenario
    case SUPLA_RESULTCODE_TRUE:
      serverActivityTimeout = registerDeviceResult->activity_timeout;
      registered = 1;
      SUPLA_LOG_DEBUG(
          "Device registered (activity timeout %d s, server version: %d, "
          "server min version: %d)",
          registerDeviceResult->activity_timeout,
          registerDeviceResult->version,
          registerDeviceResult->version_min);
      lastIterateTime = millis();
      sdc->status(STATUS_REGISTERED_AND_READY, "Registered and ready");

      if (serverActivityTimeout != activityTimeoutS) {
        SUPLA_LOG_DEBUG("Changing activity timeout to %d", activityTimeoutS);
        TDCS_SuplaSetActivityTimeout at;
        at.activity_timeout = activityTimeoutS;
        srpc_dcs_async_set_activity_timeout(srpc, &at);
      }

      for (auto element = Supla::Element::begin(); element != nullptr;
           element = element->next()) {
        element->onRegistered();
        delay(0);
      }

      return;

      // NOK scenarios
    case SUPLA_RESULTCODE_TEMPORARILY_UNAVAILABLE:
      sdc->status(
          STATUS_TEMPORARILY_UNAVAILABLE, "Temporarily unavailable!", true);
      break;

    case SUPLA_RESULTCODE_GUID_ERROR:
      sdc->status(STATUS_INVALID_GUID, "Incorrect device GUID!", true);
      break;

    case SUPLA_RESULTCODE_AUTHKEY_ERROR:
      sdc->status(STATUS_INVALID_AUTHKEY, "Incorrect AuthKey!", true);
      break;

    case SUPLA_RESULTCODE_BAD_CREDENTIALS:
      sdc->status(STATUS_BAD_CREDENTIALS,
                  "Bad credentials - incorrect AuthKey or email",
                  true);
      break;

    case SUPLA_RESULTCODE_REGISTRATION_DISABLED:
      sdc->status(STATUS_REGISTRATION_DISABLED, "Registration disabled!", true);
      break;

    case SUPLA_RESULTCODE_DEVICE_LIMITEXCEEDED:
      sdc->status(STATUS_DEVICE_LIMIT_EXCEEDED, "Device limit exceeded!", true);
      break;

    case SUPLA_RESULTCODE_NO_LOCATION_AVAILABLE:
      sdc->status(STATUS_NO_LOCATION_AVAILABLE, "No location available!", true);
      break;

    case SUPLA_RESULTCODE_DEVICE_DISABLED:
      sdc->status(STATUS_DEVICE_IS_DISABLED, "Device is disabled!", true);
      break;

    case SUPLA_RESULTCODE_LOCATION_DISABLED:
      sdc->status(STATUS_LOCATION_IS_DISABLED, "Location is disabled!", true);
      break;

    case SUPLA_RESULTCODE_LOCATION_CONFLICT:
      sdc->status(STATUS_LOCATION_CONFLICT, "Location conflict!", true);
      break;

    case SUPLA_RESULTCODE_CHANNEL_CONFLICT:
      sdc->status(STATUS_CHANNEL_CONFLICT, "Channel conflict!", true);
      break;

    default:
      sdc->status(STATUS_UNKNOWN_ERROR, "Unknown registration error", true);
      SUPLA_LOG_ERROR("Register result code %i",
                      registerDeviceResult->result_code);
      break;
  }

  disconnect();
  // server rejected registration
  registered = 2;
}

void Supla::Protocol::SuplaSrpc::onSetActivityTimeoutResult(
    TSDC_SuplaSetActivityTimeoutResult *result) {
  setActivityTimeout(result->activity_timeout);
  SUPLA_LOG_DEBUG("Activity timeout set to %d s", result->activity_timeout);
}

void Supla::Protocol::SuplaSrpc::setActivityTimeout(
    uint32_t activityTimeoutSec) {
  activityTimeoutS = activityTimeoutSec;
}

bool Supla::Protocol::SuplaSrpc::ping() {
  _supla_int64_t _millis = millis();
  // If time from last response is longer than "server_activity_timeout + 10 s",
  // then inform about failure in communication
  if ((_millis - lastResponseMs) / 1000 >= (activityTimeoutS + 10)) {
    return false;
  } else if (_millis - lastPingTimeMs >= 5000 &&
             ((_millis - lastResponseMs) / 1000 >= (activityTimeoutS - 5) ||
              (_millis - lastSentMs) / 1000 >= (activityTimeoutS - 5))) {
    lastPingTimeMs = _millis;
    srpc_dcs_async_ping_server(srpc);
  }
  return true;
}

void Supla::Protocol::SuplaSrpc::iterate(uint64_t _millis) {
  requestNetworkRestart = false;
  if (waitForIterate != 0 && _millis - lastIterateTime < waitForIterate) {
    return;
  }

  waitForIterate = 0;

  // Wait for registration (timeout) use lastIterateTime, so we don't change
  // it here if we're waiting for registration reply
  if (registered != -1) {
    lastIterateTime = _millis;
  }

  // Establish connection with Supla server
  if (!client->connected()) {
    sdc->uptime.setConnectionLostCause(
        SUPLA_LASTCONNECTIONRESETCAUSE_SERVER_CONNECTION_LOST);
    registered = 0;
    if (port == -1) {
      // TODO(klew): add ssl handling
      port = 2015;
    }
    int result = client->connect(Supla::Channel::reg_dev.ServerName, port);
    if (1 == result) {
      sdc->uptime.resetConnectionUptime();
      connectionFailCounter = 0;
      //      lastConnectionResetCounter = 0;
      SUPLA_LOG_INFO("Connected to Supla Server");

    } else {
      sdc->status(STATUS_SERVER_DISCONNECTED, "Not connected to Supla server");
      SUPLA_LOG_DEBUG("Connection fail (%d). Server: %s",
                      result,
                      Supla::Channel::reg_dev.ServerName);
      disconnect();
      waitForIterate = 10000;
      connectionFailCounter++;
      if (connectionFailCounter % 6 == 0) {
        requestNetworkRestart = true;
      }
      return;
    }
  }

  if (srpc_iterate(srpc) == SUPLA_RESULT_FALSE) {
    sdc->status(STATUS_ITERATE_FAIL, "Communication failure");
    disconnect();

    lastIterateTime = _millis;
    waitForIterate = 5000;
    return;
  }

  if (registered == 0) {
    // Perform registration if we are not yet registered
    registered = -1;
    sdc->status(STATUS_REGISTER_IN_PROGRESS, "Register in progress");
    if (!srpc_ds_async_registerdevice_e(srpc, &Supla::Channel::reg_dev)) {
      SUPLA_LOG_WARNING("Fatal SRPC failure!");
    }
    return;
  } else if (registered == -1) {
    // Handle registration timeout (in case of no reply received)
    if (_millis - lastIterateTime > 10 * 1000) {
      SUPLA_LOG_DEBUG(
          "No reply to registration message. Resetting connection.");
      sdc->status(STATUS_SERVER_DISCONNECTED, "Not connected to Supla server");
      disconnect();

      lastIterateTime = _millis;
      waitForIterate = 2000;
    }
    return;
  } else if (registered == 1) {
    // Device is registered and everything is correct

    if (ping() == false) {
      sdc->uptime.setConnectionLostCause(
          SUPLA_LASTCONNECTIONRESETCAUSE_ACTIVITY_TIMEOUT);
      SUPLA_LOG_DEBUG("TIMEOUT - lost connection with server");
      sdc->status(STATUS_SERVER_DISCONNECTED, "Not connected to Supla server");
      disconnect();
    }

    // Iterate all elements
    for (auto element = Supla::Element::begin(); element != nullptr;
         element = element->next()) {
      if (!element->iterateConnected(srpc)) {
        break;
      }
      delay(0);
    }
    return;
  } else if (registered == 2) {
    // Server rejected registration
    registered = 0;
    lastIterateTime = millis();
    waitForIterate = 10000;
  }
  return;
}

void Supla::Protocol::SuplaSrpc::disconnect() {
  registered = 0;
  client->stop();
}

void Supla::Protocol::SuplaSrpc::updateLastResponseTime() {
  lastResponseMs = millis();
}

void Supla::Protocol::SuplaSrpc::updateLastSentTime() {
  lastSentMs = millis();
}

void Supla::Protocol::SuplaSrpc::setServerPort(int value) {
  port = value;
}

void Supla::Protocol::SuplaSrpc::setVersion(int value) {
  version = value;
}

void Supla::Protocol::SuplaSrpc::setSuplaCACert(const char *cert) {
  suplaCACert = cert;
}

void Supla::Protocol::SuplaSrpc::setSupla3rdPartyCACert(const char *cert) {
  supla3rdPartyCACert = cert;
}

void Supla::Protocol::SuplaSrpc::onGetUserLocaltimeResult(
    TSDC_UserLocalTimeResult *result) {
  auto clock = getSdc()->getClock();
  if (clock) {
    clock->parseLocaltimeFromServer(result);
  }
}

bool Supla::Protocol::SuplaSrpc::isNetworkRestartRequested() {
  return requestNetworkRestart;
}

uint32_t Supla::Protocol::SuplaSrpc::getConnectionFailTime() {
  // connectionFailCounter is incremented every 10 s
  return connectionFailCounter * 10;
}
