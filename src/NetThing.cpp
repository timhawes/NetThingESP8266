#include "NetThing.hpp"
#include "base64.hpp"

using namespace std::placeholders;

NetThing::NetThing():
  jsonstream(1500, 1500)
{
  wifiEventConnectHandler = WiFi.onStationModeGotIP(std::bind(&NetThing::wifiConnectHandler, this));
  wifiEventDisconnectHandler = WiFi.onStationModeDisconnected(std::bind(&NetThing::wifiDisconnectHandler, this));
  jsonstream.onConnect(std::bind(&NetThing::jsonConnectHandler, this));
  jsonstream.onDisconnect(std::bind(&NetThing::jsonDisconnectHandler, this));
  jsonstream.onReceiveJson(std::bind(&NetThing::jsonReceiveHandler, this, _1));
  file_writer = new FileWriter;
  firmware_writer = new FirmwareWriter;
}

void NetThing::jsonConnectHandler() {
  StaticJsonDocument<JSON_OBJECT_SIZE(4) + 128> doc;
  doc[cmd_key] = "hello";
  doc["clientid"] = server_username;
  doc["username"] = server_username;
  doc["password"] = server_password;
  sendJson(doc);
  if (connect_callback) {
    connect_callback();
  }
}

void NetThing::jsonDisconnectHandler() {
  file_writer->abort();
  firmware_writer->abort();
  if (disconnect_callback) {
    disconnect_callback();
  }
}

void NetThing::loop() {
  jsonstream.loop();

  if (restart_needed) {
    if (restart_callback) {
      // main application may restart if convenient
      restart_callback(false, restart_firmware);
    } else {
      // no handler available, perform our own restart immediately
      ESP.restart();
      delay(5000);
    }
  }

  if (file_writer) {
    if (file_writer->idleMillis() > 30000) {
      Serial.println("NetThing: timing-out file writer");
      file_writer->abort();
      StaticJsonDocument<JSON_OBJECT_SIZE(2)> doc;
      doc[cmd_key] = "error";
      doc["error"] = "file write timed-out";
      sendJson(doc);
    }
  }

  if (firmware_writer) {
    if (firmware_writer->idleMillis() > 30000) {
      Serial.println("NetThing: timing-out firmware writer");
      firmware_writer->abort();
      StaticJsonDocument<JSON_OBJECT_SIZE(2)> doc;
      doc[cmd_key] = "error";
      doc["error"] = "firmware write timed-out";
      sendJson(doc);
    }
  }

  if (watchdog_timeout > 0) {
    if (millis() - last_packet_received > watchdog_timeout) {
      Serial.println("NetThing: watchdog triggered, restarting");
      if (restart_callback) {
        // main application may restart if convenient
        restart_callback(false, restart_firmware);
      } else {
        // no handler available, perform our own restart immediately
        ESP.restart();
        delay(5000);
      }
    }
  }
}

void NetThing::onConnect(NetThingConnectHandler callback) {
  connect_callback = callback;
}

void NetThing::onDisconnect(NetThingDisconnectHandler callback) {
  disconnect_callback = callback;
}

void NetThing::onReceiveJson(NetThingReceivePacketHandler callback) {
  receivejson_callback = callback;
}

void NetThing::onRestartRequest(NetThingRestartRequestHandler callback) {
  restart_callback = callback;
}

void NetThing::onTransferStatus(NetThingTransferStatusHandler callback) {
  transfer_status_callback = callback;
}

bool NetThing::sendJson(const JsonDocument &doc, bool now) {
  return jsonstream.sendJson(doc);
}

void NetThing::setCommandKey(const char *key) {
  cmd_key = key;
}

void NetThing::setCred(const char *username, const char *password) {
  server_username = username;
  server_password = password;
}

void NetThing::setDebug(bool enabled) {
  jsonstream.setDebug(enabled);
}

void NetThing::setServer(const char *host, int port,
                              bool secure, bool verify,
                              const uint8_t *fingerprint1,
                              const uint8_t *fingerprint2) {
  jsonstream.setServer(host, port, secure, verify, fingerprint1, fingerprint2);
}

void NetThing::setWatchdog(unsigned int timeout) {
  watchdog_timeout = timeout;
}

void NetThing::setWiFi(const char *ssid, const char *password) {
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.enableAP(false);
  WiFi.enableSTA(true);
  WiFi.begin(ssid, password);
}

void NetThing::start() {
  jsonstream.start();
}

void NetThing::stop() {
  jsonstream.stop();
}

void NetThing::wifiConnectHandler() {
  Serial.println("NetThing: wifi connected");
  wifi_reconnections++;
}

void NetThing::wifiDisconnectHandler() {
  Serial.println("NetThing: wifi disconnected");
}

