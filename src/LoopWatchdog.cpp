#include "LoopWatchdog.hpp"

static RTC_NOINIT_ATTR uint32_t loopwatchdog_rtc_data;

LoopWatchdog::LoopWatchdog() {
    if (loopwatchdog_rtc_data == rtc_magic) {
        restarted_flag = true;
        loopwatchdog_rtc_data = 0;
    }
}

void LoopWatchdog::setTimeout(unsigned long _timeout) {
    timeout = _timeout;
}

void LoopWatchdog::callback() {
    if (timeout > 0) {
        if ((long)(millis() - last_feed) > timeout) {
            loopwatchdog_rtc_data = rtc_magic;
            Serial.println("LoopWatchdog: restarting");
            ESP.restart();
        }
    }
}

void LoopWatchdog::feed() {
    last_feed = millis();
    if (!configured) {
        ticker.attach(1, +[](LoopWatchdog* loopwatchdog) { loopwatchdog->callback(); }, this);
        configured = true;
    }
}

void LoopWatchdog::stop() {
    if (configured) {
        ticker.detach();
        configured = false;
    }
}

bool LoopWatchdog::restarted() {
    return restarted_flag;
}
