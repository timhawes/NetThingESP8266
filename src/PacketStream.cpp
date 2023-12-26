#include "PacketStream.hpp"
#include "esp_task_wdt.h"

PacketStream::PacketStream(int rx_buffer_len, int tx_buffer_len, int rx_queue_len, int tx_queue_len):
  rx_buffer(rx_buffer_len),
  tx_buffer(tx_buffer_len)
{
  rx_queue = xQueueCreate(rx_queue_len, sizeof(asyncsession_packet_t));
  tx_queue = xQueueCreate(tx_queue_len, sizeof(asyncsession_packet_t));
}

PacketStream::~PacketStream() {

}

void PacketStream::begin() {
  xTaskCreate(
    taskWrapper, /* Task function. */
    "PacketStream", /* name of task. */
    4096, /* Stack size of task - 2048 is too small, 3072 is ok */
    this, /* parameter of the task */
    1, /* priority of the task */
    NULL
  );
}

void PacketStream::taskWrapper(void * pvParameters) {
  PacketStream *ps = (PacketStream*)pvParameters;
  ps->task();
}

void PacketStream::setDebug(bool enable) {
  debug = enable;
}

void PacketStream::setConnectionStableTime(unsigned long ms) {
  connection_stable_time = ms;
}

void PacketStream::setIdleThreshold(unsigned long ms) {
  idle_threshold = ms;
}

void PacketStream::setKeepalive(unsigned long ms) {
  keepalive_interval = ms;
}

void PacketStream::setReconnectMaxTime(unsigned long ms) {
  reconnect_interval_max = ms;
}

void PacketStream::setServer(const char *host, int port,
                             bool tls, bool verify,
                             const char *sha256_fingerprint1,
                             const char *sha256_fingerprint2) {
  
  server_host = host;
  server_port = port;
  server_tls = tls;
  server_verify = verify;
  server_sha256_fingerprint1 = sha256_fingerprint1;
  server_sha256_fingerprint2 = sha256_fingerprint2;
}

void PacketStream::onConnect(PacketStreamConnectHandler callback) {
  connect_callback = callback;
}

void PacketStream::onDisconnect(PacketStreamDisconnectHandler callback) {
  disconnect_callback = callback;
}

void PacketStream::onReceivePacket(PacketStreamReceivePacketHandler callback) {
  receivepacket_callback = callback;
}

// called from task()
void PacketStream::connect() {
  Serial.println("PacketStream: connecting");

  if (server_tls) {
    current_tls_mode = true;
    client_tls.setTimeout(10);
    client_tls.setInsecure();
    int status = client_tls.connect(server_host, server_port);
    
    if (!status) {
      char error[64];
      int err = client_tls.lastError(error, sizeof(64));
      Serial.printf("PacketStream: connect failed %d %s\r\n", err, error);
      tcp_conn_errors++;
      schedule_connect();
      return;
    }

    if (server_verify) {
      bool matched = false;
      if (server_sha256_fingerprint1 && client_tls.verify(server_sha256_fingerprint1, NULL)) {
        Serial.println("PacketStream: TLS fingerprint #1 matched");
        matched = true;
      }
      if (server_sha256_fingerprint2 && client_tls.verify(server_sha256_fingerprint2, NULL)) {
        Serial.println("PacketStream: TLS fingerprint #2 matched");
        matched = true;
      }
      if (!matched) {
        Serial.println("PacketStream: TLS fingerprint doesn't match, disconnecting");
        tcp_conn_fingerprint_errors++;
        client_tls.stop();
        return;
      }
    } else {
      Serial.println("PacketStream: TLS fingerprint not verified, continuing");
    }

    client_tls.setTimeout(0);
  } else {
    current_tls_mode = false;
    client_plain.setTimeout(10);
    int status = client_plain.connect(server_host, server_port);
    
    if (!status) {
      Serial.printf("PacketStream: connect failed\r\n");
      tcp_conn_errors++;
      schedule_connect();
      return;
    }

    client_plain.setTimeout(0);
  }

  tcp_conn_ok++;
  rx_buffer.flush();
  tx_buffer.flush();
  xQueueReset(rx_queue);
  xQueueReset(tx_queue);
  last_connect_time = millis();
  last_send = millis();
  last_receive = millis();
  connection_stable = false;
  connect_state = true;
  Serial.println("PacketStream: connection ready");
  pending_connect_callback = true;
}

