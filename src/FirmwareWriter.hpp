#ifndef FIRMWAREWRITER_HPP
#define FIRMWAREWRITER_HPP

#include <Arduino.h>

class FirmwareWriter {
 private:
  char _md5[33];
  size_t _size = 0;
  unsigned int _position = 0;
  bool begin_active = false;
  bool update_active = false;

 public:
  FirmwareWriter();
  ~FirmwareWriter();
  void abort();
  bool add(uint8_t *data, unsigned int len, unsigned int pos);
  bool add(uint8_t *data, unsigned int len);
  bool begin(const char *md5, size_t size);
  bool commit();
  int getUpdaterError();
  unsigned int position();
  int progress();
  bool upToDate(const char *md5);
};

#endif
