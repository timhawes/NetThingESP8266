#include "PacketStream.hpp"

PacketStream::PacketStream(int rx_buffer_len, int tx_buffer_len):
  rx_buffer(rx_buffer_len),
  tx_buffer(tx_buffer_len)
{
  rx_queue = xQueueCreate(20, sizeof(asyncsession_packet_t));
  tx_queue = xQueueCreate(20, sizeof(asyncsession_packet_t));
  last_connect_attempt = millis() - reconnect_interval;
}

PacketStream::~PacketStream() {

}

void PacketStream::begin() {
  if ((signed)1 > (unsigned)tx_buffer_high_watermark) {
    Serial.println();
  }
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
    tcp_sync_errors++;
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
      tcp_fingerprint_errors++;
      client.stop();
      return;
    }
  } else {
    Serial.println("PacketStream: TLS fingerprint not verified, continuing");
  }

  tcp_connects++;
  client.setTimeout(0);
  rx_buffer.flush();
  tx_buffer.flush();
  connect_state = true;
  Serial.println("PacketStream: connection ready");
  pending_connect_callback = true;
}

void PacketStream::reconnect() {
  client.stop();
}

void PacketStream::start() {
  enabled = true;
  last_connect_attempt = millis() - reconnect_interval;
}

void PacketStream::stop() {
  enabled = false;
  client.stop();
}

// loop is called periodically from the main thread
void PacketStream::loop() {
  if (pending_connect_callback) {
    Serial.println("PacketStream: running connect callback");
    if (connect_callback) {
      connect_callback();
    }
    pending_connect_callback = false;
  }

  if (pending_disconnect_callback) {
    Serial.println("PacketStream: running disconnect callback");
    if (disconnect_callback) {
      disconnect_callback();
    }
    pending_disconnect_callback = false;
  }

  if (receivepacket_callback) {
    uint8_t packet[1500];
    size_t len = receive(packet, sizeof(packet));
    while (len > 0) {
      // Serial.println("PacketStream: received packet, calling callback");
      receivepacket_callback(packet, len);
      len = receive(packet, sizeof(packet));
    }
  }
}

// task is called from a dedicated thread
void PacketStream::task() {
  while (1) {

    if (client.connected()) {

      // receive bytes
      while (rx_buffer.room() && client.available()) {
        rx_buffer.write(client.read());
        tcp_bytes_received++;
      }
      if (rx_buffer.available() > rx_buffer_high_watermark) {
        rx_buffer_high_watermark = rx_buffer.available();
      }

      // process rx cbuf into packets
      // if (debug_task) Serial.printf("PacketStream::task: rx_buffer.available = %u\n", rx_buffer.available());
      if (rx_buffer.available() > 2) {
        char peekbuf[2];
        rx_buffer.peek(peekbuf, 2);
        uint16_t length = (peekbuf[0] << 8) | peekbuf[1];
        // if (debug_task) Serial.printf("PacketStream::task: length header = %d\n", length);
        if (rx_buffer.available() >= length + 2) {
          uint8_t *data = new uint8_t[length];
          rx_buffer.remove(2);
          rx_buffer.read((char*)data, length);

          asyncsession_packet_t packet;
          packet.data = data;
          packet.len = length;
          if (xQueueSend(rx_queue, &packet, 0)) {
            int messages_in_rx_queue = uxQueueMessagesWaiting(rx_queue);
            if (messages_in_rx_queue > rx_queue_high_watermark) {
              rx_queue_high_watermark = messages_in_rx_queue;
            }
          } else {
            rx_queue_full_errors++;
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
        tx_queue_count++;
        delete packet.data;
      }

      // if (debug_task) Serial.printf("PacketStream::task: tx_buffer.available() = %u\n", tx_buffer.available());

      // tx bytes from cbuf
      size_t available = tx_buffer.available();
      if (available > tx_buffer_high_watermark) {
        tx_buffer_high_watermark = available;
      }
      if (available > 0) {
        uint8_t *out = new uint8_t[available];
        tx_buffer.read((char*)out, available);
        size_t sent;
        sent = client.write(out, available);
        tcp_bytes_sent++;
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
        // this is where we noticed that the connection has gone
        Serial.println("PacketStream: disconnect detected");
        char client_error_text[64];
        int client_error_number = client.lastError(client_error_text, sizeof(client_error_text));
        Serial.printf("PacketStream: disconnected %d %s\n", client_error_number, client_error_text);
        pending_disconnect_callback = true;
        connect_state = false;
      }
      if (enabled) {
        if (millis() - last_connect_attempt > reconnect_interval) {
          last_connect_attempt = millis();
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
    int messages_in_tx_queue = uxQueueMessagesWaiting(tx_queue);
    if (messages_in_tx_queue > tx_queue_high_watermark) {
      tx_queue_high_watermark = messages_in_tx_queue;
    }
    return true;
  } else {
    tx_queue_full_errors++;
    Serial.println("PacketStream: failed to en-queue a packet for tx");
    delete buf;
    return false;
  }
}

// this runs in the main thead
size_t PacketStream::receive(uint8_t* data, size_t len) {
  asyncsession_packet_t packet;
  if (xQueueReceive(rx_queue, &packet, 0)) {
    rx_queue_count++;
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
