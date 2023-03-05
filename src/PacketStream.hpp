#ifndef PACKETSTREAM_HPP
#define PACKETSTREAM_HPP

#include "Arduino.h"
#include "cbuf.h"
#include "WiFiClientSecure.h"
#include <functional>

typedef std::function<void()> PacketStreamConnectHandler;
typedef std::function<void()> PacketStreamDisconnectHandler;
typedef std::function<void(uint8_t *data, size_t len)> PacketStreamReceivePacketHandler;

typedef struct
{
  size_t len;
  uint8_t *data;
} asyncsession_packet_t;

class PacketStream {

 private:
  WiFiClientSecure client;
  QueueHandle_t rx_queue;
  QueueHandle_t tx_queue;
  cbuf rx_buffer;
  cbuf tx_buffer;
  PacketStreamConnectHandler connect_callback;
  PacketStreamDisconnectHandler disconnect_callback;
  PacketStreamReceivePacketHandler receivepacket_callback;
  // configuration
  bool debug = false;
  bool server_verify;
  const char *server_fingerprint1;
  const char *server_fingerprint2;
  const char *server_host;
  int server_port;
  unsigned int reconnect_interval = 1000;
  // state
  bool connect_state = false;
  bool enabled = true;
  bool pending_connect_callback = true;
  bool pending_disconnect_callback = false;
  unsigned long last_connect_attempt = 0;
  // private methods
  static void taskWrapper(void * pvParameters);
  void connect();
  void task();
 public:
  PacketStream(int rx_buffer_len, int tx_buffer_len);
  ~PacketStream();
  // metrics
  unsigned int rx_buffer_full_errors = 0;
  unsigned int rx_buffer_high_watermark = 0;
  unsigned int rx_queue_full_errors = 0;
  unsigned int rx_queue_high_watermark = 0;
  unsigned int tcp_connects = 0;
  unsigned int tcp_fingerprint_errors = 0;
  unsigned int tcp_sync_errors = 0;
  unsigned int tx_buffer_full_errors = 0;
  unsigned int tx_buffer_high_watermark = 0;
  unsigned int tx_queue_full_errors = 0;
  unsigned int tx_queue_high_watermark = 0;
  unsigned long rx_queue_count = 0;
  unsigned long tcp_bytes_received = 0;
  unsigned long tcp_bytes_sent = 0;
  unsigned long tx_queue_count = 0;
  // public methods
  void setDebug(bool enable);
  void setServer(const char *host, int port,
                 bool verify=false,
                 const char *fingerprint1=NULL,
                 const char *fingerprint2=NULL);
  void onConnect(PacketStreamConnectHandler callback);
  void onDisconnect(PacketStreamDisconnectHandler callback);
  void onReceivePacket(PacketStreamReceivePacketHandler callback);
  void start();
  void stop();
  void reconnect();
  bool send(const uint8_t* data, size_t len);
  size_t receive(uint8_t* data, size_t len);
  void loop();
  void begin();
};

#endif
