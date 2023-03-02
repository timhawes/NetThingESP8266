#include "NetThing.hpp"

NetThing net;

void setup() {
    net.setDebug(true);
    net.setWiFi("ssid", "password");
    net.setServer("host", 12345, true, false, NULL, NULL);
    net.setCred("password");
    net.start();
}

void loop() {
    net.loop();
}
