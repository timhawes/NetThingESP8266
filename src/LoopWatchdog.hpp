#ifndef LOOP_WATCHDOG_HPP
#define LOOP_WATCHDOG_HPP

#include <Arduino.h>
#include <Ticker.h>

class LoopWatchdog
{
private:
  Ticker ticker;
  void callback();
  unsigned long last_feed;
  unsigned long timeout = 60000;
  const uint32_t rtc_offset = 78;
  const uint32_t rtc_magic = 0x2df58713;
  bool configured = false;
  bool restarted_flag = false;

public:
  LoopWatchdog();
  void setTimeout(unsigned long timeout);
  void feed();
  bool restarted();
};

#endif
