#include "PacketStream.hpp"

#include "tcp_axtls.h"

using namespace std::placeholders;

PacketStream::PacketStream(int rx_buffer_len, int tx_buffer_len):
  rx_buffer(rx_buffer_len),
  tx_buffer(tx_buffer_len)
{

}

void PacketStream::setServer(const char *host, int port,
                             bool secure, bool verify,
                             const uint8_t *fingerprint1,
                             const uint8_t *fingerprint2) {
  server_host = host;
  server_port = port;
  server_secure = secure;
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

void PacketStream::start() {
  enabled = true;
  scheduleConnect();
}

void PacketStream::stop() {
  enabled = false;
  //reconnect_ticker.detach();
  client.close(true);
}

void PacketStream::reconnect() {
  // disconnect, allow disconnect handler to reconnect
  client.close(true);
}

void PacketStream::connect() {
  if (!enabled) {
    Serial.println("PacketStream: connect(): not enabled");
    return;
  }

  if (client.connected() || client.connecting()) {
    Serial.println("PacketStream: connect(): already connected or connecting");
    return;
  }

  if (tcp_active) {
    tcp_double_connect_errors++;
    return;
  }

  tcp_active = true;

  client.onError([&](void *arg, AsyncClient *c, int error) {
    tcp_async_errors++;
    Serial.print("PacketStream: TCP client error ");
    Serial.print(error, DEC);
    Serial.print(": ");
    Serial.println(c->errorToString(error));
    tcp_active = false;
    scheduleConnect();
  },
  NULL);

  //client.onPoll([=](void *arg, AsyncClient *c) {
  //  //if (tx_buffer.available() > 0) {
  //  //  processTxBuffer();
  //  //}
  //},
  //NULL);

  client.onConnect([=](void *arg, AsyncClient *c) {
    Serial.println("PacketStream: TCP client connected");
    tcp_connects++;
    if (server_secure) {
      if (server_verify) {
        SSL *ssl = c->getSSL();
        bool matched = false;
        if (ssl_match_fingerprint(ssl, server_fingerprint1) == 0) {
          Serial.println("PacketStream: TLS fingerprint #1 matched");
          matched = true;
        }
        if (ssl_match_fingerprint(ssl, server_fingerprint2) == 0) {
          Serial.println("PacketStream: TLS fingerprint #2 matched");
          matched = true;
        }
        if (!matched) {
          Serial.println("PacketStream: TLS fingerprint doesn't match");
          tcp_fingerprint_errors++;
          c->close(true);
        }
      } else {
        Serial.println("PacketStream: TLS fingerprint not verified");
      }
    }
    rx_buffer.flush();
    tx_buffer.flush();
    Serial.println("PacketStream: TCP connected");
    if (connect_callback) {
      connect_callback();
    }
  },
  NULL);

  client.onDisconnect([=](void *arg, AsyncClient *c) {
    Serial.println("PacketStream: TCP client disconnected");
    rx_buffer.flush();
    tx_buffer.flush();
    if (disconnect_callback) {
      disconnect_callback();
    }
    tcp_active = false;
    scheduleConnect();
  },
  NULL);

  client.onData([=](void *arg, AsyncClient *c, void *data, size_t len) {
    if (debug_packet) {
      Serial.print("PacketStream: received ");
      Serial.print(len, DEC);
      Serial.println(" bytes");
    }
    if (len > 0) {
      for (unsigned int i = 0; i < len; i++) {
        if (rx_buffer.write(((uint8_t *)data)[i]) != 1) {
          Serial.println("PacketStream: buffer is full, closing session!");
          c->close(true);
          return;
        }
      }
    }
    if (rx_buffer.available() > rx_buffer_high_watermark) {
      rx_buffer_high_watermark = rx_buffer.available();
    }
    if (fast_receive) {
      processRxBuffer();
    }
  },
  NULL);

  client.setAckTimeout(5000);
  client.setRxTimeout(300);
  client.setNoDelay(true);

  if (!client.connect(server_host, server_port, server_secure)) {
    tcp_sync_errors++;
    Serial.println("PacketStream: connect(): connect failed immediately");
    client.close(true);
    tcp_active = false;
    scheduleConnect();
  }  
}

bool PacketStream::sendPacket(const char* packet, size_t packet_len) {
  char header[3];

  if (debug_packet) {
    Serial.print("PacketStream: send ");
    for (int i=0; i<packet_len; i++) {
      printf("%02x", packet[i]);
    }
    Serial.println();
  }

  if (tx_buffer.room() < 2 + packet_len) {
    Serial.println("PacketStream: send failed, no room in tx queue");
    packet_queue_full++;
  }

  int sent = 0;

  header[0] = (packet_len & 0xFF00) >> 8;
  header[1] = packet_len & 0xFF;
  sent += tx_buffer.write(header[0]);
  sent += tx_buffer.write(header[1]);
  for (int i = 0; i < packet_len; i++) {
    sent += tx_buffer.write(packet[i]);
  }

  if (sent == packet_len + 2) {
    packet_queue_ok++;
  } else {
    packet_queue_error++;
    Serial.println("PacketStream: send failed, error during queue, queue may be corrupt");
    return false;
  }

  if (fast_send) {
    processTxBuffer();
  }

  return true;
}

size_t PacketStream::processTxBuffer() {
  size_t available = tx_buffer.available();
  if (available > tx_buffer_high_watermark) {
    tx_buffer_high_watermark = available;
  }
  if (available > 0) {
    if (client.canSend()) {
      size_t sendable = client.space();
      if (sendable < available) {
        available = sendable;
      }
      char *out = new char[available];
      tx_buffer.read(out, available);
      size_t sent = client.write(out, available);
      delete[] out;
      return sent;
    } else {
      if (debug_packet) {
        Serial.println("PacketStream: processTxBuffer() can't send yet");
      }
      tx_delay_count++;
      return 0;
    }
  }
  return 0;
}

size_t PacketStream::processRxBuffer() {
  if (in_rx_handler) {
    Serial.println("PacketStream::processRxBuffer(): double entry");
    return 0;
  }
  in_rx_handler = true;

  int processed_bytes = 0;

  while (rx_buffer.available() >= 2) {
    // while (receive_buffer->getSize() >= 2) {
    // at least two bytes in the buffer
    char peekbuf[2];
    rx_buffer.peek(peekbuf, 2);
    uint16_t length = (peekbuf[0] << 8) | peekbuf[1];
    if (rx_buffer.available() >= length + 2) {
      uint8_t *packet = new uint8_t[length+1];
      rx_buffer.remove(2);
      rx_buffer.read((char*)packet, length);
      processed_bytes++;
      packet[length] = 0;
      if (debug_packet) {
        Serial.print("PacketStream: recv ");
        for (int i=0; i<length; i++) {
          Serial.printf("%02x", packet[i]);
        }
        Serial.println();
      }
      if (receivepacket_callback) {
        receivepacket_callback((const char*)packet, length);
      }
      delete[] packet;
    } else {
      // packet isn't complete
      break;
    }
  };

  in_rx_handler = false;
  return processed_bytes;
}

void PacketStream::scheduleConnect() {
  if (!connect_scheduled) {
    Serial.println("PacketStream: scheduleConnect(): scheduling connect");
    connect_scheduled_time = millis();
    connect_scheduled = true;
  }
}

void PacketStream::loop() {
  if (connect_scheduled) {
    if (millis() - connect_scheduled_time > reconnect_interval) {
      connect_scheduled = false;
      connect();
    }
  }
  processRxBuffer();
  processTxBuffer();
}
