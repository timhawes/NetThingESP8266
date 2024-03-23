#ifndef NETTHING_HPP
#define NETTHING_HPP

#include <functional>
#include "ArduinoJson.h"
#include "ESP8266WiFi.h"
#include "FileWriter.hpp"
#include "FirmwareWriter.hpp"
#include "PacketStream.hpp"
#include "Restarter.hpp"
#include "Ticker.h"
#include "TimeLib.h"

#define NETTHING_RESTART_ENDUSER 0x0100
#define NETTHING_RESTART_REMOTE 0x0101
#define NETTHING_RESTART_REMOTE_IMMEDIATE 0x0102
#define NETTHING_RESTART_FIRMWARE 0x0103
#define NETTHING_RESTART_CONFIG_CHANGE 0x0104
#define NETTHING_RESTART_RECEIVE_WATCHDOG 0x0105
#define NETTHING_RESTART_LOOP_WATCHDOG 0x0106

typedef std::function<void()> NetThingConnectHandler;
typedef std::function<void()> NetThingDisconnectHandler;
typedef std::function<void(bool immediate, bool firmware)> NetThingRestartRequestHandler;
typedef std::function<void(bool immediate, bool firmware, uint16_t reason)> NetThingRestartReasonRequestHandler;
typedef std::function<void(const JsonDocument &doc)> NetThingReceivePacketHandler;
typedef std::function<void(const char *filename, int progress, bool active, bool changed)> NetThingTransferStatusHandler;

class NetThing {
 private:
  NetThingConnectHandler connect_callback;
  NetThingDisconnectHandler disconnect_callback;
  NetThingRestartRequestHandler restart_callback;
  NetThingRestartReasonRequestHandler restart_reason_callback;
  NetThingReceivePacketHandler receivejson_callback;
  NetThingTransferStatusHandler transfer_status_callback;
  PacketStream *ps;
  FirmwareWriter *firmware_writer;
  FileWriter *file_writer;
  WiFiEventHandler wifiEventConnectHandler;
  WiFiEventHandler wifiEventDisconnectHandler;
  Ticker loop_watchdog_ticker;
  Restarter restarter;
  // configuration
  char chip_id[7];
  const char *cmd_key = "cmd";
  const char *server_username;
  const char *server_password;
  const char *filename_prefix;
  unsigned long receive_watchdog_timeout = 0; // restart if no packet received for this ms period
  unsigned long loop_watchdog_timeout = 60000; // restart if loop() not called for this ms period
  unsigned long wifi_check_interval = 0; // check wifi every X millis and force a reconnect if required
  bool debug_json = false;
  bool allow_firmware_sync = true;
  bool allow_file_sync = true;
  // state
  bool enabled = false;
  unsigned long last_packet_received = 0;
  unsigned long last_wifi_check = 0;
  unsigned long last_loop = 0;
  bool loop_watchdog_started = false; // set to true on the first call to loop()
  bool restarted = true; // the system has been restarted, will be set to false when it has been logged
  bool restart_firmware = false; // a graceful restart is needed for firmware upgrades and should show an appropriate message
  time_t boot_time = 0;
  // metrics
  unsigned int json_parse_max_usage = 0;
  unsigned long json_parse_errors = 0;
  unsigned long json_parse_ok = 0;
  unsigned long wifi_reconnections = 0;
  unsigned long wifi_check_errors = 0;
  // private methods
  String canonifyFilename(String filename);
  void psConnectHandler();
  void psDisconnectHandler();
  void psReceiveHandler(uint8_t* packet, size_t packet_len);
  void jsonReceiveHandler(const JsonDocument &doc);
  void loopTimeoutHandler();
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
  NetThing(int rx_buffer_len=1500, int tx_buffer_len=1500);
  void loop();
  void onConnect(NetThingConnectHandler callback);
  void onDisconnect(NetThingDisconnectHandler callback);
  void onReceiveJson(NetThingReceivePacketHandler callback);
  [[deprecated]]
  void onRestartRequest(NetThingRestartRequestHandler callback);
  void onRestartRequest(NetThingRestartReasonRequestHandler callback);
  void onTransferStatus(NetThingTransferStatusHandler callback);
  void reconnect();
  void allowFileSync(bool allow);
  void allowFirmwareSync(bool allow);
  uint16_t getRestartReason();
  void restartWithReason(uint16_t reason);
  bool sendJson(const JsonDocument &doc, bool now=false);
  void setCred(const char *username, const char *password);
  void setCred(const char *password);
  void setCommandKey(const char *key);
  void setDebug(bool enabled);
  void setReconnectMaxTime(unsigned long ms);
  void setConnectionStableTime(unsigned long ms);
  void setFilenamePrefix(const char *prefix);
  void setServer(const char *host, int port,
                 bool secure=false, bool verify=false,
                 const uint8_t *fingerprint1=NULL,
                 const uint8_t *fingerprint2=NULL);
  void setReceiveWatchdog(unsigned long timeout);
  void setLoopWatchdog(unsigned long timeout);
  void setWiFi(const char *ssid, const char *password);
  void setWifiCheckInterval(unsigned long interval);
  void start();
  void stop();
  void sendEvent(const char* event, const char* message=NULL);
  void sendEvent(const char* event, size_t size, const char* format, ...);
};

#endif
