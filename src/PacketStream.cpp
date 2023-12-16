#include "PacketStream.hpp"

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

void PacketStream::setKeepalive(unsigned long ms) {
  keepalive_interval = ms;
}

void PacketStream::setReconnectMaxTime(unsigned long ms) {
  reconnect_interval_max = ms;
}

void PacketStream::setServer(const char *host, int port,
                             bool verify,
                             const char *fingerprint1,
                             const char *fingerprint2) {
  server_host = host;
  server_port = port;
  server_verify = verify;
  server_fingerprint1 = fingerprint1;
  server_fingerprint2 = fingerprint2;
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

  client.setTimeout(10);
  client.setInsecure();
  int status = client.connect(server_host, server_port);

  if (!status) {
    char error[64];
    int err = client.lastError(error, sizeof(64));
    Serial.printf("PacketStream: connect failed %d %s\n", err, error);
    tcp_conn_errors++;
    schedule_connect();
    return;
  }

  if (server_verify) {
    bool matched = false;
    if (server_fingerprint1 && client.verify(server_fingerprint1, NULL)) {
      Serial.println("PacketStream: TLS fingerprint #1 matched");
      matched = true;
    }
    if (server_fingerprint2 && client.verify(server_fingerprint2, NULL)) {
      Serial.println("PacketStream: TLS fingerprint #2 matched");
      matched = true;
    }
    if (!matched) {
      Serial.println("PacketStream: TLS fingerprint doesn't match, disconnecting");
      tcp_conn_fingerprint_errors++;
      client.stop();
      return;
    }
  } else {
    Serial.println("PacketStream: TLS fingerprint not verified, continuing");
  }

  tcp_conn_ok++;
  client.setTimeout(0);
  rx_buffer.flush();
  tx_buffer.flush();
  xQueueReset(rx_queue);
  xQueueReset(tx_queue);
  last_connect_time = millis();
  last_send = millis();
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
  client.stop();
}

void PacketStream::start() {
  reconnect_interval = reconnect_interval_min;
  next_connect_time = millis();
  enabled = true;
}

void PacketStream::stop() {
  enabled = false;
  client.stop();
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
  while (1) {

    if (client.connected()) {

      // mark connection as stable
      if (!connection_stable) {
        if (millis() - last_connect_time > connection_stable_time) {
          reconnect_interval = reconnect_interval_min;
          connection_stable = true;
          if (debug) Serial.println("PacketStream: connection stable");
        }
      }

      // receive bytes
      while (rx_buffer.room() && client.available()) {
        rx_buffer.write(client.read());
        tcp_bytes_received++;
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
        sent = client.write(out, available);
        tcp_bytes_sent += sent;
        // if (debug) {
        //   Serial.printf("PacketStream::task: sent %d bytes\n", sent);
        //   Serial.write(out, sent);
        // }
        delete[] out;
        if (sent == 0) {
          Serial.println("PacketStream: send failed, error during dequeue, queue may be corrupt");
        } else if (sent != available) {
          Serial.println("PacketStream: send partially failed, error during dequeue, queue may be corrupt");
        }
        // return sent;
      }
    } else {
      if (connect_state) {
        // disconnection detected
        char client_error_text[64];
        int client_error_number = client.lastError(client_error_text, sizeof(client_error_text));
        Serial.printf("PacketStream: disconnected %d %s\n", client_error_number, client_error_text);
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