void PacketStream::schedule_connect() {
  unsigned long splayed_reconnect_interval = random(0, reconnect_interval);
  Serial.print("PacketStream: reconnecting in ");
  Serial.print(splayed_reconnect_interval, DEC);
  Serial.println("ms");
  next_connect_time = millis() + splayed_reconnect_interval;

  reconnect_interval = reconnect_interval * reconnect_interval_backoff_factor;
  if (reconnect_interval > reconnect_interval_max) {
    reconnect_interval = reconnect_interval_max;
  }
}

void PacketStream::reconnect() {
  reconnect_interval = reconnect_interval_min;
  next_connect_time = millis() + reconnect_interval;
  client_plain.stop();
  client_tls.stop();
}

void PacketStream::start() {
  reconnect_interval = reconnect_interval_min;
  next_connect_time = millis();
  enabled = true;
}

void PacketStream::stop() {
  enabled = false;
  client_plain.stop();
  client_tls.stop();
}

// loop is called periodically from the main thread
void PacketStream::loop() {
  if (pending_connect_callback) {
    if (connect_callback) {
      connect_callback();
    }
    pending_connect_callback = false;
  }

  if (pending_disconnect_callback) {
    if (disconnect_callback) {
      disconnect_callback();
    }
    pending_disconnect_callback = false;
  }

  if (receivepacket_callback) {
    uint8_t packet[1500];
    size_t len = receive(packet, sizeof(packet));
    while (len > 0) {
      receivepacket_callback(packet, len);
      len = receive(packet, sizeof(packet));
    }
  }
}

// task is called from a dedicated thread
void PacketStream::task() {
  esp_err_t err = esp_task_wdt_add(NULL);
  if (err != ESP_OK) {
    Serial.println("PacketStream: failed to configure task watchdog");
  }

  while (1) {

    esp_task_wdt_reset();

    uint8_t connected = false;
    if (current_tls_mode) {
      connected = client_tls.connected();
    } else {
      connected = client_plain.connected();
    }

    if (connected) {

      // mark connection as stable
      if (!connection_stable) {
        if (millis() - last_connect_time > connection_stable_time) {
          reconnect_interval = reconnect_interval_min;
          connection_stable = true;
          if (debug) Serial.println("PacketStream: connection stable");
        }
      }

      // receive bytes
      if (current_tls_mode) {
        while (rx_buffer.room() && client_tls.available()) {
          rx_buffer.write(client_tls.read());
          tcp_bytes_received++;
          last_receive = millis();
        }
      } else {
        while (rx_buffer.room() && client_plain.available()) {
          rx_buffer.write(client_plain.read());
          tcp_bytes_received++;
          last_receive = millis();
        }
      }
      if (rx_buffer.available() > rx_buffer_max) {
        rx_buffer_max = rx_buffer.available();
      }

      // process rx cbuf into packets
      if (rx_buffer.available() >= 2) {
        char peekbuf[2];
        rx_buffer.peek(peekbuf, 2);
        uint16_t length = (peekbuf[0] << 8) | peekbuf[1];
        if (length == 0) {
          if (debug) Serial.println("PacketStream: keepalive packet received");
          rx_buffer.remove(2);
        } else {
          if (rx_buffer.available() >= length + 2) {
            // FIXME: check if space is available in rx_queue before removing from rx_buffer
            uint8_t *data = new uint8_t[length];
            rx_buffer.remove(2);
            rx_buffer.read((char*)data, length);

            asyncsession_packet_t packet;
            packet.data = data;
            packet.len = length;
            if (xQueueSend(rx_queue, &packet, 0)) {
              rx_queue_in++;
              int messages_in_rx_queue = uxQueueMessagesWaiting(rx_queue);
              if (messages_in_rx_queue > rx_queue_max) {
                rx_queue_max = messages_in_rx_queue;
              }
            } else {
              rx_queue_full++;
            }
          }
        }
      }

      // move packets from send queue to tx cbuf
      asyncsession_packet_t packet;
      if (xQueuePeek(tx_queue, &packet, 0)) {
        if (tx_buffer.room() >= packet.len + 2) {
          uint8_t msb = (packet.len & 0xFF00) >> 8;
          uint8_t lsb = packet.len & 0xFF;
          size_t sent = 0;
          sent += tx_buffer.write(msb);
          sent += tx_buffer.write(lsb);
          sent += tx_buffer.write((const char*)packet.data, packet.len);
          if (sent != packet.len + 2) {
            Serial.println("PacketStream: error, tx_buffer may be corrupt");
          }
        }
        xQueueReceive(tx_queue, &packet, 0);
        last_send = millis();
        tx_queue_out++;
        delete packet.data;
      }

      // send keepalive packets
      if (keepalive_interval > 0 && (long)(millis() - last_send) > keepalive_interval) {
        if (tx_buffer.room() >= 2) {
          Serial.println("PacketStream: sending keepalive packet");
          tx_buffer.write(0);
          tx_buffer.write(0);
          last_send = millis();
        }
      }

      // tx bytes from cbuf
      size_t available = tx_buffer.available();
      if (available > tx_buffer_max) {
        tx_buffer_max = available;
      }
      if (available > 0) {
        uint8_t *out = new uint8_t[available];
        tx_buffer.read((char*)out, available);
        size_t sent;
        if (current_tls_mode) {
          sent = client_tls.write(out, available);
        } else {
          sent = client_plain.write(out, available);
        }
        tcp_bytes_sent += sent;
        // if (debug) {
        //   Serial.printf("PacketStream::task: sent %d bytes\r\n", sent);
        //   Serial.write(out, sent);
        // }
        delete[] out;
        if (sent == 0) {
          Serial.println("PacketStream: send failed, error during dequeue, queue may be corrupt");
        } else if (sent != available) {
          Serial.println("PacketStream: send partially failed, error during dequeue, queue may be corrupt");
        }
      }
    } else {
      if (connect_state) {
        // disconnection detected
        if (current_tls_mode) {
          char client_error_text[64];
          int client_error_number = client_tls.lastError(client_error_text, sizeof(client_error_text));
          Serial.printf("PacketStream: disconnected %d %s\r\n", client_error_number, client_error_text);
        } else {
          Serial.printf("PacketStream: disconnected\r\n");
        }
        pending_disconnect_callback = true;
        connect_state = false;
        schedule_connect();
      }
      if (enabled) {
        if ((long)(millis() - next_connect_time) > 0) {
          connect();
        }
      }
    }
  
    delay(10);
  }

}

