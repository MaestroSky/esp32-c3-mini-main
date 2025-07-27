#ifndef WAKEONLAN_H
#define WAKEONLAN_H
#include <WiFi.h>
#include <WiFiUdp.h>
void send_wol_packet(const byte *macAddress) {
    byte magicPacket[102];
    for (int i = 0; i < 6; i++) magicPacket[i] = 0xFF;
    for (int i = 1; i < 17; i++) {
        for (int j = 0; j < 6; j++) magicPacket[i * 6 + j] = macAddress[j];
    }
    WiFiUDP udp;
    IPAddress broadcastIp = ~uint32_t(WiFi.subnetMask()) | uint32_t(WiFi.localIP());
    udp.beginPacket(broadcastIp, 9);
    udp.write(magicPacket, sizeof(magicPacket));
    udp.endPacket();
}
#endif