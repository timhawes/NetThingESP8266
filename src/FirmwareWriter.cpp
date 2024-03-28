#include "FirmwareWriter.hpp"

#include <Updater.h>

FirmwareWriter::FirmwareWriter() {
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
}

FirmwareWriter::~FirmwareWriter() {
  abort();
}

void FirmwareWriter::abort() {
  if (update_active) {
    Serial.println("FirmwareWriter: abort");
    // write some dummy data to break the MD5 check
    Update.write((uint8_t *)"_ABORT_", 7);
    // end the update
    Update.end();
    update_active = false;
  }
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
  begin_active = false;
}

bool FirmwareWriter::add(uint8_t *data, unsigned int len) {
  return add(data, len, _position);
}

bool FirmwareWriter::add(uint8_t *data, unsigned int len, unsigned int pos) {
  if (!begin_active) {
    Serial.println("FirmwareWriter: add() called without begin()");
    return false;
  }

  if (pos != _position) {
    Serial.print("FirmwareWriter: firmware position mismatch (expected=");
    Serial.print(_position, DEC);
    Serial.print(" received=");
    Serial.print(pos, DEC);
    Serial.println(")");
    return false;
  }

  if (_position == 0 && (!update_active)) {
    if (len < 4) {
      // need at least 4 bytes to check the file header
      Serial.println("FirmwareWriter: need at least 4 bytes to check the file header");
      return false;
    }
    if (data[0] != 0xE9) {
      // magic header doesn't start with 0xE9
      Serial.println("FirmwareWriter: magic header doesn't start with 0xE9");
      return false;
    }
    uint32_t bin_flash_size = ESP.magicFlashChipSize((data[3] & 0xf0) >> 4);
    // new file doesn't fit into flash
    if (bin_flash_size > ESP.getFlashChipRealSize()) {
      Serial.println("FirmwareWriter: new file won't fit into flash");
      return false;
    }
    Update.runAsync(true);
    if (!Update.begin(_size, U_FLASH)) {
      Serial.println("FirmwareWriter: Update.begin() failed");
      Update.printError(Serial);
      return false;
    }
    if (!Update.setMD5(_md5)) {
      Serial.println("FirmwareWriter: Update.setMD5() failed");
      Update.printError(Serial);
      return false;
    }
    update_active = true;
  }
  if (!update_active) {
    return false;
  }
  // Serial.print("FirmwareWriter: writing ");
  // Serial.print(len, DEC);
  // Serial.print(" bytes at position ");
  // Serial.println(_position, DEC);
  if (Update.write(data, len) == len) {
    _position += len;
    return true;
  } else {
    Update.printError(Serial);
    return false;
  }
}

bool FirmwareWriter::begin(const char *md5, size_t size) {
  if (begin_active || update_active) {
    if (_size != size || strcmp(_md5, md5) != 0) {
      Serial.println("FirmwareWriter: aborting firmware update to start a different one");
      abort();
    }
  }

  if (begin_active) {
    Serial.print("FirmwareWriter: continuing firmware update at position ");
    Serial.println(_position, DEC);
    return true;
  }

  _size = size;
  strncpy(_md5, md5, sizeof(_md5));

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

  Serial.println("FirmwareWriter: starting new update at position 0");
  _position = 0;
  begin_active = true;
  return true;
}

bool FirmwareWriter::commit() {
  if (update_active) {
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

unsigned int FirmwareWriter::position() {
  if (begin_active) {
    return _position;
  } else {
    Serial.println("FirmwareWriter: position() called without begin()");
    return 0;
  }
}

int FirmwareWriter::progress() {
  if (_size > 0) {
    return (100 * _position) / _size;
  } else {
    return 0;
  }
}

bool FirmwareWriter::upToDate(const char *md5) {
  String current_md5 = ESP.getSketchMD5();
  if (strncmp(current_md5.c_str(), md5, 32) == 0) {
    return true;
  } else {
    return false;
  }
}
