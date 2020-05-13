#ifndef FIRMWAREWRITER_HPP
#define FIRMWAREWRITER_HPP

#include <Arduino.h>

class FirmwareWriter {
 private:
  char _md5[33];
  size_t _size = 0;
  unsigned int position = 0;
  bool started = false;
  unsigned long last_activity = 0;

 public:
  FirmwareWriter();
  ~FirmwareWriter();
  void abort();
  bool add(uint8_t *data, unsigned int len, unsigned int pos);
  bool add(uint8_t *data, unsigned int len);
  bool begin(const char *md5, size_t size);
  bool commit();
  int getUpdaterError();
  bool open();
  int progress();
  bool running();
  bool upToDate();
  int idleMillis();
};

#endif
