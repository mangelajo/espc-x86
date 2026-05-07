// native_driver_stubs.cpp - Stub implementations of ESP32 hardware drivers
//
// These replace the real hardware drivers (VGA, PS/2, Sound, Bluetooth, SD card)
// with no-op stubs so the emulator core can compile and run on a native host.
//
// IMPORTANT: Only methods that are DECLARED (not defined inline) in headers
// need implementations here.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <sys/stat.h>

// ═══════════════════════════════════════════════════════════════════════════
// PS2Device (base class)
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/input/ps2_device.h"

namespace fabgl {

PS2Device::PS2Device() : m_PS2Port(-1), m_deviceID(0) {}
PS2Device::~PS2Device() {}
void PS2Device::quickCheckHardware() {}
void PS2Device::begin(int PS2Port) { m_PS2Port = PS2Port; }
int  PS2Device::dataAvailable() { return 0; }
int  PS2Device::getData(int timeOutMS) { return -1; }
void PS2Device::requestToResendLastByte() {}
bool PS2Device::lock(int timeOutMS) { return true; }
void PS2Device::unlock() {}
bool PS2Device::sendCommand(uint8_t cmd, uint8_t expectedReply) { return true; }
void PS2Device::sendCommand(uint8_t cmd) {}
bool PS2Device::parityError() { return false; }
bool PS2Device::syncError() { return false; }
bool PS2Device::CLKTimeOutError() { return false; }
void PS2Device::suspendPort() {}
void PS2Device::resumePort() {}
bool PS2Device::send_cmdLEDs(bool numLock, bool capsLock, bool scrollLock) { return true; }
bool PS2Device::send_cmdEcho() { return true; }
bool PS2Device::send_cmdGetScancodeSet(uint8_t *result) { if (result) *result = 2; return true; }
bool PS2Device::send_cmdSetScancodeSet(uint8_t scancodeSet) { return true; }
bool PS2Device::send_cmdIdentify(PS2DeviceType *result) { if (result) *result = PS2DeviceType::UnknownPS2Device; return true; }
bool PS2Device::send_cmdDisableScanning() { return true; }
bool PS2Device::send_cmdEnableScanning() { return true; }
bool PS2Device::send_cmdTypematicRateAndDelay(int repeatRateMS, int repeatDelayMS) { return true; }
bool PS2Device::send_cmdSetSampleRate(int sampleRate) { return true; }
bool PS2Device::send_cmdSetDefaultParams() { return true; }
bool PS2Device::send_cmdReset() { return true; }
bool PS2Device::send_cmdSetResolution(int resolution) { return true; }
bool PS2Device::send_cmdSetScaling(int scaling) { return true; }
// parityError/syncError/CLKTimeOutError are inline in header

} // namespace fabgl


// ═══════════════════════════════════════════════════════════════════════════
// PS2Controller
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/input/ps2_controller.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/mouse.h"