void NetThing::sendFileInfo(const char *filename)
{
  StaticJsonDocument<JSON_OBJECT_SIZE(4) + 64> obj;
  obj[cmd_key] = "file_info";
  obj["filename"] = filename;

  File f = SPIFFS.open(filename, "r");
  if (f) {
    MD5Builder md5;
    md5.begin();
    while (f.available()) {
      uint8_t buf[256];
      size_t buflen;
      buflen = f.readBytes((char*)buf, 256);
      md5.add(buf, buflen);
    }
    md5.calculate();
    obj["size"] = f.size();
    obj["md5"] = md5.toString();
    f.close();
  } else {
    obj["size"] = (char*)NULL;
    obj["md5"] = (char*)NULL;
  }

  sendJson(obj);
}

void NetThing::jsonReceiveHandler(const JsonDocument &doc) {
  last_packet_received = millis();
  if (doc.containsKey(cmd_key)) {
    const char *cmd = doc[cmd_key];
    if (strcmp(cmd, "file_data") == 0) {
      cmdFileData(doc);
    } else if (strcmp(cmd, "file_delete") == 0) {
      cmdFileDelete(doc);
    } else if (strcmp(cmd, "file_dir_query") == 0) {
      cmdFileDirQuery(doc);
    } else if (strcmp(cmd, "file_query") == 0) {
      cmdFileQuery(doc);
    } else if (strcmp(cmd, "file_rename") == 0) {
      cmdFileRename(doc);
    } else if (strcmp(cmd, "file_write") == 0) {
      cmdFileWrite(doc);
    } else if (strcmp(cmd, "firmware_data") == 0) {
      cmdFirmwareData(doc);
    } else if (strcmp(cmd, "firmware_write") == 0) {
      cmdFirmwareWrite(doc);
    } else if (strcmp(cmd, "keepalive") == 0) {
      // ignore
    } else if (strcmp(cmd, "ping") == 0) {
      cmdPing(doc);
    } else if (strcmp(cmd, "pong") == 0) {
      // ignore
    } else if (strcmp(cmd, "ready") == 0) {
      // ignore
    } else if (strcmp(cmd, "reset") == 0) {
      cmdRestart(doc);
    } else if (strcmp(cmd, "restart") == 0) {
      cmdRestart(doc);
    } else if (strcmp(cmd, "net_metrics_query") == 0) {
      cmdNetMetricsQuery(doc);
    } else if (strcmp(cmd, "system_query") == 0) {
      cmdSystemQuery(doc);
    } else {
      // unknown command, refer to application
      if (receivejson_callback) {
        receivejson_callback(doc);
      }
    }
  } else {
    // no command, refer to application
    if (receivejson_callback) {
      receivejson_callback(doc);
    }
  }
}

void NetThing::cmdFileData(const JsonDocument &obj)
{
  const char *b64 = obj["data"].as<const char*>();
  unsigned int binary_length = decode_base64_length((unsigned char*)b64);
  uint8_t *binary = new uint8_t[binary_length];
  binary_length = decode_base64((unsigned char*)b64, binary);

  DynamicJsonDocument reply(512);

  if (file_writer->add(binary, binary_length, obj["position"])) {
    delete[] binary;
    if (obj["eof"].as<bool>() == 1) {
      if (file_writer->commit()) {
        // finished and successful
        reply[cmd_key] = "file_write_ok";
        reply["filename"] = obj["filename"];
        sendJson(reply);
        sendFileInfo(obj["filename"]);
        if (transfer_status_callback) {
          transfer_status_callback(obj["filename"], 100, false, true);
        }
      } else {
        // finished but commit failed
        reply[cmd_key] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "file_writer->commit() failed";
        file_writer->abort();
        sendJson(reply);
      }
    } else {
      // more data required
      reply[cmd_key] = "file_continue";
      reply["filename"] = obj["filename"];
      reply["position"] = obj["position"].as<int>() + binary_length;
      sendJson(reply, true);
    }
  } else {
    delete[] binary;
    reply[cmd_key] = "file_write_error";
    reply["filename"] = obj["filename"];
    reply["error"] = "file_writer->add() failed";
    file_writer->abort();
    sendJson(reply);
  }
}

void NetThing::cmdFileDelete(const JsonDocument &obj)
{
  StaticJsonDocument<JSON_OBJECT_SIZE(3)> reply;

  if (SPIFFS.remove((const char*)obj["filename"])) {
    reply[cmd_key] = "file_delete_ok";
    reply["filename"] = obj["filename"];
    sendJson(reply);
  } else {
    reply[cmd_key] = "file_delete_error";
    reply["error"] = "failed to delete file";
    reply["filename"] = obj["filename"];
    sendJson(reply);
  }
}

