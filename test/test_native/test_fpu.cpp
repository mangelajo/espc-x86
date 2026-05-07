// test_fpu.cpp — Native unit tests for the 8087 FPU dispatch.
//
// These tests drive ESC opcodes through i8086::step() and verify that the
// 8086 decoder advances IP correctly and the i8087 emulator produces the
// expected memory / register side effects.
//
// Run with: pio test -e native --filter test_native

#include <unity.h>
#include <cstring>
#include <cmath>

#include "core/i8086.h"

using fabgl::i8086;

// 1 MB RAM
static uint8_t test_ram[1048576];

static void     test_writePort(void *, int, uint8_t) {}
static uint8_t  test_readPort(void *, int)   { return 0xFF; }
static void     test_writeVMem8(void *, int, uint8_t) {}
static void     test_writeVMem16(void *, int, uint16_t) {}
static uint8_t  test_readVMem8(void *, int)  { return 0xFF; }
static uint16_t test_readVMem16(void *, int) { return 0xFFFF; }
static bool     test_interrupt(void *, int)  { return false; }

static void fpu_init() {
    memset(test_ram, 0, sizeof(test_ram));
    i8086::setCallbacks(nullptr,
        test_readPort, test_writePort,
        test_writeVMem8, test_writeVMem16,
        test_readVMem8, test_readVMem16,
        test_interrupt);
    i8086::setMemory(test_ram);
    i8086::reset();
    // Place code at a known location instead of FFFF:0000
    i8086::setCS(0x0000);
    i8086::setIP(0x0100);
    i8086::setDS(0x0000);
    i8086::setSS(0x0000);
    i8086::setSP(0x1000);
}

static void put_code(const uint8_t *code, size_t len) {
    uint32_t addr = i8086::CS() * 16 + i8086::IP();
    memcpy(test_ram + addr, code, len);
}

// Helper: read 16-bit little-endian from RAM
static uint16_t mem16(uint32_t addr) {
    return test_ram[addr] | ((uint16_t)test_ram[addr + 1] << 8);
}

// Memory readers/writers for FPU operand widths.
static void write_bytes(uint32_t addr, const void *src, size_t n) {
    memcpy(test_ram + addr, src, n);
}
static float    read_f32 (uint32_t addr) { float    v; memcpy(&v, test_ram + addr, 4); return v; }
static double   read_f64 (uint32_t addr) { double   v; memcpy(&v, test_ram + addr, 8); return v; }
static int16_t  read_i16 (uint32_t addr) { int16_t  v; memcpy(&v, test_ram + addr, 2); return v; }
static int32_t  read_i32 (uint32_t addr) { int32_t  v; memcpy(&v, test_ram + addr, 4); return v; }
static int64_t  read_i64 (uint32_t addr) { int64_t  v; memcpy(&v, test_ram + addr, 8); return v; }
static void     write_f32(uint32_t addr, float    v) { memcpy(test_ram + addr, &v, 4); }
static void     write_f64(uint32_t addr, double   v) { memcpy(test_ram + addr, &v, 8); }
static void     write_i16(uint32_t addr, int16_t  v) { memcpy(test_ram + addr, &v, 2); }
static void     write_i32(uint32_t addr, int32_t  v) { memcpy(test_ram + addr, &v, 4); }
static void     write_i64(uint32_t addr, int64_t  v) { memcpy(test_ram + addr, &v, 8); }

// Run the next n CPU instructions.
static void step_n(int n) {
    for (int i = 0; i < n; i++) i8086::step();
}

// Standard scratch addresses used by the tests.
static const uint32_t SCRATCH_A = 0x4000;
static const uint32_t SCRATCH_B = 0x4100;
static const uint32_t SCRATCH_C = 0x4200;

// ──────────────────────────────────────────────────────────────────────────
// IP advancement tests — these must pass regardless of whether FPU
// emulation is enabled. They verify the 8086 decoder agrees with Intel
// instruction-length rules for ESC opcodes.
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_ip_fninit() {
    fpu_init();
    // FNINIT = DB E3 (mod=3, reg=4, rm=3) — 2 bytes
    uint8_t code[] = { 0xDB, 0xE3 };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 2, i8086::IP());
}

void test_fpu_ip_fstsw_ax() {
    fpu_init();
    // FNSTSW AX = DF E0 (mod=3, reg=4, rm=0) — 2 bytes
    uint8_t code[] = { 0xDF, 0xE0 };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 2, i8086::IP());
}

void test_fpu_ip_fnstcw_bx() {
    fpu_init();
    // FNSTCW WORD PTR [BX] = D9 3F (mod=0, reg=7, rm=7) — 2 bytes
    uint8_t code[] = { 0xD9, 0x3F };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 2, i8086::IP());
}

void test_fpu_ip_fnstsw_bx() {
    fpu_init();
    // FNSTSW WORD PTR [BX] = DD 3F (mod=0, reg=7, rm=7) — 2 bytes
    uint8_t code[] = { 0xDD, 0x3F };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 2, i8086::IP());
}

void test_fpu_ip_fldcw_disp16() {
    fpu_init();
    // FLDCW WORD PTR [0x1234] = D9 2E 34 12 (mod=0, reg=5, rm=6) — 4 bytes
    uint8_t code[] = { 0xD9, 0x2E, 0x34, 0x12 };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 4, i8086::IP());
}

void test_fpu_ip_fadd_disp8() {
    fpu_init();
    // FADD DWORD PTR [BX+10h] = D8 47 10 (mod=1, reg=0, rm=7) — 3 bytes
    uint8_t code[] = { 0xD8, 0x47, 0x10 };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 3, i8086::IP());
}

void test_fpu_ip_fnop() {
    fpu_init();
    // FNOP = D9 D0 — 2 bytes
    uint8_t code[] = { 0xD9, 0xD0 };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 2, i8086::IP());
}

void test_fpu_ip_fwait() {
    fpu_init();
    // FWAIT = 9B — 1 byte
    uint8_t code[] = { 0x9B };
    put_code(code, sizeof(code));
    uint16_t ip = i8086::IP();
    i8086::step();
    TEST_ASSERT_EQUAL_HEX16(ip + 1, i8086::IP());
}

