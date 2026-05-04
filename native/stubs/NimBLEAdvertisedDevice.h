// NimBLE advertised device stub
#pragma once

#include "NimBLEDevice.h"
#include <string>

class NimBLEAdvertisedDevice {
public:
    std::string getName() const { return ""; }
    NimBLEAddress getAddress() const { return NimBLEAddress(); }
    bool haveServiceUUID() const { return false; }
    bool isAdvertisingService(const char *uuid) const { return false; }
    std::string toString() const { return ""; }
    int getRSSI() const { return 0; }
};

class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual ~NimBLEAdvertisedDeviceCallbacks() {}
    virtual void onResult(NimBLEAdvertisedDevice *device) {}
};
