/*
 * Based on 8086tiny, a tiny 8086 PC emulator
 * Original work:
 *   Copyright (c) 2013-2014 Adrian Cable
 *   https://www.megalith.co.uk/8086tiny
 *
 * Modifications by:
 *   Julian Olds (8086tiny plus)
 *
 * Further modifications and integration into FabGL by:
 *   Fabrizio Di Vittorio
 *
 * Additional modifications by:
 *   Copyright (c) 2026 Jesus Martinez-Mateo
 *   Author: Jesus Martinez-Mateo <jesus.martinez.mateo@gmail.com>
 *
 * The original 8086tiny code is licensed under the MIT License.
 * This file is distributed as part of a GPL-licensed project;
 * the MIT license notice is preserved as required.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

#define I8086_SHOW_OPCODE_STATS 0

#ifndef I80186MODE
#define I80186MODE 0
#endif

#define I8086_USE_INLINE_RWMEM 1

// ESP32 PSRAM bug workaround (use when the library is NOT compiled with PSRAM hack enabled)
// Place between a write and a read PSRAM operation (write->ASM_MEMW->read), not viceversa
#ifdef NATIVE_BUILD
  #define ASM_MEMW
  #define ASM_NOP
  #define PSRAM_WORKAROUND1
  #define PSRAM_WORKAROUND2
#else
  #define ASM_MEMW asm(" MEMW");
  #define ASM_NOP asm(" NOP");
  #define PSRAM_WORKAROUND1 asm(" nop;nop;nop;nop");
  #define PSRAM_WORKAROUND2 asm(" memw");
#endif

#define VIDEOMEM_START 0xA0000
#define VIDEOMEM_END   0xC0000

namespace fabgl {

class i8087;

class i8086 {

public:

  // callbacks

  typedef void (*WritePort)(void * context, int address, uint8_t value);
  typedef uint8_t (*ReadPort)(void * context, int address);
  typedef void (*WriteVideoMemory8)(void * context, int address, uint8_t value);
  typedef void (*WriteVideoMemory16)(void * context, int address, uint16_t value);
  typedef uint8_t (*ReadVideoMemory8)(void * context, int address);
  typedef uint16_t (*ReadVideoMemory16)(void * context, int address);
  typedef bool (*Interrupt)(void * context, int num);

  static void setCallbacks(void * context, ReadPort readPort, WritePort writePort, WriteVideoMemory8 writeVideoMemory8, WriteVideoMemory16 writeVideoMemory16, ReadVideoMemory8 readVideoMemory8, ReadVideoMemory16 readVideoMemory16,Interrupt interrupt) {
    s_context            = context;
    s_readPort           = readPort;
    s_writePort          = writePort;
    s_writeVideoMemory8  = writeVideoMemory8;
    s_writeVideoMemory16 = writeVideoMemory16;
    s_readVideoMemory8   = readVideoMemory8;
    s_readVideoMemory16  = readVideoMemory16;
    s_interrupt          = interrupt;
  }

  static void setMemory(uint8_t * memory) { s_memory = memory; }

  static void reset();

  static void setAL(uint8_t value);
  static void setAH(uint8_t value);
  static void setBL(uint8_t value);
  static void setBH(uint8_t value);
  static void setCL(uint8_t value);
  static void setCH(uint8_t value);
  static void setDL(uint8_t value);
  static void setDH(uint8_t value);

  static uint8_t AL();
  static uint8_t AH();
  static uint8_t BL();
  static uint8_t BH();
  static uint8_t CL();
  static uint8_t CH();
  static uint8_t DL();
  static uint8_t DH();

  static void setAX(uint16_t value);
  static void setBX(uint16_t value);
  static void setCX(uint16_t value);
  static void setDX(uint16_t value);
  static void setDI(uint16_t value);
  static void setCS(uint16_t value);
  static void setDS(uint16_t value);
  static void setSS(uint16_t value);
  static void setES(uint16_t value);
  static void setIP(uint16_t value);
  static void setSP(uint16_t value);

  static uint16_t AX();
  static uint16_t BX();
  static uint16_t CX();
  static uint16_t DX();
  static uint16_t BP();
  static uint16_t SI();
  static uint16_t DI();
  static uint16_t SP();

  static uint16_t CS();
  static uint16_t ES();
  static uint16_t DS();
  static uint16_t SS();

  static bool flagIF();
  static bool flagTF();
  static bool flagCF();
  static bool flagZF();
  static bool flagOF();
  static bool flagDF();
  static bool flagSF();
  static bool flagAF();
  static bool flagPF();

  static void setFlagZF(bool value);
  static void setFlagCF(bool value);

  static uint16_t IP();

  static bool halted()                                    { return s_halted; }

  static bool IRQ(uint8_t interrupt_num);

  static void step();

private:

#if I8086_USE_INLINE_RWMEM

  static inline __attribute__((always_inline)) uint8_t WMEM8(int addr, uint8_t value) {
    if (addr >= VIDEOMEM_START && addr < VIDEOMEM_END) {
      s_writeVideoMemory8(s_context, addr, value);
    } else {
      s_memory[addr] = value;
    }
    return value;
  }

  static inline __attribute__((always_inline)) uint16_t WMEM16(int addr, uint16_t value) {
    if (addr >= VIDEOMEM_START && addr < VIDEOMEM_END) {
      s_writeVideoMemory16(s_context, addr, value);
    } else {
      *(uint16_t*)(s_memory + addr) = value;
    }
    return value;
  }

  static inline __attribute__((always_inline)) uint8_t RMEM8(int addr) {
    if (addr >= VIDEOMEM_START && addr < VIDEOMEM_END) {
      return s_readVideoMemory8(s_context, addr);
    } else {
      return s_memory[addr];
    }
  }

  static inline __attribute__((always_inline)) uint16_t RMEM16(int addr) {
    if (addr >= VIDEOMEM_START && addr < VIDEOMEM_END) {
      return s_readVideoMemory16(s_context, addr);
    } else {
      return *(uint16_t*)(s_memory + addr);
    }
  }

#else

  static uint8_t WMEM8(int addr, uint8_t value);
  static uint16_t WMEM16(int addr, uint16_t value);
  static uint8_t RMEM8(int addr);
  static uint16_t RMEM16(int addr);

#endif // I8086_USE_INLINE_RWMEM

  static uint16_t make_flags();
  static void set_flags(int new_flags);

  static void pc_interrupt(uint8_t interrupt_num);

  static uint8_t raiseDivideByZeroInterrupt();

  static void stepEx(uint8_t const * opcode_stream);

  static void *             s_context;
  static ReadPort           s_readPort;
  static WritePort          s_writePort;
  static WriteVideoMemory8  s_writeVideoMemory8;
  static WriteVideoMemory16 s_writeVideoMemory16;
  static ReadVideoMemory8   s_readVideoMemory8;
  static ReadVideoMemory16  s_readVideoMemory16;
  static Interrupt          s_interrupt;

  static bool               s_pendingIRQ;
  static uint8_t            s_pendingIRQIndex;
  static uint8_t *          s_memory;
  static bool               s_halted;

  friend class i8087;

  // Floating-Point Unit (FPU)
  static i8087 s_fpu;  // single i8087 instance

};

} // end of namespace
