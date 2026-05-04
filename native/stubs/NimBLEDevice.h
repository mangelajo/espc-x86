// NimBLE stub for native builds
#pragma once

#include <stdint.h>
#include <string>
#include <functional>

class NimBLEAddress {
public:
    NimBLEAddress() {}
    NimBLEAddress(const char *str) { (void)str; }
    std::string toString() const { return "00:00:00:00:00:00"; }
    bool equals(const NimBLEAddress &other) const { return false; }
    operator bool() const { return false; }
};

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteService;
class NimBLERemoteCharacteristic;
class NimBLEScan;

class NimBLEDevice {
public:
    static void init(const std::string &name) {}
    static void deinit(bool release = false) {}
    static NimBLEClient *createClient() { return nullptr; }
    static NimBLEScan *getScan() { return nullptr; }
    static void setPower(int power) {}
};