// ──────────────────────────────────────────────────────────────────────────
// Functional tests — verify the FPU produces the right side effects.
// These require case 69 in i8086.cpp to dispatch to the i8087.
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_fninit_then_fnstcw_writes_037F() {
    // FNINIT must leave control word = 0x037F; FNSTCW writes that 16-bit
    // value to DS:[BX]. Bytes above must remain untouched (a 16-bit store,
    // not a 64-bit one).
    fpu_init();

    uint8_t code[] = {
        0xBB, 0x00, 0x20,          // MOV BX, 0x2000
        0xDB, 0xE3,                // FNINIT
        0xD9, 0x3F,                // FNSTCW [BX]
    };
    put_code(code, sizeof(code));

    i8086::step();   // MOV BX
    i8086::step();   // FNINIT
    i8086::step();   // FNSTCW [BX]

    TEST_ASSERT_EQUAL_HEX16(0x037F, mem16(0x2000));
    TEST_ASSERT_EQUAL_HEX16(0x0000, mem16(0x2002));
    TEST_ASSERT_EQUAL_HEX16(0x0000, mem16(0x2004));
    TEST_ASSERT_EQUAL_HEX16(0x0000, mem16(0x2006));
}

void test_fpu_fninit_then_fnstsw_writes_0000() {
    // FNSTSW [BX] (DD /7) must write a single 16-bit word, not 8 bytes.
    // We detect an over-wide write by sentinel-filling the surrounding
    // memory.
    fpu_init();

    uint8_t code[] = {
        0xBB, 0x00, 0x20,          // MOV BX, 0x2000
        0xDB, 0xE3,                // FNINIT
        0xDD, 0x3F,                // FNSTSW [BX]
    };
    put_code(code, sizeof(code));

    for (int i = 0; i < 16; i++)
        test_ram[0x2000 + i] = 0xCC;

    i8086::step();   // MOV BX
    i8086::step();   // FNINIT
    i8086::step();   // FNSTSW [BX]

    TEST_ASSERT_EQUAL_HEX16(0x0000, mem16(0x2000));

    // Bytes 2..7 above the target must remain at the sentinel value.
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2002]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2003]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2004]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2005]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2006]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x2007]);
}

void test_fpu_fninit_then_fstsw_ax_returns_0() {
    // FNSTSW AX (DF E0) must copy the status word into AX.
    fpu_init();

    uint8_t code[] = {
        0xB8, 0xFF, 0xFF,          // MOV AX, 0xFFFF       ; sentinel
        0xDB, 0xE3,                // FNINIT
        0xDF, 0xE0,                // FNSTSW AX
    };
    put_code(code, sizeof(code));

    i8086::step();   // MOV AX
    i8086::step();   // FNINIT
    i8086::step();   // FNSTSW AX

    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::AX());
}

void test_fpu_fldcw_fnstcw_roundtrip() {
    // Set a non-default control word, then read it back.
    fpu_init();

    // Place 0x027F into RAM at DS:0x3000
    test_ram[0x3000] = 0x7F;
    test_ram[0x3001] = 0x02;

    uint8_t code[] = {
        0xBB, 0x00, 0x30,          // MOV BX, 0x3000
        0xD9, 0x2F,                // FLDCW  [BX]
        0xBB, 0x00, 0x40,          // MOV BX, 0x4000
        0xD9, 0x3F,                // FNSTCW [BX]
    };
    put_code(code, sizeof(code));

    for (int i = 0; i < 4; i++)
        i8086::step();

    TEST_ASSERT_EQUAL_HEX16(0x027F, mem16(0x4000));
}

void test_fpu_glabios_has_fpu_stack_safety() {
    // Reproduces GLaBIOS' HAS_FPU probe (GLABIOS.ASM line ~12909):
    //
    //   PUSH AX                  ; push sentinel
    //   MOV BX, SP               ; BX -> top of stack
    //   FNSTSW SS:[BX]           ; 16-bit store of status word
    //   POP AX                   ; AX = status word
    //
    // Stack grows down; PUSH AX lands at SS:0x0FFE, so the caller's
    // saved frame above sits at SS:0x1000+. A 64-bit FNSTSW would
    // clobber 6 bytes of that frame.
    fpu_init();

    test_ram[0x1000] = 0xAA;
    test_ram[0x1001] = 0xBB;
    test_ram[0x1002] = 0xCC;
    test_ram[0x1003] = 0xDD;
    test_ram[0x1004] = 0xEE;
    test_ram[0x1005] = 0xFF;

    uint8_t code[] = {
        0xB8, 0x34, 0x12,          // MOV AX, 0x1234
        0xDB, 0xE3,                // FNINIT
        0x50,                      // PUSH AX
        0x8B, 0xDC,                // MOV BX, SP
        0x36, 0xDD, 0x3F,          // FNSTSW SS:[BX]   (SS prefix + DD /7)
        0x58,                      // POP AX
    };
    put_code(code, sizeof(code));

    i8086::step();   // MOV AX, 0x1234
    i8086::step();   // FNINIT
    i8086::step();   // PUSH AX
    i8086::step();   // MOV BX, SP
    i8086::step();   // SS prefix (consumed as its own step in this core)
    i8086::step();   // FNSTSW SS:[BX]
    i8086::step();   // POP AX

    // After FNINIT, status word = 0x0000 → POP AX sees 0.
    TEST_ASSERT_EQUAL_HEX16(0x0000, i8086::AX());

    // Sentinel above the pushed word MUST survive intact. If FNSTSW
    // wrote 8 bytes (FSTP-m64 bug), these would all be 0x00.
    TEST_ASSERT_EQUAL_HEX8(0xAA, test_ram[0x1000]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, test_ram[0x1001]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, test_ram[0x1002]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, test_ram[0x1003]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, test_ram[0x1004]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, test_ram[0x1005]);
}

void test_fpu_fnop_does_not_swallow_fld_sti() {
    // FNOP is specifically D9 D0 (reg=2, rm=0). FLD ST(i) is D9 C0+i
    // (reg=0, rm=i). After FLDZ + FLD ST(0), no exception bits should
    // be set in the status word: bit 0 = IE, bit 6 = SF.
    fpu_init();

    uint8_t code[] = {
        0xDB, 0xE3,                // FNINIT
        0xD9, 0xEE,                // FLDZ            ; ST(0) = +0.0
        0xD9, 0xC0,                // FLD ST(0)       ; duplicate ST(0)
        0xDF, 0xE0,                // FNSTSW AX
    };
    put_code(code, sizeof(code));

    for (int i = 0; i < 4; i++)
        i8086::step();

    uint16_t ax = i8086::AX();
    TEST_ASSERT_EQUAL_HEX16(0, ax & ((1u << 0) | (1u << 6)));
}

