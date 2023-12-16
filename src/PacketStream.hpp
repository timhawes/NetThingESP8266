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
  unsigned int keepalive_interval = 0;
  unsigned long connection_stable_time = 30000; // connection considered stable after this time
  unsigned long reconnect_interval = 500; // current reconnect interval
  unsigned long reconnect_interval_backoff_factor = 2;
  unsigned long reconnect_interval_max = 180000;
  unsigned long reconnect_interval_min = 500;
  // state
  bool connect_state = false;
  bool connection_stable = false;
  bool enabled = false;
  bool pending_connect_callback = true;
  bool pending_disconnect_callback = false;
  unsigned long last_connect_time = 0;
  unsigned long last_send = 0; // used for sending keepalives
  unsigned long next_connect_time = 0;
  // private methods
  static void taskWrapper(void * pvParameters);
  void connect();
  void schedule_connect();
  void task();
 public:
  PacketStream(int rx_buffer_len, int tx_buffer_len, int rx_queue_len, int tx_queue_len);
  ~PacketStream();
  // metrics
  unsigned long rx_buffer_full = 0;
  unsigned long rx_buffer_max = 0;
  unsigned long rx_queue_full = 0;
  unsigned long rx_queue_in = 0;
  unsigned long rx_queue_max = 0; // high watermark for rx_queue
  unsigned long rx_queue_out = 0; // messages dequeued
  unsigned long tcp_bytes_received = 0;
  unsigned long tcp_bytes_sent = 0;
  unsigned long tcp_conn_errors = 0; // connections that failed immediately
  unsigned long tcp_conn_fingerprint_errors = 0; // connections that failed TLS fingerprint check
  unsigned long tcp_conn_ok = 0; // successful TCP connections
  unsigned long tx_buffer_full = 0;
  unsigned long tx_buffer_max = 0;
  unsigned long tx_queue_full = 0; // queue-full errors via send()
  unsigned long tx_queue_in = 0; // successfully queued messages via send()
  unsigned long tx_queue_max = 0; // high watermark for tx_queue
  unsigned long tx_queue_out = 0; // messages dequeued
  // public methods
  void setConnectionStableTime(unsigned long ms);
  void setDebug(bool enable);
  void setKeepalive(unsigned long ms);
  void setReconnectMaxTime(unsigned long ms);
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