void NetThing::cmdFileDirQuery(const JsonDocument &obj)
{
  DynamicJsonDocument reply(1024);
  JsonArray files = reply.createNestedArray("filenames");
  reply[cmd_key] = "file_dir_info";
  reply["path"] = obj["path"];
  if (SPIFFS.exists((const char*)obj["path"])) {
    Dir dir = SPIFFS.openDir((const char*)obj["path"]);
    while (dir.next()) {
      files.add(dir.fileName());
    }
  } else {
    reply["filenames"] = (char*)NULL;
  }
  sendJson(reply);
}

void NetThing::cmdFileQuery(const JsonDocument &obj)
{
  sendFileInfo(obj["filename"]);
}

void NetThing::cmdFileRename(const JsonDocument &obj)
{
  DynamicJsonDocument reply(512);

  if (SPIFFS.rename((const char*)obj["old_filename"], (const char*)obj["new_filename"])) {
    reply[cmd_key] = "file_rename_ok";
    reply["old_filename"] = obj["old_filename"];
    reply["new_filename"] = obj["new_filename"];
    sendJson(reply);
  } else {
    reply[cmd_key] = "file_rename_error";
    reply["error"] = "failed to rename file";
    reply["old_filename"] = obj["old_filename"];
    reply["new_filename"] = obj["new_filename"];
    sendJson(reply);
  }
}

void NetThing::cmdFileWrite(const JsonDocument &obj)
{
  DynamicJsonDocument reply(512);

  if (file_writer->begin(obj["filename"], obj["md5"], obj["size"])) {
    if (file_writer->upToDate()) {
        reply[cmd_key] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "already up to date";
        sendJson(reply);
    } else {
      if (file_writer->open()) {
        reply[cmd_key] = "file_continue";
        reply["filename"] = obj["filename"];
        reply["position"] = 0;
        sendJson(reply);
      } else {
        reply[cmd_key] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "file_writer->open() failed";
        sendJson(reply);
      }
    }
  } else {
    reply[cmd_key] = "file_write_error";
    reply["filename"] = obj["filename"];
    reply["error"] = "file_writer->begin() failed";
    sendJson(reply);
  }
}

void NetThing::cmdFirmwareData(const JsonDocument &obj)
{
  const char *b64 = obj["data"].as<const char*>();
  unsigned int binary_length = decode_base64_length((unsigned char*)b64);
  uint8_t *binary = new uint8_t[binary_length];
  binary_length = decode_base64((unsigned char*)b64, binary);

  StaticJsonDocument<JSON_OBJECT_SIZE(3) + 64> reply;

  if (firmware_writer->add(binary, binary_length, obj["position"])) {
    delete[] binary;
    if (obj["eof"].as<bool>() == 1) {
      if (firmware_writer->commit()) {
        // finished and successful
        reply[cmd_key] = "firmware_write_ok";
        sendJson(reply);
        if (transfer_status_callback) {
          transfer_status_callback("firmware", 100, false, true);
        }
        restart_firmware = true;
        restart_needed = true;
      } else {
        // finished but commit failed
        reply[cmd_key] = "firmware_write_error";
        reply["error"] = "firmware_writer->commit() failed";
        reply["updater_error"] = firmware_writer->getUpdaterError();
        firmware_writer->abort();
        sendJson(reply);
        if (transfer_status_callback) {
          transfer_status_callback("firmware", 0, false, false);
        }
      }
    } else {
      // more data required
      reply[cmd_key] = "firmware_continue";
      reply["position"] = obj["position"].as<int>() + binary_length;
      sendJson(reply, true);
      if (transfer_status_callback) {
        transfer_status_callback("firmware", firmware_writer->progress(), true, false);
      }
    }
  } else {
    delete[] binary;
    reply[cmd_key] = "firmware_write_error";
    reply["error"] = "firmware_writer->add() failed";
    reply["updater_error"] = firmware_writer->getUpdaterError();
    firmware_writer->abort();
    sendJson(reply);
    if (transfer_status_callback) {
      transfer_status_callback("firmware", 0, false, false);
    }
  }
}

void NetThing::cmdFirmwareWrite(const JsonDocument &obj)
{
  StaticJsonDocument<JSON_OBJECT_SIZE(4) + 64> reply;

  if (firmware_writer->begin(obj["md5"], obj["size"])) {
    if (firmware_writer->upToDate()) {
      reply[cmd_key] = "firmware_write_error";
      reply["md5"] = obj["md5"];
      reply["error"] = "already up to date";
      reply["updater_error"] = firmware_writer->getUpdaterError();
      sendJson(reply);
    } else {
      if (firmware_writer->open()) {
        reply[cmd_key] = "firmware_continue";
        reply["md5"] = obj["md5"];
        reply["position"] = 0;
        sendJson(reply);
      } else {
        reply[cmd_key] = "firmware_write_error";
        reply["md5"] = obj["md5"];
        reply["error"] = "firmware_writer->open() failed";
        reply["updater_error"] = firmware_writer->getUpdaterError();
        sendJson(reply);
      }
    }
  } else {
    reply[cmd_key] = "firmware_write_error";
    reply["md5"] = obj["md5"];
    reply["error"] = "firmware_writer->begin() failed";
    reply["updater_error"] = firmware_writer->getUpdaterError();
    sendJson(reply);
  }
}