// ──────────────────────────────────────────────────────────────────────────
// Memory load/store coverage
//   FLD m32  : D9 /0   FST m32 : D9 /2   FSTP m32 : D9 /3
//   FLD m64  : DD /0   FST m64 : DD /2   FSTP m64 : DD /3
//   FLD m80  : DB /5                     FSTP m80 : DB /7
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_fld_fst_m32_roundtrip() {
    fpu_init();
    write_f32(SCRATCH_A, 3.5f);

    uint8_t code[] = {
        0xDB, 0xE3,                 // FNINIT
        0xD9, 0x06, 0x00, 0x40,     // FLD  DWORD PTR [0x4000]
        0xD9, 0x16, 0x00, 0x41,     // FST  DWORD PTR [0x4100]
    };
    put_code(code, sizeof(code));
    step_n(3);

    TEST_ASSERT_EQUAL_FLOAT(3.5f, read_f32(SCRATCH_B));
}

void test_fpu_fld_fstp_m32_pops_stack() {
    fpu_init();
    write_f32(SCRATCH_A, -7.25f);

    uint8_t code[] = {
        0xDB, 0xE3,                 // FNINIT
        0xD9, 0x06, 0x00, 0x40,     // FLD  DWORD PTR [0x4000]
        0xD9, 0x1E, 0x00, 0x41,     // FSTP DWORD PTR [0x4100]
        0xDF, 0xE0,                 // FNSTSW AX
    };
    put_code(code, sizeof(code));
    step_n(4);

    TEST_ASSERT_EQUAL_FLOAT(-7.25f, read_f32(SCRATCH_B));
    // After FNINIT (TOP=0) + FLD (TOP=7) + FSTP (TOP=0) the stack is empty.
    // No invalid-op (IE) should be set yet.
    TEST_ASSERT_BITS_LOW((1u << 0), i8086::AX());
}

void test_fpu_fld_fst_m64_roundtrip() {
    fpu_init();
    write_f64(SCRATCH_A, 1234567.89);

    uint8_t code[] = {
        0xDB, 0xE3,                 // FNINIT
        0xDD, 0x06, 0x00, 0x40,     // FLD  QWORD PTR [0x4000]
        0xDD, 0x16, 0x00, 0x41,     // FST  QWORD PTR [0x4100]
    };
    put_code(code, sizeof(code));
    step_n(3);

    TEST_ASSERT_EQUAL_DOUBLE(1234567.89, read_f64(SCRATCH_B));
}

void test_fpu_fld_fstp_m80_roundtrip_low8() {
    // m80 is approximated as a 64-bit double in the low 8 bytes;
    // top 16 bits are zeroed on store. Round-trip preserves the low 8 bytes.
    fpu_init();
    write_f64(SCRATCH_A, 42.5);              // low 8 bytes of m80
    write_i16(SCRATCH_A + 8, 0);             // top 16 bits

    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDB, 0x2E, 0x00, 0x40,              // FLD  TBYTE PTR [0x4000]   DB /5
        0xDB, 0x3E, 0x00, 0x41,              // FSTP TBYTE PTR [0x4100]   DB /7
    };
    put_code(code, sizeof(code));
    step_n(3);

    TEST_ASSERT_EQUAL_DOUBLE(42.5, read_f64(SCRATCH_B));
    TEST_ASSERT_EQUAL_HEX16(0, mem16(SCRATCH_B + 8));
}

// ──────────────────────────────────────────────────────────────────────────
// Constants: D9 E8..EE
// ──────────────────────────────────────────────────────────────────────────

static double load_const_via_fldX_then_fst(uint8_t fld_const_modrm) {
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xD9, fld_const_modrm,               // FLDx
        0xDD, 0x16, 0x00, 0x40,              // FST  QWORD PTR [0x4000]
    };
    put_code(code, sizeof(code));
    step_n(3);
    return read_f64(SCRATCH_A);
}

void test_fpu_fld1()    { TEST_ASSERT_EQUAL_DOUBLE(1.0,                  load_const_via_fldX_then_fst(0xE8)); }
void test_fpu_fldl2t()  { TEST_ASSERT_DOUBLE_WITHIN(1e-15, log(10.0)/log(2.0), load_const_via_fldX_then_fst(0xE9)); }
void test_fpu_fldl2e()  { TEST_ASSERT_DOUBLE_WITHIN(1e-15, 1.0/log(2.0),       load_const_via_fldX_then_fst(0xEA)); }
void test_fpu_fldpi()   { TEST_ASSERT_DOUBLE_WITHIN(1e-15, M_PI,               load_const_via_fldX_then_fst(0xEB)); }
void test_fpu_fldlg2()  { TEST_ASSERT_DOUBLE_WITHIN(1e-15, log(2.0)/log(10.0), load_const_via_fldX_then_fst(0xEC)); }
void test_fpu_fldln2()  { TEST_ASSERT_DOUBLE_WITHIN(1e-15, log(2.0),           load_const_via_fldX_then_fst(0xED)); }
void test_fpu_fldz()    { TEST_ASSERT_EQUAL_DOUBLE(0.0,                  load_const_via_fldX_then_fst(0xEE)); }

// ──────────────────────────────────────────────────────────────────────────
// Stack manipulation: FLD ST(i), FSTP ST(i), FXCH, FFREE
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_fld_sti_duplicates_top() {
    // FLDZ then FLD1 then FLD ST(0) → ST(0)=ST(1)=1.0; pop+store, store again.
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xD9, 0xEE,                          // FLDZ          ; ST(0)=0
        0xD9, 0xE8,                          // FLD1          ; ST(0)=1, ST(1)=0
        0xD9, 0xC0,                          // FLD ST(0)     ; ST(0)=1, ST(1)=1, ST(2)=0
        0xDD, 0x1E, 0x00, 0x40,              // FSTP QWORD PTR [0x4000]    ; pops 1.0
        0xDD, 0x1E, 0x00, 0x41,              // FSTP QWORD PTR [0x4100]    ; pops 1.0
        0xDD, 0x1E, 0x00, 0x42,              // FSTP QWORD PTR [0x4200]    ; pops 0.0
    };
    put_code(code, sizeof(code));
    step_n(7);

    TEST_ASSERT_EQUAL_DOUBLE(1.0, read_f64(SCRATCH_A));
    TEST_ASSERT_EQUAL_DOUBLE(1.0, read_f64(SCRATCH_B));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, read_f64(SCRATCH_C));
}

