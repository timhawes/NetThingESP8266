#include "JsonStream.hpp"

using namespace std::placeholders;

JsonStream::JsonStream(int rx_buffer_len, int tx_buffer_len) : PacketStream(rx_buffer_len, tx_buffer_len) {
  onReceivePacket(std::bind(&JsonStream::receivePacket, this, _1, _2));
}

void JsonStream::receivePacket(const uint8_t *packet, int packet_len) {
  size_t doc_size = packet_len * 2;
  if (doc_size > 2048) {
    doc_size = 2048;
  }

  DynamicJsonDocument doc(doc_size);
  DeserializationError err = deserializeJson(doc, packet);

  if (err) {
    json_parse_errors++;
    Serial.printf("JsonStream: receive len=%d usage=-/%d error=%s packet=", packet_len, doc.capacity(), err.c_str());
    for (int i=0; i<packet_len; i++) {
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
    Serial.printf("JsonStream: recv len=%d usage=%d/%d json=", packet_len, doc.memoryUsage(), doc.capacity());
    Serial.write(packet, packet_len);
    Serial.println();
  }

  doc.shrinkToFit();

  if (receivejson_callback) {
    receivejson_callback(doc);
  }
}

void JsonStream::onReceiveJson(JsonStreamReceiveJsonHandler callback) {
  receivejson_callback = callback;
}

bool JsonStream::sendJson(const JsonDocument &doc, bool now) {
  int packet_len = measureJson(doc);
  char *packet = new char[packet_len+1];
  serializeJson(doc, packet, packet_len+1);

  if (debug_json) {
    Serial.printf("JsonStream: send usage=%d/%d len=%d json=", doc.memoryUsage(), doc.capacity(), packet_len);
    Serial.println(packet);
  }
  
  bool result = sendPacket((uint8_t*)packet, packet_len);
  delete[] packet;
  return result;
}

void JsonStream::setDebug(bool enabled) {
  debug_json = enabled;
}
