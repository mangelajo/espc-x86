// NimBLE scan stub
#pragma once

#include "NimBLEAdvertisedDevice.h"

class NimBLEScanResults {
public:
    int getCount() { return 0; }
    NimBLEAdvertisedDevice getDevice(int i) { return NimBLEAdvertisedDevice(); }
};

class NimBLEScanCallbacks {
public:
    virtual ~NimBLEScanCallbacks() {}
    virtual void onResult(const NimBLEAdvertisedDevice *dev) {}
    virtual void onComplete(const NimBLEScanResults &results) {}
};

class NimBLEScan {
public:
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks *cb, bool wantDuplicates = false) {}
    void setActiveScan(bool active) {}
    void setInterval(uint16_t interval) {}
    void setWindow(uint16_t window) {}
    NimBLEScanResults start(uint32_t duration, bool is_continue = false) { return NimBLEScanResults(); }
    bool start(uint32_t duration, void (*cb)(NimBLEScanResults), bool is_continue = false) { return false; }
    void stop() {}
    void clearResults() {}
    bool isScanning() { return false; }
};