namespace fabgl {

// Static members
PS2Controller  *PS2Controller::s_instance          = nullptr;
Keyboard       *PS2Controller::s_keyboard          = nullptr;
Mouse          *PS2Controller::s_mouse             = nullptr;
bool            PS2Controller::s_keyboardAllocated  = false;
bool            PS2Controller::s_mouseAllocated     = false;
bool            PS2Controller::s_portEnabled[2]     = {};
intr_handle_t   PS2Controller::s_ULPWakeISRHandle   = nullptr;
bool            PS2Controller::s_parityError[2]     = {};
bool            PS2Controller::s_syncError[2]       = {};
bool            PS2Controller::s_CLKTimeOutError[2] = {};

bool PS2Controller::s_initDone = false;

PS2Controller::PS2Controller() {}
PS2Controller::~PS2Controller() {}

void PS2Controller::begin(gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t) {
    if (!s_instance) s_instance = new PS2Controller();
    if (!s_keyboard) { s_keyboard = new Keyboard(); s_keyboardAllocated = true; }
    if (!s_mouse)    { s_mouse = new Mouse();       s_mouseAllocated = true; }
}
void PS2Controller::begin(PS2Preset preset, KbdMode keyboardMode) {
    begin(GPIO_UNUSED, GPIO_UNUSED, GPIO_UNUSED, GPIO_UNUSED);
}
void PS2Controller::end() {}
// initialized(), keyboard(), setKeyboard(), mouse(), setMouse(), instance(),
// parityError(), syncError(), CLKTimeOutError() are all inline in header
bool PS2Controller::dataAvailable(int PS2Port) { return false; }
int  PS2Controller::getData(int PS2Port, int timeOutMS) { return -1; }
void PS2Controller::sendData(uint8_t data, int PS2Port) {}
void PS2Controller::disableRX(int PS2Port) {}
void PS2Controller::enableRX(int PS2Port) {}
bool PS2Controller::lock(int PS2Port, int timeOutMS) { return true; }
void PS2Controller::unlock(int PS2Port) {}
void PS2Controller::ULPWakeISR(void *arg) {}


// ═══════════════════════════════════════════════════════════════════════════
// Keyboard - only methods NOT defined inline in keyboard.h
// ═══════════════════════════════════════════════════════════════════════════

Keyboard::Keyboard()
    : m_scancodeSet(2), m_lastDeadKey(VirtualKey::VK_NONE) {}
Keyboard::~Keyboard() {}
void Keyboard::begin(gpio_num_t, gpio_num_t, bool, bool) {}
void Keyboard::begin(bool, bool, int) {}
void Keyboard::enableVirtualKeys(bool, bool) {}
bool Keyboard::reset() { return true; }
// isKeyboardAvailable() - inline
void Keyboard::setLayout(KeyboardLayout const *layout) { m_layout = layout; }
// getLayout() - inline
bool Keyboard::isVKDown(VirtualKey) { return false; }
int  Keyboard::virtualKeyAvailable() { return 0; }
VirtualKey Keyboard::getNextVirtualKey(bool *keyDown, int timeOutMS) { return VirtualKey::VK_NONE; }
bool Keyboard::getNextVirtualKey(VirtualKeyItem *item, int timeOutMS) { return false; }
void Keyboard::injectVirtualKey(VirtualKey, bool, bool) {}
void Keyboard::injectVirtualKey(VirtualKeyItem const &, bool) {}
void Keyboard::emptyVirtualKeyQueue() {}
int  Keyboard::virtualKeyToASCII(VirtualKey) { return 0; }
// setCodePage() - inline
int  Keyboard::scancodeAvailable() { return 0; }
int  Keyboard::getNextScancode(int timeOutMS, bool requestResendOnTimeOut) { return -1; }
bool Keyboard::setLEDs(bool numLock, bool capsLock, bool scrollLock) { return true; }
void Keyboard::getLEDs(bool *numLock, bool *capsLock, bool *scrollLock) {
    if (numLock) *numLock = false;
    if (capsLock) *capsLock = false;
    if (scrollLock) *scrollLock = false;
}
// setTypematicRateAndDelay() - inline, calls send_cmd
bool Keyboard::setScancodeSet(int value) { m_scancodeSet = value; return true; }
// scancodeSet() - inline

// Scancode set 2 → set 1 translation
uint8_t Keyboard::convScancodeSet2To1(uint8_t code) {
    struct Entry { uint8_t from, to; };
    static const Entry entries[] = {
        {0x01,0x43},{0x03,0x3F},{0x04,0x3D},{0x05,0x3B},{0x06,0x3C},
        {0x07,0x58},{0x09,0x44},{0x0A,0x42},{0x0B,0x40},{0x0C,0x3E},
        {0x0D,0x0F},{0x0E,0x29},{0x11,0x38},{0x12,0x2A},{0x14,0x1D},
        {0x15,0x10},{0x16,0x02},{0x1A,0x2C},{0x1B,0x1F},{0x1C,0x1E},
        {0x1D,0x20},{0x1E,0x12},{0x21,0x2E},{0x22,0x2D},{0x23,0x22},
        {0x24,0x21},{0x25,0x24},{0x26,0x13},{0x29,0x39},{0x2A,0x30},
        {0x2B,0x2F},{0x2C,0x23},{0x2D,0x25},{0x2E,0x15},{0x31,0x32},
        {0x32,0x31},{0x33,0x26},{0x34,0x27},{0x35,0x28},{0x36,0x14},
        {0x3A,0x33},{0x3B,0x34},{0x3C,0x35},{0x3D,0x17},{0x3E,0x08},
        {0x41,0x36},{0x42,0x37},{0x43,0x16},{0x44,0x18},{0x45,0x0B},
        {0x46,0x09},{0x49,0x34},{0x4A,0x35},{0x4B,0x19},{0x4C,0x27},
        {0x4D,0x1A},{0x4E,0x0C},{0x52,0x28},{0x54,0x1A},{0x55,0x0D},
        {0x58,0x3A},{0x59,0x36},{0x5A,0x1C},{0x5B,0x1B},{0x5D,0x2B},
        {0x61,0x56},{0x66,0x0E},{0x69,0x4F},{0x6B,0x4B},{0x6C,0x47},
        {0x70,0x52},{0x71,0x53},{0x72,0x50},{0x74,0x4D},{0x75,0x48},
        {0x76,0x01},{0x77,0x45},{0x78,0x57},{0x79,0x4E},{0x7A,0x51},
        {0x7B,0x4A},{0x7C,0x37},{0x7D,0x49},{0x7E,0x46},{0x83,0x41},
    };
    static uint8_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (auto const & e : entries) table[e.from] = e.to;
        initialized = true;
    }
    return table[code];
}