void test_fpu_fxch_swaps_top_and_sti() {
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xD9, 0xEE,                          // FLDZ          ; ST(0)=0
        0xD9, 0xE8,                          // FLD1          ; ST(0)=1, ST(1)=0
        0xD9, 0xC9,                          // FXCH ST(1)    ; ST(0)=0, ST(1)=1
        0xDD, 0x1E, 0x00, 0x40,              // FSTP m64 [A]  ; pops 0.0
        0xDD, 0x1E, 0x00, 0x41,              // FSTP m64 [B]  ; pops 1.0
    };
    put_code(code, sizeof(code));
    step_n(6);

    TEST_ASSERT_EQUAL_DOUBLE(0.0, read_f64(SCRATCH_A));
    TEST_ASSERT_EQUAL_DOUBLE(1.0, read_f64(SCRATCH_B));
}

void test_fpu_fstp_sti_copies_and_pops() {
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xD9, 0xEE,                          // FLDZ
        0xD9, 0xE8,                          // FLD1
        0xDD, 0xD9,                          // FSTP ST(1)    ; ST(1) <- ST(0)=1, pop -> ST(0)=1
        0xDD, 0x1E, 0x00, 0x40,              // FSTP m64 [A]
    };
    put_code(code, sizeof(code));
    step_n(5);

    TEST_ASSERT_EQUAL_DOUBLE(1.0, read_f64(SCRATCH_A));
}

// ──────────────────────────────────────────────────────────────────────────
// Memory arithmetic: D8 /0..7 m32 and DC /0..7 m64
// ──────────────────────────────────────────────────────────────────────────

static double do_d8_m32_op(uint8_t reg_field, double a, float b) {
    fpu_init();
    write_f32(SCRATCH_A, b);
    uint8_t modrm = (reg_field << 3) | 0x06;       // mod=0 rm=6 disp16
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDD, 0x06, 0x00, 0x40,              // (placeholder) FLD m64 — overwritten below
        0xD8, modrm, 0x00, 0x40,             // D8 /reg m32 [0x4000]
        0xDD, 0x16, 0x00, 0x41,              // FST m64 [B]
    };
    // Overwrite the placeholder: we want a separate FLD m64 from a different
    // address so we can set ST(0) = a precisely.
    write_f64(SCRATCH_C, a);
    code[2] = 0xDD; code[3] = 0x06; code[4] = 0x00; code[5] = 0x42;  // FLD m64 [C]
    put_code(code, sizeof(code));
    step_n(4);
    return read_f64(SCRATCH_B);
}

void test_fpu_d8_m32_fadd()  { TEST_ASSERT_EQUAL_DOUBLE(  10.0 + (double)2.5f,  do_d8_m32_op(0, 10.0, 2.5f)); }
void test_fpu_d8_m32_fmul()  { TEST_ASSERT_EQUAL_DOUBLE(  10.0 * (double)2.5f,  do_d8_m32_op(1, 10.0, 2.5f)); }
void test_fpu_d8_m32_fsub()  { TEST_ASSERT_EQUAL_DOUBLE(  10.0 - (double)2.5f,  do_d8_m32_op(4, 10.0, 2.5f)); }
void test_fpu_d8_m32_fsubr() { TEST_ASSERT_EQUAL_DOUBLE( (double)2.5f - 10.0,   do_d8_m32_op(5, 10.0, 2.5f)); }
void test_fpu_d8_m32_fdiv()  { TEST_ASSERT_EQUAL_DOUBLE(  10.0 / (double)2.5f,  do_d8_m32_op(6, 10.0, 2.5f)); }
void test_fpu_d8_m32_fdivr() { TEST_ASSERT_EQUAL_DOUBLE( (double)2.5f / 10.0,   do_d8_m32_op(7, 10.0, 2.5f)); }

static double do_dc_m64_op(uint8_t reg_field, double a, double b) {
    fpu_init();
    write_f64(SCRATCH_A, b);
    write_f64(SCRATCH_C, a);
    uint8_t modrm = (reg_field << 3) | 0x06;
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDD, 0x06, 0x00, 0x42,              // FLD  m64 [C]            ; ST(0)=a
        0xDC, modrm, 0x00, 0x40,             // DC /reg m64 [A]         ; op with b
        0xDD, 0x16, 0x00, 0x41,              // FST  m64 [B]
    };
    put_code(code, sizeof(code));
    step_n(4);
    return read_f64(SCRATCH_B);
}

void test_fpu_dc_m64_fadd()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 +  3.0,  do_dc_m64_op(0, 4.0, 3.0)); }
void test_fpu_dc_m64_fmul()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 *  3.0,  do_dc_m64_op(1, 4.0, 3.0)); }
void test_fpu_dc_m64_fsub()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 -  3.0,  do_dc_m64_op(4, 4.0, 3.0)); }
void test_fpu_dc_m64_fsubr() { TEST_ASSERT_EQUAL_DOUBLE(  3.0 -  4.0,  do_dc_m64_op(5, 4.0, 3.0)); }
void test_fpu_dc_m64_fdiv()  { TEST_ASSERT_EQUAL_DOUBLE( 12.0 /  4.0,  do_dc_m64_op(6,12.0, 4.0)); }
void test_fpu_dc_m64_fdivr() { TEST_ASSERT_EQUAL_DOUBLE(  4.0 / 12.0,  do_dc_m64_op(7,12.0, 4.0)); }

// ──────────────────────────────────────────────────────────────────────────
// Register-register arithmetic: D8 mod=3 (ST(0) <- ST(0) op ST(i))
// (DC mod=3 /4..7 has the encoding-swap quirk and is intentionally not
//  covered here. It will get its own tests when we fix the swap.)
// ──────────────────────────────────────────────────────────────────────────

