#include "Restarter.hpp"

#ifdef ESP32
static RTC_NOINIT_ATTR uint32_t netthing_restarter_rtc;
#endif

Restarter::Restarter() {
#ifdef ESP8266
    uint32_t data;
    ESP.rtcUserMemoryRead(NETTHING_RESTARTER_RTCOFFSET, &data, 4);
    if ((data & 0xFFFF0000) == NETTHING_RESTARTER_RTCMAGIC << 16) {
        last_reason = data & 0xFFFF;
        data = 0;
        ESP.rtcUserMemoryWrite(NETTHING_RESTARTER_RTCOFFSET, &data, 4);
    }
#endif
#ifdef ESP32
    if ((netthing_restarter_rtc & 0xFFFF0000) == NETTHING_RESTARTER_RTCMAGIC << 16) {
        last_reason = netthing_restarter_rtc & 0xFFFF;
        netthing_restarter_rtc = 0;
    }
#endif
}

uint16_t Restarter::getReason() {
    return last_reason;
}

void Restarter::restartWithReason(uint16_t reason) {
    uint32_t data = NETTHING_RESTARTER_RTCMAGIC << 16 | reason;
#ifdef ESP8266
    ESP.rtcUserMemoryWrite(NETTHING_RESTARTER_RTCOFFSET, &data, 4);
#endif
#ifdef ESP32
    netthing_restarter_rtc = data;
#endif
    ESP.restart();
}