#if FABGLIB_HAS_VirtualKeyO_STRING
char const *Keyboard::virtualKeyToString(VirtualKey) { return ""; }
#endif


// ═══════════════════════════════════════════════════════════════════════════
// Mouse - only methods NOT defined inline in mouse.h
// ═══════════════════════════════════════════════════════════════════════════

bool Mouse::s_quickCheckHardware = false;

Mouse::Mouse()
    : m_mouseAvailable(false), m_mouseType(LegacyMouse), m_prevDeltaTime(0) {}
Mouse::~Mouse() {}
void Mouse::begin(gpio_num_t, gpio_num_t) {}
void Mouse::begin(int PS2Port) {}
bool Mouse::reset() { return true; }
// isMouseAvailable() - inline
int  Mouse::getPacketSize() { return 3; }
bool Mouse::packetAvailable() { return false; }
bool Mouse::getNextPacket(MousePacket *, int, bool) { return false; }
bool Mouse::deltaAvailable() { return false; }
bool Mouse::getNextDelta(MouseDelta *, int, bool) { return false; }
// setSampleRate(), setResolution(), setScaling() - inline, call send_cmd
void Mouse::setupAbsolutePositioner(int, int, bool, BitmappedDisplayController *) {}
void Mouse::terminateAbsolutePositioner() {}
void Mouse::updateAbsolutePosition(MouseDelta *) {}
// status() - inline
int  Mouse::availableStatus() { return 0; }
MouseStatus Mouse::getNextStatus(int timeOutMS) { return MouseStatus(); }
void Mouse::emptyQueue() {}
// quickCheckHardware() - inline

} // namespace fabgl


// ═══════════════════════════════════════════════════════════════════════════
// Sound Generator
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/audio/sound_generator.h"

