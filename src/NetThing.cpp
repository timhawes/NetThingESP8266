#include "NetThing.hpp"
#include "base64.hpp"

using namespace std::placeholders;

const char* reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN: return "ESP_RST_UNKNOWN";
    case ESP_RST_POWERON: return "ESP_RST_POWERON";
    case ESP_RST_EXT: return "ESP_RST_EXT";
    case ESP_RST_SW: return "ESP_RST_SW";
    case ESP_RST_PANIC: return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT: return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT: return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT: return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP: return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO: return "ESP_RST_SDIO";
    default: return "";
  }
}

NetThing::NetThing(int rx_buffer_len, int tx_buffer_len, int rx_queue_len, int tx_queue_len)
{
  uint8_t m[6] = {};
  esp_efuse_mac_get_default(m);
  snprintf(mac_address, sizeof(mac_address), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
  server_username = mac_address;

  setSyncInterval(3600);

  WiFi.onEvent(std::bind(&NetThing::wifiConnectHandler, this), WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(std::bind(&NetThing::wifiDisconnectHandler, this), WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  ps = new PacketStream(rx_buffer_len, tx_buffer_len, rx_queue_len, tx_queue_len);
  ps->onConnect(std::bind(&NetThing::psConnectHandler, this));
  ps->onDisconnect(std::bind(&NetThing::psDisconnectHandler, this));
  ps->onReceivePacket(std::bind(&NetThing::psReceiveHandler, this, _1, _2));

  file_writer = new FileWriter;
  firmware_writer = new FirmwareWriter;
}

String NetThing::canonifyFilename(String filename) {
  if (filename[0] == '/') {
    // absolute filename, no change
    return filename;
  } else if (filename_prefix) {
    // rewrite
    return filename_prefix + filename;
  } else {
    // no prefix configured
    return filename;
  }
}

void NetThing::psConnectHandler() {
  StaticJsonDocument<JSON_OBJECT_SIZE(7) + 128> doc;
  doc[cmd_key] = "hello";
  doc["clientid"] = server_username;
  doc["username"] = server_username;
  doc["password"] = server_password;
  doc["esp_efuse_mac"] = mac_address;
  doc["esp_sketch_md5"] = ESP.getSketchMD5();
#ifdef ARDUINO_VARIANT
  doc["arduino_variant"] = ARDUINO_VARIANT;
#endif
  sendJson(doc);
  if (connect_callback) {
    connect_callback();
  }
}

void NetThing::psDisconnectHandler() {
  file_writer->abort();
  firmware_writer->abort();
  if (disconnect_callback) {
    disconnect_callback();
  }
}

void NetThing::loop() {
  loopwatchdog.feed();

  ps->loop();

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

  if (receive_watchdog_timeout > 0) {
    if ((long)(millis() - last_packet_received) > receive_watchdog_timeout) {
      Serial.println("NetThing: receive watchdog triggered, restarting");
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

void NetThing::allowFileSync(bool allow) {
  allow_file_sync = allow;
}

void NetThing::allowFirmwareSync(bool allow) {
  allow_firmware_sync = allow;
}

bool NetThing::sendJson(const JsonDocument &doc, bool now) {
  int packet_len = measureJson(doc);
  char *packet = new char[packet_len+1];
  serializeJson(doc, packet, packet_len+1);

  if (debug_json) {
    Serial.printf("NetThing: send usage=%d/%d len=%d json=", doc.memoryUsage(), doc.capacity(), packet_len);
    Serial.println(packet);
  }

  bool result = ps->send((uint8_t*)packet, packet_len);
  delete[] packet;
  return result;
}

void NetThing::setCommandKey(const char *key) {
  cmd_key = key;
}

void NetThing::setCred(const char *username, const char *password) {
  server_username = username;
  server_password = password;
}

void NetThing::setCred(const char *password) {
  server_username = mac_address;
  server_password = password;
}

void NetThing::setDebug(bool enabled) {
  ps->setDebug(enabled);
  debug_json = enabled;
}

void NetThing::setKeepalive(unsigned long ms) {
  ps->setKeepalive(ms);
}

void NetThing::setConnectionStableTime(unsigned long ms) {
  ps->setConnectionStableTime(ms);
}

void NetThing::setFilenamePrefix(const char *prefix) {
  filename_prefix = prefix;
}

void NetThing::setReconnectMaxTime(unsigned long ms) {
  ps->setReconnectMaxTime(ms);
}

void NetThing::setServer(const char *host, int port,
                         bool tls, bool verify,
                         const char *fingerprint1,
                         const char *fingerprint2) {
  ps->setServer(host, port, verify, fingerprint1, fingerprint2);
}

void NetThing::setReceiveWatchdog(unsigned long timeout) {
  receive_watchdog_timeout = timeout;
}

void NetThing::setLoopWatchdog(unsigned long timeout) {
  loopwatchdog.setTimeout(timeout);
}

void NetThing::setWiFi(const char *ssid, const char *password) {
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.enableAP(false);
  WiFi.enableSTA(true);
  WiFi.begin(ssid, password);
}

void NetThing::start() {
  ps->start();
}

void NetThing::stop() {
  ps->stop();
  loopwatchdog.stop();
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
  StaticJsonDocument<JSON_OBJECT_SIZE(5) + 64> obj;
  String path = canonifyFilename(filename);
  obj[cmd_key] = "file_info";
  obj["filename"] = filename;
  obj["local_filename"] = path;

  File f = SPIFFS.open(path, "r");
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
      if (allow_file_sync) cmdFileData(doc);
    } else if (strcmp(cmd, "file_delete") == 0) {
      if (allow_file_sync) cmdFileDelete(doc);
    } else if (strcmp(cmd, "file_dir_query") == 0) {
      if (allow_file_sync) cmdFileDirQuery(doc);
    } else if (strcmp(cmd, "file_query") == 0) {
      if (allow_file_sync) cmdFileQuery(doc);
    } else if (strcmp(cmd, "file_rename") == 0) {
      if (allow_file_sync) cmdFileRename(doc);
    } else if (strcmp(cmd, "file_write") == 0) {
      if (allow_file_sync) cmdFileWrite(doc);
    } else if (strcmp(cmd, "firmware_data") == 0) {
      if (allow_firmware_sync) cmdFirmwareData(doc);
    } else if (strcmp(cmd, "firmware_write") == 0) {
      if (allow_firmware_sync) cmdFirmwareWrite(doc);
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
    } else if (strcmp(cmd, "time") == 0) {
      cmdTime(doc);
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
          String path = canonifyFilename(obj["filename"]);
          transfer_status_callback(path.c_str(), 100, false, true);
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

  String path = canonifyFilename(obj["filename"]);

  if (SPIFFS.remove(path)) {
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
  JsonArray dirs = reply.createNestedArray("dirs");
  JsonArray files = reply.createNestedArray("files");
  reply[cmd_key] = "file_dir_info";
  reply["path"] = obj["path"];
  File dir = SPIFFS.open((const char*)obj["path"]);
  File file;
  while (file = dir.openNextFile()) {
    if (file.isDirectory()) {
      dirs.add((char*)(file.path()));
    } else {
      files.add((char*)(file.path()));
    }
  }
  reply.shrinkToFit();
  sendJson(reply);
}

void NetThing::cmdFileQuery(const JsonDocument &obj)
{
  sendFileInfo(obj["filename"]);
}

void NetThing::cmdFileRename(const JsonDocument &obj)
{
  DynamicJsonDocument reply(512);

  String old_path = canonifyFilename(obj["old_filename"]);
  String new_path = canonifyFilename(obj["new_filename"]);

  if (SPIFFS.rename(old_path, new_path)) {
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

  String path = canonifyFilename(obj["filename"]);

  if (file_writer->begin(path.c_str(), obj["md5"], obj["size"])) {
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
  reply["esp_free_heap"] = ESP.getFreeHeap();
  reply["esp_free_psram"] = ESP.getFreePsram();
  reply["esp_max_alloc_heap"] = ESP.getMaxAllocHeap();
  reply["esp_max_alloc_psram"] = ESP.getMaxAllocPsram();
  reply["esp_min_free_heap"] = ESP.getMinFreeHeap();
  reply["esp_min_free_psram"] = ESP.getMinFreePsram();
  reply["millis"] = millis();
  reply["net_json_parse_errors"] = json_parse_errors;
  reply["net_json_parse_max_usage"] = json_parse_max_usage;
  reply["net_json_parse_ok"] = json_parse_ok;
  reply["net_wifi_reconns"] = wifi_reconnections;
  reply["net_wifi_rssi"] = WiFi.RSSI();
  reply["rx_buffer_full"] = ps->rx_buffer_full;
  reply["rx_buffer_max"] = ps->rx_buffer_max;
  reply["rx_queue_full"] = ps->rx_queue_full;
  reply["rx_queue_in"] = ps->rx_queue_in;
  reply["rx_queue_max"] = ps->rx_queue_max;
  reply["rx_queue_out"] = ps->rx_queue_out;
  reply["tcp_bytes_received"] = ps->tcp_bytes_received;
  reply["tcp_bytes_sent"] = ps->tcp_bytes_sent;
  reply["tcp_conn_errors"] = ps->tcp_conn_errors;
  reply["tcp_conn_fingerprint_errors"] = ps->tcp_conn_fingerprint_errors;
  reply["tcp_conn_ok"] = ps->tcp_conn_ok;
  reply["tx_buffer_full"] = ps->tx_buffer_full;
  reply["tx_buffer_max"] = ps->tx_buffer_max;
  reply["tx_queue_full"] = ps->tx_queue_full;
  reply["tx_queue_in"] = ps->tx_queue_in;
  reply["tx_queue_max"] = ps->tx_queue_max;
  reply["tx_queue_out"] = ps->tx_queue_out;
  if (timeStatus() != timeNotSet) {
    reply["boot_time"] = boot_time;
    reply["time"] = now();
    reply["uptime"] = now() - boot_time;
  }
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
  DynamicJsonDocument reply(1024);
  reply[cmd_key] = "system_info";
#ifdef ARDUINO_VARIANT
  reply["arduino_variant"] = ARDUINO_VARIANT;
#endif
  reply["esp_chip_cores"] = ESP.getChipCores();
  reply["esp_chip_model"] = ESP.getChipModel();
  reply["esp_chip_revision"] = ESP.getChipRevision();
  reply["esp_cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  reply["esp_core_version"] = ESP_ARDUINO_VERSION_STR;
  reply["esp_cycle_count"] = ESP.getCycleCount();
  reply["esp_efuse_mac"] = mac_address;
  reply["esp_flash_chip_mode"] = ESP.getFlashChipMode();
  reply["esp_flash_chip_size"] = ESP.getFlashChipSize();
  reply["esp_flash_chip_speed"] = ESP.getFlashChipSpeed();
  reply["esp_free_sketch_space"] = ESP.getFreeSketchSpace();
  reply["esp_heap_size"] = ESP.getHeapSize();
  reply["esp_psram_size"] = ESP.getPsramSize();
  reply["esp_reset_reason"] = reset_reason_name(esp_reset_reason());
  reply["esp_sdk_version"] = ESP.getSdkVersion();
  reply["esp_sketch_md5"] = ESP.getSketchMD5();
  reply["esp_sketch_size"] = ESP.getSketchSize();
  reply["fs_total_bytes"] = SPIFFS.totalBytes();
  reply["fs_used_bytes"] = SPIFFS.usedBytes();
  reply["millis"] = millis();
  if (timeStatus() != timeNotSet) {
    reply["boot_time"] = boot_time;
    reply["time"] = now();
    reply["uptime"] = now() - boot_time;
  }
  if (loopwatchdog.restarted()) {
    reply["net_reset_info"] = "loop() watchdog timeout";
  }
  if (restarted) {
    reply["restarted"] = true;
    restarted = false;
  }
  reply.shrinkToFit();
  sendJson(reply);
}

void NetThing::cmdTime(const JsonDocument &doc) {
  if (doc["time"] > 0) {
    setTime(doc["time"]);
    if (boot_time == 0) {
      boot_time = now() - (millis() / 1000);
    }
  }
}

void NetThing::begin() {
  ps->begin();
}

void NetThing::psReceiveHandler(uint8_t* packet, size_t packet_len) {
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, packet, packet_len);

  if (err) {
    json_parse_errors++;
    Serial.printf("NetThing: receive len=%d usage=-/%d error=%s packet=", packet_len, doc.capacity(), err.c_str());
    for (unsigned int i=0; i<packet_len; i++) {
      Serial.printf("%02x", packet[i]);
    }
    Serial.println();
    return;
  }

  json_parse_ok++;
  if (doc.memoryUsage() > json_parse_max_usage) {
    json_parse_max_usage = doc.memoryUsage();
  }

  if (debug_json) {
    Serial.printf("NetThing: recv len=%d usage=%d/%d json=", packet_len, doc.memoryUsage(), doc.capacity());
    serializeJson(doc, Serial);
    Serial.println();
  }

  doc.shrinkToFit();

  jsonReceiveHandler(doc);
}

void NetThing::sendEvent(const char* event, const char* message) {
  StaticJsonDocument<JSON_OBJECT_SIZE(5)> obj;
  obj["cmd"] = "event";
  obj["millis"] = millis();
  if (timeStatus() != timeNotSet) {
    obj["time"] = now();
  }
  obj["event"] = event;
  if (message) {
    obj["message"] = message;
  }
  sendJson(obj);
}

void NetThing::sendEvent(const char* event, size_t size, const char* format, ...) {
  char message[size];
  va_list args;
  va_start(args, format);
  vsnprintf(message, size, format, args);
  va_end(args);
  sendEvent(event, message);
}
