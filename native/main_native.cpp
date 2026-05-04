// main_native.cpp - Native (desktop) entry point for ESPC-x86
//
// This builds the full emulator core (CPU, chipset, BIOS, video adapters)
// on the host for testing without ESP32 hardware.
//
// Excluded from test builds via pio_extra.py since tests provide their own main().
// Usage:
//   cd native && mkdir -p build && cd build
//   cmake .. && make -j$(nproc)
//   ./espc-x86-native [--steps N] [--base-dir PATH]
//
// The emulator will boot with the GLaBIOS ROM, initialize the full PC/XT
// chipset, and execute N CPU steps (default: 1000000).

#include "computer.h"
#include "setup.h"

#include "host/sdcard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <thread>
#include <chrono>

#include "core/i8086.h"

using fabgl::i8086;

static Computer *computer = nullptr;
static volatile bool g_running = true;

static void signal_handler(int sig) {
    printf("\nCaught signal %d - stopping emulation\n", sig);
    g_running = false;
}

static void print_cpu_state() {
    printf("\n=== CPU State ===\n");
    printf(" CS=%04X  DS=%04X  ES=%04X  SS=%04X\n",
           i8086::CS(), i8086::DS(), i8086::ES(), i8086::SS());
    printf(" IP=%04X  AX=%04X  BX=%04X  CX=%04X  DX=%04X\n",
           i8086::IP(), i8086::AX(), i8086::BX(), i8086::CX(), i8086::DX());
    printf(" SI=%04X  DI=%04X  BP=%04X  SP=%04X\n",
           i8086::SI(), i8086::DI(), i8086::BP(), i8086::SP());
    printf(" Flags: O=%d D=%d I=%d T=%d S=%d Z=%d A=%d P=%d C=%d\n",
           i8086::flagOF(), i8086::flagDF(), i8086::flagIF(), i8086::flagTF(),
           i8086::flagSF(), i8086::flagZF(), i8086::flagAF(), i8086::flagPF(), i8086::flagCF());
    printf(" CS:IP = %05X    Halted = %s\n",
           i8086::CS() * 16 + i8086::IP(),
           i8086::halted() ? "yes" : "no");
    printf("=================\n\n");
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);  // Disable stdout buffering
    printf("ESPC-x86 Native Test Build\n");
    printf("==========================\n\n");

    signal(SIGINT, signal_handler);

    // Parse arguments
    uint64_t maxSteps = 1000000;
    const char *baseDir = ".";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            maxSteps = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--base-dir") == 0 && i + 1 < argc) {
            baseDir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--steps N] [--base-dir PATH]\n", argv[0]);
            printf("  --steps N       Number of CPU steps to execute (default: 1000000)\n");
            printf("  --base-dir PATH Path to disk images directory\n");
            return 0;
        }
    }

    printf("Max steps: %llu\n", (unsigned long long)maxSteps);
    printf("Base dir:  %s\n\n", baseDir);

    // Create and configure the computer
    sdcard_mount(SD_MOUNT_PATH);

    computer = new Computer();
    computer->setBaseDirectory(baseDir);

    // The Computer::run() method creates a FreeRTOS task (now a std::thread).
    // For native testing, we call init() + reset() and step manually.
    // But run() will work too since our stubs handle threads.

    printf("Starting emulation...\n");
    printf("(Press Ctrl+C to stop and dump CPU state)\n\n");

    // Let the computer run in its own thread
    computer->run();

    printf("[native] Entering monitor loop...\n");

    // Monitor loop - print status periodically
    uint64_t lastTicks = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint32_t ticks = computer->ticksCounter();
        uint32_t delta = ticks - lastTicks;
        lastTicks = ticks;

        printf("[native] %u ticks/sec (total: %u, halted: %s)\n",
               delta, ticks, i8086::halted() ? "yes" : "no");

        if (ticks == 0 && delta == 0) {
            printf("[native] CPU CS:IP = %04X:%04X\n", i8086::CS(), i8086::IP());
        }

        if (maxSteps > 0 && ticks >= maxSteps) {
            printf("\nReached %llu steps - stopping\n", (unsigned long long)maxSteps);
            break;
        }
    }

    print_cpu_state();

    printf("Native test completed.\n");
    delete computer;
    return 0;
}