namespace fabgl {

// WaveformGenerator ctor/dtor are inline in the header

SineWaveformGenerator::SineWaveformGenerator() : m_phaseInc(0), m_phaseAcc(0), m_frequency(0) {}
void SineWaveformGenerator::setFrequency(int value) { m_frequency = value; }
int  SineWaveformGenerator::getSample() { return 0; }

SoundGenerator::SoundGenerator(int sampleRate, gpio_num_t gpio, SoundGenMethod genMethod)
    : m_channels(nullptr), m_volume(100), m_sampleRate(sampleRate),
      m_play(false), m_gpio(gpio), m_isr_handle(nullptr), m_DMAChain(nullptr),
      m_genMethod(genMethod), m_initDone(false), m_timerHandle(nullptr) {
    m_sampleBuffer[0] = m_sampleBuffer[1] = nullptr;
#ifdef FABGL_EMULATED
    m_device = 0;
#endif
}
SoundGenerator::~SoundGenerator() {}
void SoundGenerator::clear() {}
bool SoundGenerator::play(bool value) { m_play = value; return true; }
// playing() - inline
void SoundGenerator::attach(WaveformGenerator *value) {
    if (value) { value->next = m_channels; m_channels = value; value->setSampleRate(m_sampleRate); }
}
void SoundGenerator::detach(WaveformGenerator *value) {
    if (!value) return;
    if (m_channels == value) { m_channels = value->next; return; }
    for (auto ch = m_channels; ch; ch = ch->next)
        if (ch->next == value) { ch->next = value->next; return; }
}
// setVolume(), volume() - inline
int  SoundGenerator::getSample() { return 0; }
void SoundGenerator::init() {}
void SoundGenerator::setDMANode(int, volatile uint16_t *, int) {}
int  SoundGenerator::calcI2STimingParams(int, int *, int *, int *, int *) { return 0; }

#ifdef FABGL_EMULATED
void SoundGenerator::sdl_init() {}
void SoundGenerator::SDLAudioCallback(void *, Uint8 *, int) {}
#endif

} // namespace fabgl


// ═══════════════════════════════════════════════════════════════════════════
// VGA Direct Controller / BitmappedDisplayController
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/video/vga_direct.h"

namespace fabgl {

int BitmappedDisplayController::queueSize = 1024;
VGADirectController *VGADirectController::s_instance = nullptr;

bool VGADirectController::s_VSync = false;

VGADirectController::VGADirectController(bool autoRun) { s_instance = this; }
void VGADirectController::setResolution(VGATimings const &, int, int, bool) {}
void VGADirectController::readScreen(Rect const &, RGB888 *) {}
// setDrawScanlineCallback(), setScanlinesPerCallBack(), VSync(), nativePixelFormat() - inline
void VGADirectController::run() {}
void VGADirectController::setScanlineBuffer(int, uint8_t volatile *) {}
uint8_t volatile *VGADirectController::getScanlineBuffer(int) { static uint8_t buf[1024]={}; return buf; }
uint8_t volatile *VGADirectController::getDefaultScanlineBuffer(int s) { return getScanlineBuffer(s); }

void VGADirectController::onSetupDMABuffer(lldesc_t volatile *, bool, int, bool, int) {}
void VGADirectController::rawDrawBitmap_Native(int, int, Bitmap const *, int, int, int, int) {}
void VGADirectController::rawDrawBitmap_Mask(int, int, Bitmap const *, void *, int, int, int, int) {}
void VGADirectController::rawDrawBitmap_RGBA2222(int, int, Bitmap const *, void *, int, int, int, int) {}
void VGADirectController::rawDrawBitmap_RGBA8888(int, int, Bitmap const *, void *, int, int, int, int) {}

LightMemoryPool::LightMemoryPool(int poolSize) : m_poolSize(0), m_mem(nullptr) {}
LightMemoryPool::~LightMemoryPool() {}
void *LightMemoryPool::alloc(int size) { return nullptr; }
bool LightMemoryPool::memCheck() { return true; }
int  LightMemoryPool::totFree() { return 0; }
int  LightMemoryPool::totAllocated() { return 0; }
int  LightMemoryPool::largestFree() { return 0; }
void LightMemoryPool::mark(int pos, int16_t size, bool allocated) {}

Sprite::Sprite() {}
Sprite::~Sprite() {}

BitmappedDisplayController::BitmappedDisplayController() : m_primDynMemPool(0) {}
BitmappedDisplayController::~BitmappedDisplayController() {}
bool BitmappedDisplayController::suspendDoubleBuffering(bool) { return false; }

// VGABaseController
VGABaseController::VGABaseController() {}
void VGABaseController::begin() {}
void VGABaseController::end() {}
void VGABaseController::setResolution(char const *, int, int, bool) {}
void VGABaseController::setResolution(VGATimings const &, int, int, bool) {}
void VGABaseController::init() {}
void VGABaseController::freeViewPort() {}
bool VGABaseController::suspendDoubleBuffering(bool) { return false; }
void VGABaseController::swapBuffers() {}
void VGABaseController::resumeBackgroundPrimitiveExecution() {}
void VGABaseController::suspendBackgroundPrimitiveExecution() {}

// VGADirectController methods not yet stubbed
void VGADirectController::init() {}
void VGADirectController::allocateViewPort() {}
void VGADirectController::freeViewPort() {}
void VGADirectController::clear(Rect &) {}
void VGADirectController::setPixelAt(PixelDesc const &, Rect &) {}
void VGADirectController::absDrawLine(int, int, int, int, RGB888) {}
void VGADirectController::rawFillRow(int, int, int, RGB888) {}
void VGADirectController::drawEllipse(Size const &, Rect &) {}
void VGADirectController::drawGlyph(Glyph const &, GlyphOptions, RGB888, RGB888, Rect &) {}
void VGADirectController::invertRect(Rect const &, Rect &) {}
void VGADirectController::swapFGBG(Rect const &, Rect &) {}
void VGADirectController::copyRect(Rect const &, Rect &) {}
void VGADirectController::HScroll(int, Rect &) {}
void VGADirectController::VScroll(int, Rect &) {}

} // namespace fabgl


