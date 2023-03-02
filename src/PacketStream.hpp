#ifndef PACKETSTREAM_HPP
#define PACKETSTREAM_HPP

#include "Arduino.h"
#include "cbuf.h"
#include <ESPAsyncTCP.h>
#include <functional>

typedef std::function<void()> PacketStreamConnectHandler;
typedef std::function<void()> PacketStreamDisconnectHandler;
typedef std::function<void(const char *data, int len)> PacketStreamReceivePacketHandler;

class PacketStream {
 private:
  AsyncClient client;
  cbuf rx_buffer;
  cbuf tx_buffer;
  PacketStreamConnectHandler connect_callback;
  PacketStreamDisconnectHandler disconnect_callback;
  PacketStreamReceivePacketHandler receivepacket_callback;
  // configuration
  bool debug_packet = false;
  const char *server_host;
  int server_port;
  bool server_secure;
  bool server_verify;
  const uint8_t *server_fingerprint1;
  const uint8_t *server_fingerprint2;
  unsigned int reconnect_interval = 1000;
  bool fast_receive = false;
  bool fast_send = false;
  // state
  bool enabled = false;
  bool connect_scheduled = false;
  unsigned long connect_scheduled_time = 0;
  bool in_rx_handler = false;
  bool tcp_active = false;
  // private methods
  void connect();
  size_t processTxBuffer();
  size_t processRxBuffer();
  void scheduleConnect();
 public:
  PacketStream(int rx_buffer_len, int tx_buffer_len);
  // metrics
  unsigned int tcp_connects = 0;
  unsigned int tcp_double_connect_errors = 0;
  unsigned int tcp_async_errors = 0;
  unsigned int tcp_sync_errors = 0;
  unsigned int tcp_fingerprint_errors = 0;
  unsigned int rx_buffer_high_watermark = 0;
  unsigned int tx_buffer_high_watermark = 0;
  unsigned int tx_delay_count = 0;
  unsigned long packet_queue_error = 0;
  unsigned long packet_queue_full = 0;
  unsigned long packet_queue_ok = 0;
  // public methods
  void setServer(const char *host, int port,
                 bool secure=false, bool verify=false,
                 const uint8_t *fingerprint1=NULL,
                 const uint8_t *fingerprint2=NULL);
  void onConnect(PacketStreamConnectHandler callback);
  void onDisconnect(PacketStreamDisconnectHandler callback);
  void onReceivePacket(PacketStreamReceivePacketHandler callback);
  void start();
  void stop();
  void reconnect();
  bool sendPacket(const char* data, size_t size);
  void loop();
};

#endif