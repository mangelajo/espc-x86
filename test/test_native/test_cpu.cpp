// test_cpu.cpp — Native unit tests for the 8086 CPU core
//
// Run with: pio test -e native

#include <unity.h>
#include <cstring>

#include "core/i8086.h"

using fabgl::i8086;

// 1 MB RAM for the test CPU
static uint8_t test_ram[1048576];

// Minimal callbacks — no I/O, no video, no interrupts
static void     test_writePort(void *, int, uint8_t) {}
static uint8_t  test_readPort(void *, int)   { return 0xFF; }
static void     test_writeVMem8(void *, int, uint8_t) {}
static void     test_writeVMem16(void *, int, uint16_t) {}
static uint8_t  test_readVMem8(void *, int)  { return 0xFF; }
static uint16_t test_readVMem16(void *, int) { return 0xFFFF; }
static bool     test_interrupt(void *, int)  { return false; }

static void cpu_init() {
    memset(test_ram, 0, sizeof(test_ram));
    i8086::setCallbacks(nullptr,
        test_readPort, test_writePort,
        test_writeVMem8, test_writeVMem16,
        test_readVMem8, test_readVMem16,
        test_interrupt);
    i8086::setMemory(test_ram);
    i8086::reset();
}

// Helper: write code at CS:IP and step
static void write_code(const uint8_t *code, size_t len) {
    uint32_t addr = i8086::CS() * 16 + i8086::IP();
    memcpy(test_ram + addr, code, len);
}

// ── Tests ───────────────────────────────────────────────────────────────────

void test_cpu_reset_state() {
    cpu_init();
    // After reset, CS=FFFF IP=0000 (8086 reset vector)
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, i8086::CS());
    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::IP());
    TEST_ASSERT_FALSE(i8086::halted());
}

void test_cpu_nop() {
    cpu_init();
    // NOP = 0x90
    uint8_t code[] = { 0x90 };
    write_code(code, sizeof(code));
    uint16_t ip_before = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip_before + 1, i8086::IP());
}

void test_cpu_mov_ax_imm16() {
    cpu_init();
    // MOV AX, 0x1234 = B8 34 12
    uint8_t code[] = { 0xB8, 0x34, 0x12 };
    write_code(code, sizeof(code));
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(0x1234, i8086::AX());
}

void test_cpu_mov_bx_imm16() {
    cpu_init();
    // MOV BX, 0xABCD = BB CD AB
    uint8_t code[] = { 0xBB, 0xCD, 0xAB };
    write_code(code, sizeof(code));
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(0xABCD, i8086::BX());
}

void test_cpu_add_ax_bx() {
    cpu_init();
    // MOV AX, 0x0010 ; MOV BX, 0x0020 ; ADD AX, BX
    uint8_t code[] = {
        0xB8, 0x10, 0x00,  // MOV AX, 0x0010
        0xBB, 0x20, 0x00,  // MOV BX, 0x0020
        0x01, 0xD8,        // ADD AX, BX
    };
    write_code(code, sizeof(code));
    i8086::step(); // MOV AX
    i8086::step(); // MOV BX
    i8086::step(); // ADD AX, BX
    TEST_ASSERT_EQUAL_HEX16(0x0030, i8086::AX());
    TEST_ASSERT_FALSE(i8086::flagZF());
    TEST_ASSERT_FALSE(i8086::flagCF());
}

void test_cpu_add_overflow() {
    cpu_init();
    // MOV AX, 0xFFFF ; ADD AX, 1
    uint8_t code[] = {
        0xB8, 0xFF, 0xFF,  // MOV AX, 0xFFFF
        0x05, 0x01, 0x00,  // ADD AX, 0x0001
    };
    write_code(code, sizeof(code));
    i8086::step();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::AX());
    TEST_ASSERT_TRUE(i8086::flagZF());
    TEST_ASSERT_TRUE(i8086::flagCF());
}

void test_cpu_sub() {
    cpu_init();
    // MOV AX, 0x0050 ; SUB AX, 0x0020
    uint8_t code[] = {
        0xB8, 0x50, 0x00,  // MOV AX, 0x0050
        0x2D, 0x20, 0x00,  // SUB AX, 0x0020
    };
    write_code(code, sizeof(code));
    i8086::step();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(0x0030, i8086::AX());
    TEST_ASSERT_FALSE(i8086::flagCF());
}

void test_cpu_jmp_short() {
    cpu_init();
    // JMP short +2 (skip next 2 bytes); DB 0xCC, 0xCC; NOP
    uint8_t code[] = { 0xEB, 0x02, 0xCC, 0xCC, 0x90 };
    write_code(code, sizeof(code));
    uint16_t ip_before = i8086::IP();
    i8086::step(); // JMP
    TEST_ASSERT_EQUAL_HEX16(ip_before + 4, i8086::IP());
}

void test_cpu_push_pop() {
    cpu_init();
    // Set up a valid stack
    i8086::setSS(0x0000);
    i8086::setSP(0x1000);

    // MOV AX, 0x4242 ; PUSH AX ; MOV AX, 0 ; POP BX
    uint8_t code[] = {
        0xB8, 0x42, 0x42,  // MOV AX, 0x4242
        0x50,              // PUSH AX
        0xB8, 0x00, 0x00,  // MOV AX, 0x0000
        0x5B,              // POP BX
    };
    write_code(code, sizeof(code));
    i8086::step(); // MOV AX
    i8086::step(); // PUSH AX
    TEST_ASSERT_EQUAL_HEX16(0x0FFE, i8086::SP());
    i8086::step(); // MOV AX, 0
    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::AX());
    i8086::step(); // POP BX
    TEST_ASSERT_EQUAL_HEX16(0x4242, i8086::BX());
    TEST_ASSERT_EQUAL_HEX16(0x1000, i8086::SP());
}

void test_cpu_xor_self() {
    cpu_init();
    // MOV AX, 0x1234 ; XOR AX, AX
    uint8_t code[] = {
        0xB8, 0x34, 0x12,  // MOV AX, 0x1234
        0x31, 0xC0,        // XOR AX, AX
    };
    write_code(code, sizeof(code));
    i8086::step();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::AX());
    TEST_ASSERT_TRUE(i8086::flagZF());
}

void test_cpu_hlt() {
    cpu_init();
    // HLT = 0xF4
    uint8_t code[] = { 0xF4 };
    write_code(code, sizeof(code));
    TEST_ASSERT_FALSE(i8086::halted());
    i8086::step();
    TEST_ASSERT_TRUE(i8086::halted());
}

// ── Main ────────────────────────────────────────────────────────────────────

extern void run_fpu_tests();

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cpu_reset_state);
    RUN_TEST(test_cpu_nop);
    RUN_TEST(test_cpu_mov_ax_imm16);
    RUN_TEST(test_cpu_mov_bx_imm16);
    RUN_TEST(test_cpu_add_ax_bx);
    RUN_TEST(test_cpu_add_overflow);
    RUN_TEST(test_cpu_sub);
    RUN_TEST(test_cpu_jmp_short);
    RUN_TEST(test_cpu_push_pop);
    RUN_TEST(test_cpu_xor_self);
    RUN_TEST(test_cpu_hlt);

    run_fpu_tests();

    return UNITY_END();
}
