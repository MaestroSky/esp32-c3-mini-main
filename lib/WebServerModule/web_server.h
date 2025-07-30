#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>

// Оголошуємо, що ці змінні існують десь в іншому місці (у main.cpp).
// Це дозволяє web_server.cpp "бачити" їх, не створюючи копій.
extern String ssid;
extern String password;
extern String api_key;
extern String city;
extern String macStr;
extern long gmtOffset_sec;
extern bool daylightOffset_enabled;
extern String latest_version;
extern bool update_available;

// Оголошуємо функції, які будемо викликати з main.cpp
void setupWebServer();
void handleWebServerClient();

#endif // WEB_SERVER_H