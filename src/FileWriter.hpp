#ifndef FILEWRITER_HPP
#define FILEWRITER_HPP

#include <Arduino.h>
#include <FS.h>

class FileWriter {
 private:
  File file_handle;
  char _filename[31];
  char _tmp_filename[32];
  char _md5[33];
  size_t _size = 0;
  bool active = false;
  bool file_open = false;
  unsigned int received_size;
  unsigned long last_activity;
  void parse_md5_stream(MD5Builder *md5, Stream *stream);

 public:
  FileWriter();
  bool begin(const char *filename, const char *md5, size_t size);
  bool upToDate();
  bool open();
  bool add(uint8_t *data, unsigned int len);
  bool add(uint8_t *data, unsigned int len, unsigned int pos);
  bool commit();
  void abort();
  bool running();
  int idleMillis();
};

#endif