// this runs in the main thead
bool PacketStream::send(const uint8_t* data, size_t len) {
  uint8_t *buf = new uint8_t[len];
  memcpy(buf, data, len);

  asyncsession_packet_t packet;
  packet.data = buf;
  packet.len = len;

  if (xQueueSend(tx_queue, &packet, 0)) {
    tx_queue_in++;
    int messages_in_tx_queue = uxQueueMessagesWaiting(tx_queue);
    if (messages_in_tx_queue > tx_queue_max) {
      tx_queue_max = messages_in_tx_queue;
    }
    return true;
  } else {
    tx_queue_full++;
    Serial.println("PacketStream: failed to en-queue a packet for tx");
    delete buf;
    return false;
  }
}

// this runs in the main thead
size_t PacketStream::receive(uint8_t* data, size_t len) {
  asyncsession_packet_t packet;
  if (xQueueReceive(rx_queue, &packet, 0)) {
    rx_queue_out++;
    if (packet.len > len) {
      // incoming packet is too large
      Serial.println("failed to de-queue a packet for rx (too large for buffer)");
      delete packet.data;
      return 0;
    }
    memcpy(data, packet.data, packet.len);
    delete packet.data;
    return packet.len;
  }
  return 0;
}

bool PacketStream::idle() {
  if (connect_state) {
    if (rx_buffer.available() > 0) return false;
    if (tx_buffer.available() > 0) return false;
    if (uxQueueMessagesWaiting(tx_queue) > 0) return false;
    if (uxQueueMessagesWaiting(rx_queue) > 0) return false;
    if ((long)(millis() - last_send) < idle_threshold) return false;
    if ((long)(millis() - last_receive) < idle_threshold) return false;
  }
  return true;
}