void NetThing::cmdNetMetricsQuery(const JsonDocument &doc) {
  DynamicJsonDocument reply(1024);
  reply[cmd_key] = "net_metrics_info";
  reply["esp_free_cont_stack"] = ESP.getFreeContStack();
  reply["esp_free_heap"] = ESP.getFreeHeap();
  reply["esp_heap_fragmentation"] = ESP.getHeapFragmentation();
  reply["esp_max_free_block_size"] = ESP.getMaxFreeBlockSize();
  reply["millis"] = millis();
  reply["net_rx_buf_max"] = jsonstream.rx_buffer_high_watermark;
  reply["net_tcp_double_connect_errors"] = jsonstream.tcp_double_connect_errors;
  reply["net_tcp_reconns"] = jsonstream.tcp_connects;
  reply["net_tcp_fingerprint_errors"] = jsonstream.tcp_fingerprint_errors;
  reply["net_tcp_async_errors"] = jsonstream.tcp_async_errors;
  reply["net_tcp_sync_errors"] = jsonstream.tcp_sync_errors;
  reply["net_tx_buf_max"] = jsonstream.tx_buffer_high_watermark;
  reply["net_tx_delay_count"] = jsonstream.tx_delay_count;
  reply["net_json_parse_errors"] = jsonstream.json_parse_errors;
  reply["net_json_parse_ok"] = jsonstream.json_parse_ok;
  reply["net_json_parse_max_usage"] = jsonstream.json_parse_max_usage;
  reply["net_wifi_reconns"] = wifi_reconnections;
  reply["net_wifi_rssi"] = WiFi.RSSI();
  reply.shrinkToFit();
  sendJson(reply);
}

void NetThing::cmdPing(const JsonDocument &doc) {
  StaticJsonDocument<JSON_OBJECT_SIZE(3) + 64> reply;
  reply[cmd_key] = "pong";
  if (doc["seq"]) {
    reply["seq"] = doc["seq"];
  }
  if (doc["timestamp"]) {
    reply["timestamp"] = doc["timestamp"];
  }
  sendJson(reply);
}

void NetThing::cmdRestart(const JsonDocument &doc) {
  if (doc["force"]) {
    if (restart_callback) {
      restart_callback(true, restart_firmware);
    }
    // if the main application didn't restart, we will...
    ESP.restart();
    delay(5000);
  } else {
    if (restart_callback) {
      // main application may restart if convenient
      restart_callback(false, restart_firmware);
    } else {
      // no handler available, perform our own restart immediately
      ESP.restart();
      delay(5000);
    }
  }
}

void NetThing::cmdSystemQuery(const JsonDocument &doc) {
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  DynamicJsonDocument reply(1024);
  reply[cmd_key] = "system_info";
  reply["esp_free_heap"] = ESP.getFreeHeap();
  reply["esp_chip_id"] = ESP.getChipId();
  reply["esp_sdk_version"] = ESP.getSdkVersion();
  reply["esp_core_version"] = ESP.getCoreVersion();
  reply["esp_boot_version"] = ESP.getBootVersion();
  reply["esp_boot_mode"] = ESP.getBootMode();
  reply["esp_cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  reply["esp_flash_chip_id"] = ESP.getFlashChipId();
  reply["esp_flash_chip_real_size"] = ESP.getFlashChipRealSize();
  reply["esp_flash_chip_size"] = ESP.getFlashChipSize();
  reply["esp_flash_chip_speed"] = ESP.getFlashChipSpeed();
  reply["esp_flash_chip_mode"] = ESP.getFlashChipMode();
  reply["esp_flash_chip_size_by_chip_id"] = ESP.getFlashChipSizeByChipId();
  reply["esp_sketch_size"] = ESP.getSketchSize();
  reply["esp_sketch_md5"] = ESP.getSketchMD5();
  reply["esp_free_sketch_space"] = ESP.getFreeSketchSpace();
  reply["esp_reset_reason"] = ESP.getResetReason();
  reply["esp_reset_info"] = ESP.getResetInfo();
  reply["esp_cycle_count"] = ESP.getCycleCount();
  reply["fs_total_bytes"] = fs_info.totalBytes;
  reply["fs_used_bytes"] = fs_info.usedBytes;
  reply["fs_block_size"] = fs_info.blockSize;
  reply["fs_page_size"] = fs_info.pageSize;
  reply["millis"] = millis();
  reply.shrinkToFit();
  sendJson(reply);
}
