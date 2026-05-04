// native_stubs.cpp - Implementations of ESP32/FreeRTOS/Arduino stubs for native builds
//
// Maps ESP32-specific APIs to standard POSIX/C++ equivalents so the emulator
// core can be compiled and tested on a desktop host (macOS / Linux).

#include <chrono>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdarg>

#include "freertos/FreeRTOS.h"

// ── ESP timer ───────────────────────────────────────────────────────────────

static auto g_startTime = std::chrono::steady_clock::now();

extern "C" int64_t esp_timer_get_time(void) {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - g_startTime).count();
}

#include "esp_timer.h"

struct esp_timer_impl {
    esp_timer_cb_t callback;
    void          *arg;
    uint64_t       period_us;
    bool           active;
    bool           periodic;
    std::thread   *thread;
};

esp_err_t esp_timer_init(void) { return ESP_OK; }

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    auto t = new esp_timer_impl();
    t->callback = args->callback;
    t->arg      = args->arg;
    t->active   = false;
    t->periodic = false;
    t->thread   = nullptr;
    *out = (esp_timer_handle_t)t;
    return ESP_OK;
}

static void timer_thread_func(esp_timer_impl *t) {
    while (t->active) {
        std::this_thread::sleep_for(std::chrono::microseconds(t->period_us));
        if (t->active && t->callback)
            t->callback(t->arg);
        if (!t->periodic)
            break;
    }
    t->active = false;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t handle, uint64_t period_us) {
    auto t = (esp_timer_impl *)handle;
    t->period_us = period_us;
    t->periodic  = true;
    t->active    = true;
    t->thread    = new std::thread(timer_thread_func, t);
    t->thread->detach();
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
    auto t = (esp_timer_impl *)handle;
    t->period_us = timeout_us;
    t->periodic  = false;
    t->active    = true;
    t->thread    = new std::thread(timer_thread_func, t);
    t->thread->detach();
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t handle) {
    auto t = (esp_timer_impl *)handle;
    t->active = false;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t handle) {
    auto t = (esp_timer_impl *)handle;
    t->active = false;
    // Give thread time to exit
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    delete t;
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t handle) {
    auto t = (esp_timer_impl *)handle;
    return t->active;
}

// ── FreeRTOS tasks ──────────────────────────────────────────────────────────

void vTaskDelay(uint32_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

void vTaskDelete(TaskHandle_t handle) {
    // In native builds, tasks are std::threads - deletion is a no-op
    // (the thread detaches on creation)
}

int xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stackSize,
                            void *param, int priority, TaskHandle_t *handle, int core) {
    auto t = new std::thread(fn, param);
    t->detach();
    if (handle)
        *handle = (TaskHandle_t)t;
    return 1; // pdPASS
}

// ── FreeRTOS semaphores ─────────────────────────────────────────────────────

struct native_mutex {
    std::mutex mtx;
};

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return (SemaphoreHandle_t)(new native_mutex());
}

SemaphoreHandle_t xSemaphoreCreateBinary() {
    return xSemaphoreCreateMutex();
}

int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t timeout) {
    if (!sem) return 0;
    auto m = (native_mutex *)sem;
    m->mtx.lock();
    return 1;
}

int xSemaphoreGive(SemaphoreHandle_t sem) {
    if (!sem) return 0;
    auto m = (native_mutex *)sem;
    m->mtx.unlock();
    return 1;
}

void vSemaphoreDelete(SemaphoreHandle_t sem) {
    delete (native_mutex *)sem;
}

// ── FreeRTOS queues ─────────────────────────────────────────────────────────

struct native_queue {
    uint8_t *buffer;
    uint32_t itemSize;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    std::mutex mtx;
};

QueueHandle_t xQueueCreate(uint32_t length, uint32_t itemSize) {
    auto q = new native_queue();
    q->buffer   = new uint8_t[length * itemSize];
    q->itemSize = itemSize;
    q->capacity = length;
    q->head     = 0;
    q->count    = 0;
    return (QueueHandle_t)q;
}

int xQueueSend(QueueHandle_t queue, const void *item, uint32_t timeout) {
    return xQueueSendToBack(queue, item, timeout);
}

int xQueueSendToBack(QueueHandle_t queue, const void *item, uint32_t timeout) {
    if (!queue) return 0;
    auto q = (native_queue *)queue;
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->count >= q->capacity) return 0;
    uint32_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->buffer + tail * q->itemSize, item, q->itemSize);
    q->count++;
    return 1;
}