// VideoScanout: real implementation is built from src/video/video_scanout.cpp
// (added to build_src_filter for the native env). No stubs needed here.


// ═══════════════════════════════════════════════════════════════════════════
// MCP23S17 - only non-inline methods
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/comm/mcp23S17.h"

namespace fabgl {

MCP23S17::MCP23S17() : m_SPIDevHandle(nullptr) {}
MCP23S17::~MCP23S17() {}
bool MCP23S17::begin(int, int, int, int, int, int) { return false; }
void MCP23S17::end() {}
// available() - inline
bool MCP23S17::initDevice(uint8_t) { return false; }
void MCP23S17::writeReg(uint8_t, uint8_t, uint8_t) {}
uint8_t MCP23S17::readReg(uint8_t, uint8_t) { return 0; }
void MCP23S17::writeReg16(uint8_t, uint16_t, uint8_t) {}
uint16_t MCP23S17::readReg16(uint8_t, uint8_t) { return 0; }
void MCP23S17::enableINTMirroring(bool, uint8_t) {}
void MCP23S17::enableINTOpenDrain(bool, uint8_t) {}
void MCP23S17::setINTActiveHigh(bool value, uint8_t) {}
// getINTActiveHigh, setPortDir, getPortDir, setPortInputPolarity,
// enablePortPullUp, getPortPullUp, writePort, readPort, writePort16, readPort16
// are all inline in the header (they call writeReg/readReg)
void MCP23S17::configureGPIO(int, MCPDir, bool, uint8_t) {}
void MCP23S17::writeGPIO(int, bool, uint8_t) {}
bool MCP23S17::readGPIO(int, uint8_t) { return false; }
void MCP23S17::enableInterrupt(int, MCPIntTrigger, bool, uint8_t) {}
void MCP23S17::disableInterrupt(int, uint8_t) {}
// getPortIntFlags, getPortIntCaptured - inline
void MCP23S17::writePort(int, void const *, size_t, uint8_t) {}
void MCP23S17::readPort(int, void *, size_t, uint8_t) {}

} // namespace fabgl


// ═══════════════════════════════════════════════════════════════════════════
// SerialPort - only non-inline methods
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/comm/serial_port.h"

namespace fabgl {

SerialPort::SerialPort() : m_initialized(false) {}
void SerialPort::setCallbacks(void *, RXReadyCallback, RXCallback, LineStatusCallback) {}
void SerialPort::setSignals(int, int, int, int, int, int, int, int) {}
void SerialPort::setup(int, uint32_t, int, char, float, FlowControl, bool) {}
void SerialPort::setBaud(int) {}
void SerialPort::setFrame(int, char, float) {}
void SerialPort::flowControl(bool) {}
bool SerialPort::readyToSend() { return true; }
// readyToReceive(), initialized(), CTSStatus(), RTSStatus(), DTRStatus(),
// DSRStatus(), DCDStatus(), RIStatus(), parityError(), framingError(),
// overflowError(), breakDetected() - all inline
void SerialPort::setRTSStatus(bool) {}
void SerialPort::setDTRStatus(bool) {}
void SerialPort::send(uint8_t) {}
void SerialPort::sendBreak(bool) {}

} // namespace fabgl