static double do_d8_reg_op(uint8_t reg_field, double a, double b) {
    fpu_init();
    write_f64(SCRATCH_A, b);
    write_f64(SCRATCH_C, a);
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDD, 0x06, 0x00, 0x40,              // FLD  m64 [A]   ; ST(0)=b
        0xDD, 0x06, 0x00, 0x42,              // FLD  m64 [C]   ; ST(0)=a, ST(1)=b
        (uint8_t)0xD8, (uint8_t)(0xC0 | (reg_field << 3) | 1), // D8 /reg ST(1)
        0xDD, 0x16, 0x00, 0x41,              // FST  m64 [B]
    };
    put_code(code, sizeof(code));
    step_n(5);
    return read_f64(SCRATCH_B);
}

void test_fpu_d8_reg_fadd()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 +  3.0,  do_d8_reg_op(0, 4.0, 3.0)); }
void test_fpu_d8_reg_fmul()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 *  3.0,  do_d8_reg_op(1, 4.0, 3.0)); }
void test_fpu_d8_reg_fsub()  { TEST_ASSERT_EQUAL_DOUBLE(  4.0 -  3.0,  do_d8_reg_op(4, 4.0, 3.0)); }
void test_fpu_d8_reg_fsubr() { TEST_ASSERT_EQUAL_DOUBLE(  3.0 -  4.0,  do_d8_reg_op(5, 4.0, 3.0)); }
void test_fpu_d8_reg_fdiv()  { TEST_ASSERT_EQUAL_DOUBLE( 12.0 /  4.0,  do_d8_reg_op(6,12.0, 4.0)); }
void test_fpu_d8_reg_fdivr() { TEST_ASSERT_EQUAL_DOUBLE(  4.0 / 12.0,  do_d8_reg_op(7,12.0, 4.0)); }

// ──────────────────────────────────────────────────────────────────────────
// FxxP: DE mod=3 (only commutative cases here — non-commutative /4..7 has
// the same swap quirk as DC and is deferred to the bug-fix PR.)
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_faddp() {
    fpu_init();
    write_f64(SCRATCH_A, 3.0);
    write_f64(SCRATCH_C, 4.0);
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]   ; ST(0)=3
        0xDD, 0x06, 0x00, 0x42,              // FLD m64 [C]   ; ST(0)=4, ST(1)=3
        0xDE, 0xC1,                          // FADDP ST(1),ST(0) ; ST(1) <- 4+3, pop -> ST(0)=7
        0xDD, 0x16, 0x00, 0x41,              // FST m64 [B]
    };
    put_code(code, sizeof(code));
    step_n(5);
    TEST_ASSERT_EQUAL_DOUBLE(7.0, read_f64(SCRATCH_B));
}

void test_fpu_fmulp() {
    fpu_init();
    write_f64(SCRATCH_A, 3.0);
    write_f64(SCRATCH_C, 4.0);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]   ; ST(0)=3
        0xDD, 0x06, 0x00, 0x42,              // FLD m64 [C]   ; ST(0)=4, ST(1)=3
        0xDE, 0xC9,                          // FMULP ST(1),ST(0) ; pop -> ST(0)=12
        0xDD, 0x16, 0x00, 0x41,
    };
    put_code(code, sizeof(code));
    step_n(5);
    TEST_ASSERT_EQUAL_DOUBLE(12.0, read_f64(SCRATCH_B));
}

void test_fpu_fcompp_pops_two() {
    // ST(1)=3, ST(0)=3 — equal compare → C0=0, C2=0, C3=1
    fpu_init();
    write_f64(SCRATCH_A, 3.0);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]
        0xDE, 0xD9,                          // FCOMPP
        0xDF, 0xE0,                          // FNSTSW AX
    };
    put_code(code, sizeof(code));
    step_n(5);
    // C3 (bit 14) set, C2 (bit 10) clear, C0 (bit 8) clear.
    uint16_t ax = i8086::AX();
    TEST_ASSERT_BITS_HIGH((1u << 14), ax);
    TEST_ASSERT_BITS_LOW( (1u << 10) | (1u << 8), ax);
}

void test_fpu_ffree_marks_empty() {
    // FFREE marks ST(i) empty in the tag word. We can observe via FNSTENV
    // which writes the tag word at offset 4 of the env block.
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT          ; TW = 0xFFFF
        0xD9, 0xEE,                          // FLDZ            ; ST(0)=0, tag(7)=zero
        0xDD, 0xC0,                          // FFREE ST(0)     ; tag(7)=empty
        0xD9, 0x36, 0x00, 0x40,              // FNSTENV [0x4000]
    };
    put_code(code, sizeof(code));
    step_n(4);
    // Tag word at offset 4 (16-bit). After FFREE all 8 entries are 11b → 0xFFFF.
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, mem16(SCRATCH_A + 4));
}

// ──────────────────────────────────────────────────────────────────────────
// Integer load / store
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_fild_m16_then_fistp_m16() {
    fpu_init();
    write_i16(SCRATCH_A, -1234);
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xDF, 0x06, 0x00, 0x40,              // FILD  m16 [A]
        0xDF, 0x1E, 0x00, 0x41,              // FISTP m16 [B]
    };
    put_code(code, sizeof(code));
    step_n(3);
    TEST_ASSERT_EQUAL_INT16(-1234, read_i16(SCRATCH_B));
}

void test_fpu_fild_m32_then_fistp_m32() {
    fpu_init();
    write_i32(SCRATCH_A, 0x12345678);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDB, 0x06, 0x00, 0x40,              // FILD  m32 [A]      DB /0
        0xDB, 0x1E, 0x00, 0x41,              // FISTP m32 [B]      DB /3
    };
    put_code(code, sizeof(code));
    step_n(3);
    TEST_ASSERT_EQUAL_INT32(0x12345678, read_i32(SCRATCH_B));
}

void test_fpu_fild_m64_then_fistp_m64() {
    // Pick a value well below 2^52 so the implementation's
    // `roundToMode` (uses `floor(x + 0.5)`) round-trips losslessly
    // through `double`.
    fpu_init();
    const int64_t expected = 0x000000123456789ALL; // ~78 billion
    write_i64(SCRATCH_A, expected);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDF, 0x2E, 0x00, 0x40,              // FILD  m64 [A]      DF /5
        0xDF, 0x3E, 0x00, 0x41,              // FISTP m64 [B]      DF /7
    };
    put_code(code, sizeof(code));
    step_n(3);
    TEST_ASSERT_EQUAL_INT64(expected, read_i64(SCRATCH_B));
}