int xQueueSendToFront(QueueHandle_t queue, const void *item, uint32_t timeout) {
    if (!queue) return 0;
    auto q = (native_queue *)queue;
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->count >= q->capacity) return 0;
    q->head = (q->head == 0) ? q->capacity - 1 : q->head - 1;
    memcpy(q->buffer + q->head * q->itemSize, item, q->itemSize);
    q->count++;
    return 1;
}

int xQueueReceive(QueueHandle_t queue, void *item, uint32_t timeout) {
    if (!queue) return 0;
    auto q = (native_queue *)queue;
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->count == 0) return 0;
    memcpy(item, q->buffer + q->head * q->itemSize, q->itemSize);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return 1;
}

int xQueuePeek(QueueHandle_t queue, void *item, uint32_t timeout) {
    if (!queue) return 0;
    auto q = (native_queue *)queue;
    std::lock_guard<std::mutex> lock(q->mtx);
    if (q->count == 0) return 0;
    memcpy(item, q->buffer + q->head * q->itemSize, q->itemSize);
    return 1;
}

int uxQueueMessagesWaiting(QueueHandle_t queue) {
    if (!queue) return 0;
    auto q = (native_queue *)queue;
    std::lock_guard<std::mutex> lock(q->mtx);
    return q->count;
}

void vQueueDelete(QueueHandle_t queue) {
    if (!queue) return;
    auto q = (native_queue *)queue;
    delete[] q->buffer;
    delete q;
}

// ── FreeRTOS timers ─────────────────────────────────────────────────────────

TimerHandle_t xTimerCreate(const char *name, uint32_t period, int autoReload,
                           void *timerId, TimerCallbackFunction_t callback) {
    return nullptr; // stub
}

int xTimerStart(TimerHandle_t timer, uint32_t timeout) { return 1; }
int xTimerStop(TimerHandle_t timer, uint32_t timeout) { return 1; }
int xTimerDelete(TimerHandle_t timer, uint32_t timeout) { return 1; }
int xTimerReset(TimerHandle_t timer, uint32_t timeout) { return 1; }
void *pvTimerGetTimerID(TimerHandle_t timer) { return nullptr; }

// ── FRC1 Timer (used by i8253_pit for high-resolution timing) ───────────────

static uint32_t frc_regs[16] = {};

void REG_WRITE(int reg, uint32_t value) {
    if (reg >= 0 && reg < 16)
        frc_regs[reg] = value;
}

uint32_t REG_READ(int reg) {
    // For the COUNT register, return a monotonically increasing counter
    // that simulates the 5 MHz FRC1 timer (80 MHz / 16 prescaler)
    if (reg == 1) { // FRC_TIMER_COUNT_REG(0)
        auto us = esp_timer_get_time();
        // 5 MHz = 5 ticks per microsecond, wrap at 23 bits
        return (uint32_t)((us * 5) & 0x7FFFFF);
    }
    if (reg >= 0 && reg < 16)
        return frc_regs[reg];
    return 0;
}

// ── Arduino stubs ───────────────────────────────────────────────────────────

#include "Arduino.h"

HardwareSerial Serial;

void HardwareSerial::printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000);
}

unsigned long micros() {
    return (unsigned long)esp_timer_get_time();
}

// ── ESP system stubs ────────────────────────────────────────────────────────

void esp_restart(void) {
    printf("esp_restart() called - exiting native build\n");
    exit(0);
}

// ── FABGL_EMULATED helpers ──────────────────────────────────────────────────

// Called from Computer::runTask() under FABGL_EMULATED - every CPU step
void taskEmuCheck() {
    // On native: do nothing. The CPU loop runs at full speed.
    // Yielding every step would be far too slow.
}

// ── SOC peripheral stubs (global data) ──────────────────────────────────────

#include "soc/i2s_struct.h"
i2s_dev_t I2S0;
i2s_dev_t I2S1;

#include "soc/rtc_io_periph.h"
const rtc_io_desc_t rtc_io_desc[] = {};
const int rtc_io_num_map[] = {};

#include "soc/gpio_periph.h"
const uint32_t GPIO_PIN_MUX_REG[] = {};

// ── Video mode static ───────────────────────────────────────────────────────
// hal_video.h declares this but nobody defines it
#include "drivers/hal_video.h"
VideoMode CurrentVideoMode::s_videoMode = VideoMode::None;
