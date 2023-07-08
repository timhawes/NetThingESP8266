#include "LoopWatchdog.hpp"

LoopWatchdog::LoopWatchdog() {
    uint32_t data;
    ESP.rtcUserMemoryRead(rtc_offset, &data, 4);
    if (data == rtc_magic) {
        restarted_flag = true;
        data = 0;
        ESP.rtcUserMemoryWrite(rtc_offset, &data, 4);
    }
}

void LoopWatchdog::setTimeout(unsigned long _timeout) {
    timeout = _timeout;
}

void LoopWatchdog::callback() {
    if (timeout > 0) {
        if ((long)(millis() - last_feed) > timeout) {
            uint32_t data = rtc_magic;
            ESP.rtcUserMemoryWrite(rtc_offset, &data, 4);
            Serial.println("LoopWatchdog: restarting");
            ESP.restart();
        }
    }
}

void LoopWatchdog::feed() {
    last_feed = millis();
    if (!configured) {
        ticker.attach(1, std::bind(&LoopWatchdog::callback, this));
        configured = true;
    }
}

bool LoopWatchdog::restarted() {
    return restarted_flag;
}