void test_fpu_fist_m32_does_not_pop() {
    // FIST m32 (DB /2) stores without popping. After two FIST stores
    // ST(0) is still valid and a third FISTP picks it up.
    fpu_init();
    write_i32(SCRATCH_A, 99);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDB, 0x06, 0x00, 0x40,              // FILD  m32 [A]
        0xDB, 0x16, 0x00, 0x41,              // FIST  m32 [B]    ; no pop
        0xDB, 0x1E, 0x00, 0x42,              // FISTP m32 [C]    ; pop
    };
    put_code(code, sizeof(code));
    step_n(4);
    TEST_ASSERT_EQUAL_INT32(99, read_i32(SCRATCH_B));
    TEST_ASSERT_EQUAL_INT32(99, read_i32(SCRATCH_C));
}

// ──────────────────────────────────────────────────────────────────────────
// Integer arithmetic with memory: DA m32, DE m16
// ──────────────────────────────────────────────────────────────────────────

static double do_de_m16_op(uint8_t reg_field, double a, int16_t b) {
    fpu_init();
    write_i16(SCRATCH_A, b);
    write_f64(SCRATCH_C, a);
    uint8_t modrm = (reg_field << 3) | 0x06;
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x42,              // FLD m64 [C]      ; ST(0) = a
        0xDE, modrm, 0x00, 0x40,             // DE /reg m16 [A]
        0xDD, 0x16, 0x00, 0x41,
    };
    put_code(code, sizeof(code));
    step_n(4);
    return read_f64(SCRATCH_B);
}

void test_fpu_de_m16_fiadd()  { TEST_ASSERT_EQUAL_DOUBLE( 10.0 +  3.0,  do_de_m16_op(0, 10.0, 3)); }
void test_fpu_de_m16_fimul()  { TEST_ASSERT_EQUAL_DOUBLE( 10.0 *  3.0,  do_de_m16_op(1, 10.0, 3)); }
void test_fpu_de_m16_fisub()  { TEST_ASSERT_EQUAL_DOUBLE( 10.0 -  3.0,  do_de_m16_op(4, 10.0, 3)); }
void test_fpu_de_m16_fisubr() { TEST_ASSERT_EQUAL_DOUBLE(  3.0 - 10.0,  do_de_m16_op(5, 10.0, 3)); }
void test_fpu_de_m16_fidiv()  { TEST_ASSERT_EQUAL_DOUBLE( 12.0 /  3.0,  do_de_m16_op(6, 12.0, 3)); }
void test_fpu_de_m16_fidivr() { TEST_ASSERT_EQUAL_DOUBLE(  3.0 / 12.0,  do_de_m16_op(7, 12.0, 3)); }

// ──────────────────────────────────────────────────────────────────────────
// Compare / test / examine
// ──────────────────────────────────────────────────────────────────────────

// Convenience: returns the (C3:C2:C0) bits of SW packed as 0bC3_C2_C0 (3-bit).
static uint8_t fxam_status(double v) {
    fpu_init();
    write_f64(SCRATCH_A, v);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]
        0xD9, 0xE5,                          // FXAM
        0xDF, 0xE0,                          // FNSTSW AX
    };
    put_code(code, sizeof(code));
    step_n(4);
    uint16_t sw = i8086::AX();
    uint8_t c0 = (sw >> 8)  & 1;
    uint8_t c2 = (sw >> 10) & 1;
    uint8_t c3 = (sw >> 14) & 1;
    return (c3 << 2) | (c2 << 1) | c0;
}

void test_fpu_fxam_normal()    { TEST_ASSERT_EQUAL_UINT8(0b010, fxam_status( 3.14)); } // C3=0,C2=1,C0=0
void test_fpu_fxam_zero()      { TEST_ASSERT_EQUAL_UINT8(0b100, fxam_status( 0.0));  } // C3=1,C2=0,C0=0
void test_fpu_fxam_neg_normal(){ TEST_ASSERT_EQUAL_UINT8(0b010, fxam_status(-3.14)); } // C3=0,C2=1,C0=0 (sign in C1)
void test_fpu_fxam_inf()       { TEST_ASSERT_EQUAL_UINT8(0b011, fxam_status( INFINITY)); }
void test_fpu_fxam_nan()       { TEST_ASSERT_EQUAL_UINT8(0b001, fxam_status( NAN)); }

void test_fpu_ftst_zero_sets_c3() {
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,
        0xD9, 0xEE,                          // FLDZ
        0xD9, 0xE4,                          // FTST
        0xDF, 0xE0,                          // FNSTSW AX
    };
    put_code(code, sizeof(code));
    step_n(4);
    uint16_t sw = i8086::AX();
    TEST_ASSERT_BITS_HIGH((1u << 14), sw);                       // C3 set (equal)
    TEST_ASSERT_BITS_LOW( (1u << 10) | (1u << 8), sw);           // C2,C0 clear
}

void test_fpu_fcom_lt_sets_c0() {
    fpu_init();
    write_f64(SCRATCH_A, 5.0);                                    // memory operand
    uint8_t code[] = {
        0xDB, 0xE3,
        0xD9, 0xEE,                          // FLDZ           ; ST(0)=0
        0xDC, 0x16, 0x00, 0x40,              // FCOM m64 [A]   ; 0 vs 5 -> C0=1
        0xDF, 0xE0,                          // FNSTSW AX
    };
    put_code(code, sizeof(code));
    step_n(4);
    uint16_t sw = i8086::AX();
    TEST_ASSERT_BITS_HIGH((1u << 8),  sw);                       // C0 set (ST<src)
    TEST_ASSERT_BITS_LOW( (1u << 14) | (1u << 10), sw);
}

// ──────────────────────────────────────────────────────────────────────────
// Unary ops: FCHS, FABS, FSQRT, FRNDINT
// ──────────────────────────────────────────────────────────────────────────

