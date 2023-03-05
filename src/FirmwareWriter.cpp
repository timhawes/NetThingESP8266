#include "FirmwareWriter.hpp"

#include <Update.h>

FirmwareWriter::FirmwareWriter() {
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
}

FirmwareWriter::~FirmwareWriter() {
  abort();
}

void FirmwareWriter::abort() {
  if (started) {
    Serial.println("FirmwareWriter: abort");
    Update.abort();
    strncpy(_md5, "", sizeof(_md5));
    _size = 0;
    started = false;
  }
}

bool FirmwareWriter::add(uint8_t *data, unsigned int len) {
  return add(data, len, position);
}

bool FirmwareWriter::add(uint8_t *data, unsigned int len, unsigned int pos) {
  last_activity = millis();
  if (pos != position) {
    Serial.print("FirmwareWriter: firmware position mismatch (expected=");
    Serial.print(position, DEC);
    Serial.print(" received=");
    Serial.println(pos, DEC);
    return false;
  }
  if (position == 0 && (!started)) {
    if (!Update.begin(_size)) {
      Serial.println("FirmwareWriter: Update.begin() failed");
      Update.printError(Serial);
      return false;
    }
    if (!Update.setMD5(_md5)) {
      Serial.println("FirmwareWriter: Update.setMD5() failed");
      Update.printError(Serial);
      return false;
    }
    started = true;
  }
  if (!started) {
    return false;
  }
  // Serial.print("FirmwareWriter: writing ");
  // Serial.print(len, DEC);
  // Serial.print(" bytes at position ");
  // Serial.println(position, DEC);
  if (Update.write(data, len) == len) {
    position += len;
    return true;
  } else {
    Update.printError(Serial);
    return false;
  }
}

bool FirmwareWriter::begin(const char *md5, size_t size) {
  if (started) {
    return false;
  } else {
    _size = size;
    strncpy(_md5, md5, sizeof(_md5));
    position = 0;
    started = false;
    last_activity = millis();
    return true;
  }
}

bool FirmwareWriter::commit() {
  if (started) {
    Serial.println("FirmwareWriter: finishing up");
    if (Update.end()) {
      Serial.println("FirmwareWriter: end() succeeded");
      return true;
    } else {
      Serial.println("FirmwareWriter: end() failed");
      Update.printError(Serial);
      Serial.println(Update.getError());
      return false;
    }
  } else {
    Serial.println("FirmwareWriter: nothing to commit");
    return false;
  }
}

int FirmwareWriter::getUpdaterError() {
  return Update.getError();
}

bool FirmwareWriter::open() {
  String current_md5 = ESP.getSketchMD5();
  if (strncmp(current_md5.c_str(), _md5, 32) == 0) {
    // this firmware is already installed
    Serial.println("FirmwareWriter: existing firmware has same md5");
    return false;
  }
  if (_size > (unsigned int)ESP.getFreeSketchSpace()) {
    // not enough space for new firmware
    Serial.println("FirmwareWriter: not enough free sketch space");
    return false;
  }
  position = 0;
  return true;
}

int FirmwareWriter::progress() {
  if (_size > 0) {
    return (100 * position) / _size;
  } else {
    return 0;
  }
}

bool FirmwareWriter::running() {
  return started;
}

int FirmwareWriter::idleMillis() {
  if (started) {
    return millis() - last_activity;
  } else {
    return -1;
  }
}

bool FirmwareWriter::upToDate() {
  String current_md5 = ESP.getSketchMD5();
  if (strncmp(current_md5.c_str(), _md5, 32) == 0) {
    return true;
  } else {
    return false;
  }
}
