// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2021 the V8 project authors. All rights reserved.

#include "jit/riscv64/extension/base-assembler-riscv.h"
#include "jit/riscv64/Architecture-riscv64.h"

namespace js {
namespace jit {

int ToNumber(Register reg) {
  MOZ_ASSERT(reg.code() < Registers::Total && reg.code() >= 0);
  const int kNumbers[] = {
      0,   // zero_reg
      1,   // ra
      2,   // sp
      3,   // gp
      4,   // tp
      5,   // t0
      6,   // t1
      7,   // t2
      8,   // s0/fp
      9,   // s1
      10,  // a0
      11,  // a1
      12,  // a2
      13,  // a3
      14,  // a4
      15,  // a5
      16,  // a6
      17,  // a7
      18,  // s2
      19,  // s3
      20,  // s4
      21,  // s5
      22,  // s6
      23,  // s7
      24,  // s8
      25,  // s9
      26,  // s10
      27,  // s11
      28,  // t3
      29,  // t4
      30,  // t5
      31,  // t6
  };
  return kNumbers[reg.code()];
}

Register ToRegister(uint32_t num) {
  MOZ_ASSERT(num >= 0 && num < Registers::Total);
  const Register kRegisters[] = {
      zero_reg, ra, sp, gp, tp, t0, t1, t2, fp, s1, a0,  a1,  a2, a3, a4, a5,
      a6,       a7, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6};
  return kRegisters[num];
}

// ----- Top-level instruction formats match those in the ISA manual
// (R, I, S, B, U, J). These match the formats defined in the compiler
void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, Register rd, Register rs1,
                                   Register rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, FPURegister rd,
                                   FPURegister rs1, FPURegister rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.encoding() << kRdShift) |
                (funct3 << kFunct3Shift) | (rs1.encoding() << kRs1Shift) |
                (rs2.encoding() << kRs2Shift) | (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, Register rd,
                                   FPURegister rs1, Register rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.encoding() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, FPURegister rd,
                                   Register rs1, Register rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.encoding() << kRdShift) |
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) | (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, FPURegister rd,
                                   FPURegister rs1, Register rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.encoding() << kRdShift) |
                (funct3 << kFunct3Shift) | (rs1.encoding() << kRs1Shift) |
                (rs2.code() << kRs2Shift) | (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR(uint8_t funct7, uint8_t funct3,
                                   BaseOpcode opcode, Register rd,
                                   FPURegister rs1, FPURegister rs2) {
  MOZ_ASSERT(is_uint7(funct7) && is_uint3(funct3));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.encoding() << kRs1Shift) | (rs2.encoding() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR4(uint8_t funct2, BaseOpcode opcode,
                                    Register rd, Register rs1, Register rs2,
                                    Register rs3, FPURoundingMode frm) {
  MOZ_ASSERT(is_uint2(funct2) && is_uint3(frm));
  Instr instr = opcode | (rd.code() << kRdShift) | (frm << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct2 << kFunct2Shift) | (rs3.code() << kRs3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrR4(uint8_t funct2, BaseOpcode opcode,
                                    FPURegister rd, FPURegister rs1,
                                    FPURegister rs2, FPURegister rs3,
                                    FPURoundingMode frm) {
  MOZ_ASSERT(is_uint2(funct2) && is_uint3(frm));
  Instr instr = opcode | (rd.encoding() << kRdShift) | (frm << kFunct3Shift) |
                (rs1.encoding() << kRs1Shift) | (rs2.encoding() << kRs2Shift) |
                (funct2 << kFunct2Shift) | (rs3.encoding() << kRs3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrRAtomic(uint8_t funct5, bool aq, bool rl,
                                         uint8_t funct3, Register rd,
                                         Register rs1, Register rs2) {
  MOZ_ASSERT(is_uint5(funct5) && is_uint3(funct3));
  Instr instr = AMO | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (rl << kRlShift) | (aq << kAqShift) | (funct5 << kFunct5Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrRFrm(uint8_t funct7, BaseOpcode opcode,
                                      Register rd, Register rs1, Register rs2,
                                      FPURoundingMode frm) {
  MOZ_ASSERT(is_uint3(frm));
  Instr instr = opcode | (rd.code() << kRdShift) | (frm << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (rs2.code() << kRs2Shift) |
                (funct7 << kFunct7Shift);
  emit(instr);
}

BufferOffset AssemblerRiscvBase::GenInstrI(uint8_t funct3, BaseOpcode opcode,
                                           Register rd, Register rs1,
                                           int16_t imm12) {
  MOZ_ASSERT(is_uint3(funct3) && (is_uint12(imm12) || is_int12(imm12)));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (imm12 << kImm12Shift);
  return emit(instr);
}

BufferOffset AssemblerRiscvBase::GenInstrI(uint8_t funct3, BaseOpcode opcode,
                                           FPURegister rd, Register rs1,
                                           int16_t imm12) {
  MOZ_ASSERT(is_uint3(funct3) && (is_uint12(imm12) || is_int12(imm12)));
  Instr instr = opcode | (rd.encoding() << kRdShift) |
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (imm12 << kImm12Shift);
  return emit(instr);
}

void AssemblerRiscvBase::GenInstrIShift(bool arithshift, uint8_t funct3,
                                        BaseOpcode opcode, Register rd,
                                        Register rs1, uint8_t shamt) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint6(shamt));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (shamt << kShamtShift) |
                (arithshift << kArithShiftShift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrIShiftW(bool arithshift, uint8_t funct3,
                                         BaseOpcode opcode, Register rd,
                                         Register rs1, uint8_t shamt) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint5(shamt));
  Instr instr = opcode | (rd.code() << kRdShift) | (funct3 << kFunct3Shift) |
                (rs1.code() << kRs1Shift) | (shamt << kShamtWShift) |
                (arithshift << kArithShiftShift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrS(uint8_t funct3, BaseOpcode opcode,
                                   Register rs1, Register rs2, int16_t imm12) {
  MOZ_ASSERT(is_uint3(funct3) && is_int12(imm12));
  Instr instr = opcode | ((imm12 & 0x1f) << 7) |  // bits  4-0
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) |
                ((imm12 & 0xfe0) << 20);  // bits 11-5
  emit(instr);
}

void AssemblerRiscvBase::GenInstrS(uint8_t funct3, BaseOpcode opcode,
                                   Register rs1, FPURegister rs2,
                                   int16_t imm12) {
  MOZ_ASSERT(is_uint3(funct3) && is_int12(imm12));
  Instr instr = opcode | ((imm12 & 0x1f) << 7) |  // bits  4-0
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.encoding() << kRs2Shift) |
                ((imm12 & 0xfe0) << 20);  // bits 11-5
  emit(instr);
}

void AssemblerRiscvBase::GenInstrB(uint8_t funct3, BaseOpcode opcode,
                                   Register rs1, Register rs2, int16_t imm13) {
  MOZ_ASSERT(is_uint3(funct3) && is_int13(imm13) && ((imm13 & 1) == 0));
  Instr instr = opcode | ((imm13 & 0x800) >> 4) |  // bit  11
                ((imm13 & 0x1e) << 7) |            // bits 4-1
                (funct3 << kFunct3Shift) | (rs1.code() << kRs1Shift) |
                (rs2.code() << kRs2Shift) |
                ((imm13 & 0x7e0) << 20) |  // bits 10-5
                ((imm13 & 0x1000) << 19);  // bit 12
  emit(instr);
}

void AssemblerRiscvBase::GenInstrU(BaseOpcode opcode, Register rd,
                                   int32_t imm20) {
  MOZ_ASSERT((is_int20(imm20) || is_uint20(imm20)));
  Instr instr = opcode | (rd.code() << kRdShift) | (imm20 << kImm20Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrJ(BaseOpcode opcode, Register rd,
                                   int32_t imm21) {
  MOZ_ASSERT(is_int21(imm21) && ((imm21 & 1) == 0));
  Instr instr = opcode | (rd.code() << kRdShift) |
                (imm21 & 0xff000) |          // bits 19-12
                ((imm21 & 0x800) << 9) |     // bit  11
                ((imm21 & 0x7fe) << 20) |    // bits 10-1
                ((imm21 & 0x100000) << 11);  // bit  20
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCR(uint8_t funct4, BaseOpcode opcode,
                                    Register rd, Register rs2) {
  MOZ_ASSERT(is_uint4(funct4));
  ShortInstr instr = opcode | (rs2.code() << kRvcRs2Shift) |
                     (rd.code() << kRvcRdShift) | (funct4 << kRvcFunct4Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCA(uint8_t funct6, BaseOpcode opcode,
                                    Register rd, uint8_t funct, Register rs2) {
  MOZ_ASSERT(is_uint6(funct6) && is_uint2(funct));
  ShortInstr instr = opcode | ((rs2.code() & 0x7) << kRvcRs2sShift) |
                     ((rd.code() & 0x7) << kRvcRs1sShift) |
                     (funct6 << kRvcFunct6Shift) | (funct << kRvcFunct2Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCI(uint8_t funct3, BaseOpcode opcode,
                                    Register rd, int8_t imm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_int6(imm6));
  ShortInstr instr = opcode | ((imm6 & 0x1f) << 2) |
                     (rd.code() << kRvcRdShift) | ((imm6 & 0x20) << 7) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCIU(uint8_t funct3, BaseOpcode opcode,
                                     Register rd, uint8_t uimm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint6(uimm6));
  ShortInstr instr = opcode | ((uimm6 & 0x1f) << 2) |
                     (rd.code() << kRvcRdShift) | ((uimm6 & 0x20) << 7) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCIU(uint8_t funct3, BaseOpcode opcode,
                                     FPURegister rd, uint8_t uimm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint6(uimm6));
  ShortInstr instr = opcode | ((uimm6 & 0x1f) << 2) |
                     (rd.encoding() << kRvcRdShift) | ((uimm6 & 0x20) << 7) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCIW(uint8_t funct3, BaseOpcode opcode,
                                     Register rd, uint8_t uimm8) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint8(uimm8));
  ShortInstr instr = opcode | ((uimm8) << 5) |
                     ((rd.code() & 0x7) << kRvcRs2sShift) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCSS(uint8_t funct3, BaseOpcode opcode,
                                     Register rs2, uint8_t uimm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint6(uimm6));
  ShortInstr instr = opcode | (uimm6 << 7) | (rs2.code() << kRvcRs2Shift) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCSS(uint8_t funct3, BaseOpcode opcode,
                                     FPURegister rs2, uint8_t uimm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint6(uimm6));
  ShortInstr instr = opcode | (uimm6 << 7) | (rs2.encoding() << kRvcRs2Shift) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCL(uint8_t funct3, BaseOpcode opcode,
                                    Register rd, Register rs1, uint8_t uimm5) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint5(uimm5));
  ShortInstr instr = opcode | ((uimm5 & 0x3) << 5) |
                     ((rd.code() & 0x7) << kRvcRs2sShift) |
                     ((uimm5 & 0x1c) << 8) | (funct3 << kRvcFunct3Shift) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCL(uint8_t funct3, BaseOpcode opcode,
                                    FPURegister rd, Register rs1,
                                    uint8_t uimm5) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint5(uimm5));
  ShortInstr instr = opcode | ((uimm5 & 0x3) << 5) |
                     ((rd.encoding() & 0x7) << kRvcRs2sShift) |
                     ((uimm5 & 0x1c) << 8) | (funct3 << kRvcFunct3Shift) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift);
  emit(instr);
}
void AssemblerRiscvBase::GenInstrCJ(uint8_t funct3, BaseOpcode opcode,
                                    uint16_t uint11) {
  MOZ_ASSERT(is_uint11(uint11));
  ShortInstr instr = opcode | (funct3 << kRvcFunct3Shift) | (uint11 << 2);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCS(uint8_t funct3, BaseOpcode opcode,
                                    Register rs2, Register rs1, uint8_t uimm5) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint5(uimm5));
  ShortInstr instr = opcode | ((uimm5 & 0x3) << 5) |
                     ((rs2.code() & 0x7) << kRvcRs2sShift) |
                     ((uimm5 & 0x1c) << 8) | (funct3 << kRvcFunct3Shift) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCS(uint8_t funct3, BaseOpcode opcode,
                                    FPURegister rs2, Register rs1,
                                    uint8_t uimm5) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint5(uimm5));
  ShortInstr instr = opcode | ((uimm5 & 0x3) << 5) |
                     ((rs2.encoding() & 0x7) << kRvcRs2sShift) |
                     ((uimm5 & 0x1c) << 8) | (funct3 << kRvcFunct3Shift) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCB(uint8_t funct3, BaseOpcode opcode,
                                    Register rs1, uint8_t uimm8) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint8(uimm8));
  ShortInstr instr = opcode | ((uimm8 & 0x1f) << 2) | ((uimm8 & 0xe0) << 5) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift) |
                     (funct3 << kRvcFunct3Shift);
  emit(instr);
}

void AssemblerRiscvBase::GenInstrCBA(uint8_t funct3, uint8_t funct2,
                                     BaseOpcode opcode, Register rs1,
                                     int8_t imm6) {
  MOZ_ASSERT(is_uint3(funct3) && is_uint2(funct2) && is_int6(imm6));
  ShortInstr instr = opcode | ((imm6 & 0x1f) << 2) | ((imm6 & 0x20) << 7) |
                     ((rs1.code() & 0x7) << kRvcRs1sShift) |
                     (funct3 << kRvcFunct3Shift) | (funct2 << 10);
  emit(instr);
}
// ----- Instruction class templates match those in the compiler

void AssemblerRiscvBase::GenInstrBranchCC_rri(uint8_t funct3, Register rs1,
                                              Register rs2, int16_t imm13) {
  GenInstrB(funct3, BRANCH, rs1, rs2, imm13);
}

void AssemblerRiscvBase::GenInstrLoad_ri(uint8_t funct3, Register rd,
                                         Register rs1, int16_t imm12) {
  GenInstrI(funct3, LOAD, rd, rs1, imm12);
}

void AssemblerRiscvBase::GenInstrStore_rri(uint8_t funct3, Register rs1,
                                           Register rs2, int16_t imm12) {
  GenInstrS(funct3, STORE, rs1, rs2, imm12);
}

void AssemblerRiscvBase::GenInstrALU_ri(uint8_t funct3, Register rd,
                                        Register rs1, int16_t imm12) {
  GenInstrI(funct3, OP_IMM, rd, rs1, imm12);
}

void AssemblerRiscvBase::GenInstrShift_ri(bool arithshift, uint8_t funct3,
                                          Register rd, Register rs1,
                                          uint8_t shamt) {
  MOZ_ASSERT(is_uint6(shamt));
  GenInstrI(funct3, OP_IMM, rd, rs1, (arithshift << 10) | shamt);
}

void AssemblerRiscvBase::GenInstrALU_rr(uint8_t funct7, uint8_t funct3,
                                        Register rd, Register rs1,
                                        Register rs2) {
  GenInstrR(funct7, funct3, OP, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrCSR_ir(uint8_t funct3, Register rd,
                                        ControlStatusReg csr, Register rs1) {
  GenInstrI(funct3, SYSTEM, rd, rs1, csr);
}

void AssemblerRiscvBase::GenInstrCSR_ii(uint8_t funct3, Register rd,
                                        ControlStatusReg csr, uint8_t imm5) {
  GenInstrI(funct3, SYSTEM, rd, ToRegister(imm5), csr);
}

void AssemblerRiscvBase::GenInstrShiftW_ri(bool arithshift, uint8_t funct3,
                                           Register rd, Register rs1,
                                           uint8_t shamt) {
  GenInstrIShiftW(arithshift, funct3, OP_IMM_32, rd, rs1, shamt);
}

void AssemblerRiscvBase::GenInstrALUW_rr(uint8_t funct7, uint8_t funct3,
                                         Register rd, Register rs1,
                                         Register rs2) {
  GenInstrR(funct7, funct3, OP_32, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrPriv(uint8_t funct7, Register rs1,
                                      Register rs2) {
  GenInstrR(funct7, 0b000, SYSTEM, ToRegister(0UL), rs1, rs2);
}

void AssemblerRiscvBase::GenInstrLoadFP_ri(uint8_t funct3, FPURegister rd,
                                           Register rs1, int16_t imm12) {
  GenInstrI(funct3, LOAD_FP, rd, rs1, imm12);
}

void AssemblerRiscvBase::GenInstrStoreFP_rri(uint8_t funct3, Register rs1,
                                             FPURegister rs2, int16_t imm12) {
  GenInstrS(funct3, STORE_FP, rs1, rs2, imm12);
}

void AssemblerRiscvBase::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3,
                                          FPURegister rd, FPURegister rs1,
                                          FPURegister rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3,
                                          FPURegister rd, Register rs1,
                                          Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3,
                                          FPURegister rd, FPURegister rs1,
                                          Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3,
                                          Register rd, FPURegister rs1,
                                          Register rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

void AssemblerRiscvBase::GenInstrALUFP_rr(uint8_t funct7, uint8_t funct3,
                                          Register rd, FPURegister rs1,
                                          FPURegister rs2) {
  GenInstrR(funct7, funct3, OP_FP, rd, rs1, rs2);
}

}  // namespace jit
}  // namespace js