// ═══════════════════════════════════════════════════════════════════════════
// Bluetooth Gamepad
// ═══════════════════════════════════════════════════════════════════════════

#include "drivers/joystick/gamepad.h"
#include "drivers/joystick/bt_driver.h"

// BTGamepad ctor, setAxis, setButton are all inline in the header

BTGamepadDriver *BTGamepadDriver::s_instance = nullptr;
BTGamepadDriver::BTGamepadDriver(BTGamepad *gp) : m_gp(gp), m_client(nullptr), m_char(nullptr), m_connected(false) {
    s_instance = this;
}
void BTGamepadDriver::begin() {}
void BTGamepadDriver::startScan() {}
void BTGamepadDriver::onConnect(NimBLEClient *) {}
void BTGamepadDriver::onDisconnect(NimBLEClient *, int) {}
void BTGamepadDriver::onResult(const NimBLEAdvertisedDevice *) {}
void BTGamepadDriver::notifyCallback(NimBLERemoteCharacteristic *, uint8_t *, size_t, bool) {}
void BTGamepadDriver::connectTo(const NimBLEAdvertisedDevice *) {}
void BTGamepadDriver::parseHID(const uint8_t *, size_t) {}


// ═══════════════════════════════════════════════════════════════════════════
// SD Card / Host / Settings / Snapshot / VFS / Unzip
// ═══════════════════════════════════════════════════════════════════════════

#include "host/sdcard.h"
#include "host/settings.h"
#include "host/snapshot.h"
#include "host/vfs_fat.h"
#include "host/unzip.h"

int sdcard_mount(const char *mount_point) {
    printf("[native] sdcard_mount(%s) - creating native SD directory\n", mount_point);

    struct stat st = {};
    if (stat(mount_point, &st) == -1) {
        if (mkdir(mount_point, 0775) != 0) {
            printf("[native] ERROR: Could not create SD directory %s (errno: %d)\n", mount_point, errno);
            return SDCARD_ERROR;
        }
        printf("[native] Created SD card directory: %s\n", mount_point);
    }

    return SDCARD_OK;
}
int sdcard_umount() {
    printf("[native] sdcard_umount() - keeping SD directory intact\n");
    return 0;
}

Settings::Settings(Computer *computer) : m_computer(computer) {}
void Settings::show() { printf("[native] Settings::show() - not available\n"); }
void Settings::mountFloppy() { printf("[native] mountFloppy() - not available\n"); }
void Settings::mountHardDisk() { printf("[native] mountHardDisk() - not available\n"); }

int snapshot(uint16_t width, uint16_t height, uint8_t *src, const char *path) {
    printf("[native] snapshot %dx%d → %s (stub)\n", width, height, path);
    return 0;
}

int vfs_fat_create_image(char *img_filename, const char *mount_point, bool floppy) {
    size_t size = floppy ? VFS_FAT_FLOPPY_SIZE : (VFS_FAT_SIZE * 1024ULL * 1024);

    FILE *f = fopen(img_filename, "wb");
    if (!f) {
        printf("[native] vfs_fat_create_image: Could not create %s\n", img_filename);
        return -1;
    }

    // Create the file and write a zero at the last byte to set the size
    if (fseek(f, size - 1, SEEK_SET) != 0) {
        printf("[native] vfs_fat_create_image: fseek failed on %s\n", img_filename);
        fclose(f);
        return -1;
    }
    fputc(0, f);
    fclose(f);

    printf("[native] Created %s disk image: %s (%zu bytes)\n",
           floppy ? "floppy" : "HDD", img_filename, size);

    return 0;
}
int vfs_fat_unmount_image(const char *) { return 0; }
int vfs_fat_check_image(char *, char *) { return -1; }

int unzip_file_to_path(char *, const char *, unzip_progress_callback_t, void *) { return -1; }
