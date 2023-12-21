#ifndef NETTHING_HPP
#define NETTHING_HPP

#include <functional>
#include "ArduinoJson.h"
#include "WiFi.h"
#include "FileWriter.hpp"
#include "FirmwareWriter.hpp"
#include "FS.h"
#include "SPIFFS.h"
#include "PacketStream.hpp"
#include "TimeLib.h"
#include "LoopWatchdog.hpp"

#define df2xstr(s) #s
#define df2str(s) df2xstr(s)
#define ESP_ARDUINO_VERSION_STR df2str(ESP_ARDUINO_VERSION_MAJOR) "." df2str(ESP_ARDUINO_VERSION_MINOR) "." df2str(ESP_ARDUINO_VERSION_PATCH)

typedef std::function<void()> NetThingConnectHandler;
typedef std::function<void()> NetThingDisconnectHandler;
typedef std::function<void(bool immediate, bool firmware)> NetThingRestartRequestHandler;
typedef std::function<void(const JsonDocument &doc)> NetThingReceivePacketHandler;
typedef std::function<void(const char *filename, int progress, bool active, bool changed)> NetThingTransferStatusHandler;

class NetThing {
 private:
  NetThingConnectHandler connect_callback;
  NetThingDisconnectHandler disconnect_callback;
  NetThingRestartRequestHandler restart_callback;
  NetThingReceivePacketHandler receivejson_callback;
  NetThingTransferStatusHandler transfer_status_callback;
  PacketStream *ps;
  FirmwareWriter *firmware_writer;
  FileWriter *file_writer;
  LoopWatchdog loopwatchdog;
  // configuration
  char mac_address[13] = "";
  const char *cmd_key = "cmd";
  const char *server_username;
  const char *server_password;
  const char *filename_prefix = "/";
  unsigned long receive_watchdog_timeout = 0; // restart if no packet received for this ms period
  unsigned long loop_watchdog_timeout = 60000; // restart if loop() not called for this ms period
  bool debug_json = false;
  bool allow_firmware_sync = true;
  bool allow_file_sync = true;
  // state
  bool enabled = false;
  unsigned long last_packet_received = 0;
  bool restarted = true; // the system has been restarted, will be set to false when it has been logged
  bool restart_needed = false; // a graceful restart is needed
  bool restart_firmware = false; // the restart is for firmware upgrades and should show an appropriate message
  time_t boot_time = 0;
  // metrics
  unsigned int json_parse_max_usage = 0;
  unsigned long json_parse_errors = 0;
  unsigned long json_parse_ok = 0;
  unsigned long wifi_reconnections = 0;
  // private methods
  String canonifyFilename(String filename);
  void psConnectHandler();
  void psDisconnectHandler();
  void psReceiveHandler(uint8_t* packet, size_t packet_len);
  void jsonReceiveHandler(const JsonDocument &doc);
  void wifiConnectHandler();
  void wifiDisconnectHandler();
  // network commands
  void cmdFileData(const JsonDocument &doc);
  void cmdFileDelete(const JsonDocument &doc);
  void cmdFileDirQuery(const JsonDocument &doc);
  void cmdFileQuery(const JsonDocument &doc);
  void cmdFileRename(const JsonDocument &doc);
  void cmdFileWrite(const JsonDocument &doc);
  void cmdFirmwareData(const JsonDocument &doc);
  void cmdFirmwareWrite(const JsonDocument &doc);
  void cmdNetMetricsQuery(const JsonDocument &doc);
  void cmdPing(const JsonDocument &doc);
  void cmdReset(const JsonDocument &doc);
  void cmdRestart(const JsonDocument &doc);
  void cmdSystemQuery(const JsonDocument &doc);
  void cmdTime(const JsonDocument &doc);
  void sendFileInfo(const char *filename);
 public:
  NetThing(int rx_buffer_len=1500, int tx_buffer_len=1500, int rx_queue_len=20, int tx_queue_len=20);
  void loop();
  void onConnect(NetThingConnectHandler callback);
  void onDisconnect(NetThingDisconnectHandler callback);
  void onReceiveJson(NetThingReceivePacketHandler callback);
  void onRestartRequest(NetThingRestartRequestHandler callback);
  void onTransferStatus(NetThingTransferStatusHandler callback);
  void reconnect();
  void allowFileSync(bool allow);
  void allowFirmwareSync(bool allow);
  bool sendJson(const JsonDocument &doc, bool now=false);
  void setCred(const char *username, const char *password);
  void setCred(const char *password);
  void setCommandKey(const char *key);
  void setConnectionStableTime(unsigned long ms);
  void setDebug(bool enabled);
  void setIdleThreshold(unsigned long ms);
  void setKeepalive(unsigned long ms);
  void setReconnectMaxTime(unsigned long ms);
  void setFilenamePrefix(const char *prefix);
  void setServer(const char *host, int port,
                 bool tls=true, bool verify=false,
                 const char *sha256_fingerprint1=NULL,
                 const char *sha256_fingerprint2=NULL);
  void setReceiveWatchdog(unsigned long timeout);
  void setLoopWatchdog(unsigned long timeout);
  void setWiFi(const char *ssid, const char *password);
  void start();
  void stop();
  void sendEvent(const char* event, const char* message=NULL);
  void sendEvent(const char* event, size_t size, const char* format, ...);
  void begin();
  bool idle();
};

#endif
