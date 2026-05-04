// Arduino stub for native builds
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Arduino basic types
typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

// Pin modes
#define INPUT   0
#define OUTPUT  1
#define INPUT_PULLUP 2

#define HIGH 1
#define LOW  0

#define LED_BUILTIN 2

inline void pinMode(int pin, int mode) {}
inline void digitalWrite(int pin, int value) {}
inline int  digitalRead(int pin) { return 0; }
inline void delay(unsigned long ms);  // implemented in native_stubs.cpp
inline unsigned long millis();        // implemented in native_stubs.cpp
inline unsigned long micros();        // implemented in native_stubs.cpp

// Serial stub
class HardwareSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void end() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t c) { return fputc(c, stdout) != EOF ? 1 : 0; }
    size_t write(const uint8_t *buf, size_t size) { return fwrite(buf, 1, size, stdout); }
    void print(const char *s) { fputs(s, stdout); }
    void println(const char *s) { puts(s); }
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void flush() { fflush(stdout); }
};

extern HardwareSerial Serial;