static double do_d9_unary(uint8_t modrm, double in) {
    fpu_init();
    write_f64(SCRATCH_A, in);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]
        0xD9, modrm,
        0xDD, 0x16, 0x00, 0x41,              // FST m64 [B]
    };
    put_code(code, sizeof(code));
    step_n(4);
    return read_f64(SCRATCH_B);
}

void test_fpu_fchs()    { TEST_ASSERT_EQUAL_DOUBLE(-3.5,         do_d9_unary(0xE0,  3.5)); }
void test_fpu_fabs_neg(){ TEST_ASSERT_EQUAL_DOUBLE( 3.5,         do_d9_unary(0xE1, -3.5)); }
void test_fpu_fabs_pos(){ TEST_ASSERT_EQUAL_DOUBLE( 3.5,         do_d9_unary(0xE1,  3.5)); }
void test_fpu_fsqrt()   { TEST_ASSERT_EQUAL_DOUBLE( 4.0,         do_d9_unary(0xFA, 16.0)); }
void test_fpu_frndint() { TEST_ASSERT_EQUAL_DOUBLE( 4.0,         do_d9_unary(0xFC,  3.7)); }
void test_fpu_fsin()    { TEST_ASSERT_DOUBLE_WITHIN(1e-12, sin(1.0), do_d9_unary(0xFE, 1.0)); }
void test_fpu_fcos()    { TEST_ASSERT_DOUBLE_WITHIN(1e-12, cos(1.0), do_d9_unary(0xFF, 1.0)); }

// ──────────────────────────────────────────────────────────────────────────
// Binary transcendentals (smoke tests)
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_f2xm1() {
    // F2XM1: ST(0) <- 2^ST(0) - 1; for ST(0)=1.0 → 1.0
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,
        0xD9, 0xE8,                          // FLD1
        0xD9, 0xF0,                          // F2XM1
        0xDD, 0x1E, 0x00, 0x40,              // FSTP m64 [A]
    };
    put_code(code, sizeof(code));
    step_n(4);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, read_f64(SCRATCH_A));
}

void test_fpu_fyl2x() {
    // FYL2X: ST(1) <- ST(1)*log2(ST(0)); pop. With ST(0)=2, ST(1)=3 → 3.
    fpu_init();
    write_f64(SCRATCH_A, 2.0);
    write_f64(SCRATCH_B, 3.0);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x41,              // FLD m64 [B]   ; ST(0)=3
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]   ; ST(0)=2, ST(1)=3
        0xD9, 0xF1,                          // FYL2X         ; ST(1)<-3*1=3, pop -> ST(0)=3
        0xDD, 0x1E, 0x00, 0x42,              // FSTP m64 [C]
    };
    put_code(code, sizeof(code));
    step_n(5);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 3.0, read_f64(SCRATCH_C));
}

void test_fpu_fpatan() {
    // FPATAN: ST(1) <- atan2(ST(1), ST(0)); pop. ST(0)=1, ST(1)=1 → π/4
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,
        0xD9, 0xE8,                          // FLD1                ; ST(0)=1
        0xD9, 0xE8,                          // FLD1                ; ST(0)=1, ST(1)=1
        0xD9, 0xF3,                          // FPATAN              ; ST(1)<-π/4, pop
        0xDD, 0x1E, 0x00, 0x40,              // FSTP m64 [A]
    };
    put_code(code, sizeof(code));
    step_n(5);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, M_PI/4.0, read_f64(SCRATCH_A));
}

void test_fpu_fscale() {
    // FSCALE: ST(0) <- ST(0) * 2^trunc(ST(1)). ST(0)=3, ST(1)=4 → 48
    fpu_init();
    write_f64(SCRATCH_A, 3.0);
    write_f64(SCRATCH_B, 4.0);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x41,              // FLD m64 [B]   ; ST(0)=4
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]   ; ST(0)=3, ST(1)=4
        0xD9, 0xFD,                          // FSCALE
        0xDD, 0x1E, 0x00, 0x42,              // FSTP m64 [C]
    };
    put_code(code, sizeof(code));
    step_n(5);
    TEST_ASSERT_EQUAL_DOUBLE(48.0, read_f64(SCRATCH_C));
}

// ──────────────────────────────────────────────────────────────────────────
// Environment & state save/restore
// ──────────────────────────────────────────────────────────────────────────

void test_fpu_fnstenv_layout() {
    // After FNINIT then FNSTENV [A], we expect:
    //   off 0 : control word = 0x037F
    //   off 2 : status word  = 0x0000
    //   off 4 : tag word     = 0xFFFF
    fpu_init();
    uint8_t code[] = {
        0xDB, 0xE3,                          // FNINIT
        0xD9, 0x36, 0x00, 0x40,              // FNSTENV [A]    D9 /6
    };
    put_code(code, sizeof(code));
    step_n(2);

    TEST_ASSERT_EQUAL_HEX16(0x037F, mem16(SCRATCH_A + 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, mem16(SCRATCH_A + 2));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, mem16(SCRATCH_A + 4));
}

void test_fpu_fldenv_restores_control_word() {
    // Build an environment block with CW=0x027F and FLDENV it; verify with FNSTCW.
    fpu_init();
    write_i16(SCRATCH_A + 0, 0x027F);        // CW
    write_i16(SCRATCH_A + 2, 0x0000);        // SW
    write_i16(SCRATCH_A + 4, 0xFFFF);        // TW
    for (int i = 6; i < 14; i++) test_ram[SCRATCH_A + i] = 0;

    uint8_t code[] = {
        0xDB, 0xE3,
        0xD9, 0x26, 0x00, 0x40,              // FLDENV [A]     D9 /4
        0xD9, 0x3E, 0x00, 0x41,              // FNSTCW [B]     D9 /7
    };
    put_code(code, sizeof(code));
    step_n(3);

    TEST_ASSERT_EQUAL_HEX16(0x027F, mem16(SCRATCH_B));
}

