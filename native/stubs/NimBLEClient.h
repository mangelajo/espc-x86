// NimBLE client stub
#pragma once

#include "NimBLEDevice.h"
#include <string>
#include <functional>

class NimBLERemoteCharacteristic {
public:
    std::string readValue() { return ""; }
    bool writeValue(const uint8_t *data, size_t len, bool response = false) { return false; }
    bool subscribe(bool notifications, std::function<void(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)> cb = nullptr) { return false; }
    bool canNotify() { return false; }
    bool canIndicate() { return false; }
};

class NimBLERemoteService {
public:
    NimBLERemoteCharacteristic *getCharacteristic(const char *uuid) { return nullptr; }
};

class NimBLEClientCallbacks {
public:
    virtual ~NimBLEClientCallbacks() {}
    virtual void onConnect(NimBLEClient *client) {}
    virtual void onDisconnect(NimBLEClient *client, int reason = 0) {}
};

class NimBLEClient {
public:
    bool connect(NimBLEAdvertisedDevice *device) { return false; }
    bool connect(const NimBLEAddress &addr) { return false; }
    void disconnect() {}
    bool isConnected() { return false; }
    NimBLERemoteService *getService(const char *uuid) { return nullptr; }
    void setClientCallbacks(NimBLEClientCallbacks *callbacks) {}
    void setConnectionParams(uint16_t min, uint16_t max, uint16_t latency, uint16_t timeout) {}
};
