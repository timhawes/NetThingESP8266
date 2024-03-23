#ifndef NETTHING_RESTART_HPP
#define NETTHING_RESTART_HPP

#include "Arduino.h"

#ifdef ESP8266
#ifndef NETTHING_RESTARTER_RTCOFFSET
#define NETTHING_RESTARTER_RTCOFFSET 78
#endif
#endif
#ifndef NETTHING_RESTARTER_RTCMAGIC
#define NETTHING_RESTARTER_RTCMAGIC 0x2DF5
#endif

class Restarter
{
private:
  uint16_t last_reason = 0;
public:
  Restarter();
  uint16_t getReason();
  void restartWithReason(uint16_t reason);
};

#endif
