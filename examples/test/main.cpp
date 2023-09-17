#include "NetThing.hpp"

NetThing net;

void setup() {
    net.setDebug(true);
    net.setWiFi("ssid", "password");
    net.setServer("host", 12345, true, false, NULL, NULL);
    net.setCred("password");
    net.setReceiveWatchdog(3600000);
    net.setLoopWatchdog(30000);
    net.start();
}

void loop() {
    net.loop();
}
