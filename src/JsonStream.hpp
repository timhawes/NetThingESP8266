#ifndef JSONSTREAM_HPP
#define JSONSTREAM_HPP

#include "PacketStream.hpp"
#include "ArduinoJson.h"

typedef std::function<void(const JsonDocument &doc)> JsonStreamReceiveJsonHandler;

class JsonStream : public PacketStream {
 private:
  JsonStreamReceiveJsonHandler receivejson_callback;
  // configuration
  bool debug_json = false;
  // private methods
  void receivePacket(const uint8_t *data, int len);
 public:
  JsonStream(int rx_buffer_len, int tx_buffer_len);
  // metrics
  unsigned long json_parse_errors = 0;
  unsigned long json_parse_ok = 0;
  unsigned int json_parse_max_usage = 0;
  // public methods
  void onReceiveJson(JsonStreamReceiveJsonHandler callback);
  bool sendJson(const JsonDocument &doc, bool now=false);
  void setDebug(bool enabled);
};

#endif