void test_fpu_fnsave_frstor_roundtrip() {
    // Push a value, FNSAVE, change state, FRSTOR, verify the value comes back.
    fpu_init();
    write_f64(SCRATCH_A, 7.5);
    uint8_t code[] = {
        0xDB, 0xE3,
        0xDD, 0x06, 0x00, 0x40,              // FLD m64 [A]            ; ST(0)=7.5
        0xDD, 0x36, 0x00, 0x42,              // FNSAVE [C]             DD /6  (resets FPU)
        0xDB, 0xE3,                          // FNINIT (just to make sure state is clean)
        0xDD, 0x26, 0x00, 0x42,              // FRSTOR [C]             DD /4
        0xDD, 0x1E, 0x00, 0x41,              // FSTP m64 [B]
    };
    put_code(code, sizeof(code));
    step_n(6);
    TEST_ASSERT_EQUAL_DOUBLE(7.5, read_f64(SCRATCH_B));
}

// ──────────────────────────────────────────────────────────────────────────
// Test runner
// ──────────────────────────────────────────────────────────────────────────

void run_fpu_tests() {
    RUN_TEST(test_fpu_ip_fninit);
    RUN_TEST(test_fpu_ip_fstsw_ax);
    RUN_TEST(test_fpu_ip_fnstcw_bx);
    RUN_TEST(test_fpu_ip_fnstsw_bx);
    RUN_TEST(test_fpu_ip_fldcw_disp16);
    RUN_TEST(test_fpu_ip_fadd_disp8);
    RUN_TEST(test_fpu_ip_fnop);
    RUN_TEST(test_fpu_ip_fwait);

    RUN_TEST(test_fpu_fninit_then_fnstcw_writes_037F);
    RUN_TEST(test_fpu_fninit_then_fnstsw_writes_0000);
    RUN_TEST(test_fpu_fninit_then_fstsw_ax_returns_0);
    RUN_TEST(test_fpu_fldcw_fnstcw_roundtrip);
    RUN_TEST(test_fpu_glabios_has_fpu_stack_safety);
    RUN_TEST(test_fpu_fnop_does_not_swallow_fld_sti);

    // Memory load/store
    RUN_TEST(test_fpu_fld_fst_m32_roundtrip);
    RUN_TEST(test_fpu_fld_fstp_m32_pops_stack);
    RUN_TEST(test_fpu_fld_fst_m64_roundtrip);
    RUN_TEST(test_fpu_fld_fstp_m80_roundtrip_low8);

    // Constants
    RUN_TEST(test_fpu_fld1);
    RUN_TEST(test_fpu_fldl2t);
    RUN_TEST(test_fpu_fldl2e);
    RUN_TEST(test_fpu_fldpi);
    RUN_TEST(test_fpu_fldlg2);
    RUN_TEST(test_fpu_fldln2);
    RUN_TEST(test_fpu_fldz);

    // Stack ops
    RUN_TEST(test_fpu_fld_sti_duplicates_top);
    RUN_TEST(test_fpu_fxch_swaps_top_and_sti);
    RUN_TEST(test_fpu_fstp_sti_copies_and_pops);

    // Memory arithmetic
    RUN_TEST(test_fpu_d8_m32_fadd);
    RUN_TEST(test_fpu_d8_m32_fmul);
    RUN_TEST(test_fpu_d8_m32_fsub);
    RUN_TEST(test_fpu_d8_m32_fsubr);
    RUN_TEST(test_fpu_d8_m32_fdiv);
    RUN_TEST(test_fpu_d8_m32_fdivr);
    RUN_TEST(test_fpu_dc_m64_fadd);
    RUN_TEST(test_fpu_dc_m64_fmul);
    RUN_TEST(test_fpu_dc_m64_fsub);
    RUN_TEST(test_fpu_dc_m64_fsubr);
    RUN_TEST(test_fpu_dc_m64_fdiv);
    RUN_TEST(test_fpu_dc_m64_fdivr);

    // Reg-reg arithmetic (D8 mod=3)
    RUN_TEST(test_fpu_d8_reg_fadd);
    RUN_TEST(test_fpu_d8_reg_fmul);
    RUN_TEST(test_fpu_d8_reg_fsub);
    RUN_TEST(test_fpu_d8_reg_fsubr);
    RUN_TEST(test_fpu_d8_reg_fdiv);
    RUN_TEST(test_fpu_d8_reg_fdivr);

    // FxxP (commutative cases only)
    RUN_TEST(test_fpu_faddp);
    RUN_TEST(test_fpu_fmulp);
    RUN_TEST(test_fpu_fcompp_pops_two);
    RUN_TEST(test_fpu_ffree_marks_empty);

    // Integer load/store
    RUN_TEST(test_fpu_fild_m16_then_fistp_m16);
    RUN_TEST(test_fpu_fild_m32_then_fistp_m32);
    RUN_TEST(test_fpu_fild_m64_then_fistp_m64);
    RUN_TEST(test_fpu_fist_m32_does_not_pop);

    // Integer arithmetic
    RUN_TEST(test_fpu_de_m16_fiadd);
    RUN_TEST(test_fpu_de_m16_fimul);
    RUN_TEST(test_fpu_de_m16_fisub);
    RUN_TEST(test_fpu_de_m16_fisubr);
    RUN_TEST(test_fpu_de_m16_fidiv);
    RUN_TEST(test_fpu_de_m16_fidivr);

    // Compare / examine
    RUN_TEST(test_fpu_fxam_normal);
    RUN_TEST(test_fpu_fxam_zero);
    RUN_TEST(test_fpu_fxam_neg_normal);
    RUN_TEST(test_fpu_fxam_inf);
    RUN_TEST(test_fpu_fxam_nan);
    RUN_TEST(test_fpu_ftst_zero_sets_c3);
    RUN_TEST(test_fpu_fcom_lt_sets_c0);

    // Unary
    RUN_TEST(test_fpu_fchs);
    RUN_TEST(test_fpu_fabs_neg);
    RUN_TEST(test_fpu_fabs_pos);
    RUN_TEST(test_fpu_fsqrt);
    RUN_TEST(test_fpu_frndint);
    RUN_TEST(test_fpu_fsin);
    RUN_TEST(test_fpu_fcos);

    // Transcendentals
    RUN_TEST(test_fpu_f2xm1);
    RUN_TEST(test_fpu_fyl2x);
    RUN_TEST(test_fpu_fpatan);
    RUN_TEST(test_fpu_fscale);

    // Environment & save/restore
    RUN_TEST(test_fpu_fnstenv_layout);
    RUN_TEST(test_fpu_fldenv_restores_control_word);
    RUN_TEST(test_fpu_fnsave_frstor_roundtrip);
}
