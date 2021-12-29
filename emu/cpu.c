// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

#include <env.h>

#define SHOULD_INT(interrupt) ((mem.map.interrupt_flag_reg.interrupt) \
                            && (mem.map.interrupt_enable_reg.interrupt))

struct CPU_INSTRUCTION {
    byte opcode;
    uint32_t clock_cycles;
    uint8_t operands_length; // in bytes
    word *operands[2];
    uint8_t *description;
    void (*handler)(struct CPU_INSTRUCTION *);
    void (*progresser)(struct CPU_INSTRUCTION *);
};

struct CPU_REGS cpu_regs;
struct CPU_INSTRUCTION p_instr;

_Bool cpu_alive = 0;
_Bool cpu_int_halt = 0;
_Bool cpu_dma_halt = 0;

byte interrupt_master_enable = 0; // 0: disabled, 1: enabled, >1: disabled but transitioning to enabled

int32_t clock_cycle_counter = 0;
uint32_t global_cycle_counter = 0;

#ifdef __DEBUG
_Bool single_steps = 0; // sort of hacky single-stepping mechanism for debugging
_Bool activate_single_stepping_on_condition = 0; // toggle-able user key to set single_steps = 1 only if activate_single_stepping_on_condition == 1
_Bool till_zero = 0;    // flag to continue executing until the zero flag is set
_Bool till_carry = 0;   // flag to continue executing until the carry flag is set
#endif

/* CONTROL FLOW */

__always_inline static void prog_default(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.PC += instr->operands_length + 1; // increase PC
}

__always_inline static void prog_jmp(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.PC = instr->operands[0];
}

/* GENERAL INSTRUCTION HANDLERS */

__always_inline static void instr_illegal(struct CPU_INSTRUCTION *instr)
{
    printf("CPU instructed to execute illegal opcode 0x%02X at 0x%04X. Breaking...\n", instr->opcode, cpu_regs.PC);

    cpu_break();
}

__always_inline static void instr_nop(struct CPU_INSTRUCTION *instr)// { /* .-. */ }
{

}

__always_inline static void instr_STOP(struct CPU_INSTRUCTION *instr)
{

}

__always_inline static void instr_HALT(struct CPU_INSTRUCTION *instr)
{
    DEBUG_PRINT(("halting cpu until next interrupt...\n"));
    cpu_int_halt = 1;
}

__always_inline static void instr_LD_r_s(struct CPU_INSTRUCTION *instr) // Load register data8
{
    (* (byte *)instr->operands[0]) = (* instr->operands[1]).b.l;
}

__always_inline static void instr_LD_rr_ss(struct CPU_INSTRUCTION *instr) // Load combined register data16
{
    (* instr->operands[0]).w = (* instr->operands[1]).w;
}

__always_inline static void instr_LD_dd_s(struct CPU_INSTRUCTION *instr) // Load destination16 data8
{
    mem_write((* instr->operands[0]).w, (* (byte *)instr->operands[1]) & 0xFF);
}

__always_inline static void instr_LD_dd_ss(struct CPU_INSTRUCTION *instr) // Load destination16 data16
{
    mem_write_16((* instr->operands[0]).w, (* instr->operands[1]));
}

__always_inline static void instr_INC_r(struct CPU_INSTRUCTION *instr) // Increase register
{
    byte *reg = (byte *)instr->operands[0];
    (* reg)++;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = (((* reg) & 0xF) == 0);
}

__always_inline static void instr_INC_rr(struct CPU_INSTRUCTION *instr) // Increase combined register
{
    (* instr->operands[0]).w++;
}

__always_inline static void instr_DEC_r(struct CPU_INSTRUCTION *instr) // Decrease register
{
    byte *reg = (byte *)instr->operands[0];
    (* reg)--;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 1;
    cpu_regs.F.H = (((* reg) & 0xF) == 0xF);
}

__always_inline static void instr_DEC_rr(struct CPU_INSTRUCTION *instr) // Decrease combined register
{
    (* instr->operands[0]).w--;
}

__always_inline static void instr_INC_dd(struct CPU_INSTRUCTION *instr) // Increase data8 at destination16
{
    uint16_t offset = (* instr->operands[0]).w;

    uint16_t value = mem_read(offset);

    cpu_regs.F.H = (value & 0xF) == 0xF;

    value++;
    value &= 0xFF; // todo: maybe only do this on write as this may set the Z flag incorrectly

    mem_write(offset, value);

    cpu_regs.F.Z = (value == 0);
    cpu_regs.F.N = 0;
}

__always_inline static void instr_DEC_dd(struct CPU_INSTRUCTION *instr) // Decrease data8 at destination16
{
    uint16_t offset = (* instr->operands[0]).w;

    byte value = mem_read(offset);

    cpu_regs.F.H = (value & 0xF) == 0x0;

    value--;
    value &= 0xFF;

    mem_write(offset, value);

    cpu_regs.F.Z = (value == 0);
    cpu_regs.F.N = 1;
}

__always_inline static void instr_AND_s(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.A &= (* (byte *)instr->operands[0]);

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 1;
    cpu_regs.F.C = 0;
}

__always_inline static void instr_ADD_r_s(struct CPU_INSTRUCTION *instr) // Add register data8
{
    // todo: test
    uint16_t result = (* (byte *)instr->operands[0]) + (* (byte *)instr->operands[1]);
    cpu_regs.F.H = ((result & 0xF) < ((* (byte *)instr->operands[0]) & 0xF));
    cpu_regs.F.C = (result > 0xFF);

    result &= 0xFF;

    (* (byte *)instr->operands[0]) = result;

    cpu_regs.F.Z = (result == 0);
    cpu_regs.F.N = 0;
    //printf("[DEBUG] instr_ADD_r_s: 0x%04X + 0x%04X = 0x%04X | Z: %d N: %d H: %d C: %d\n", (* (byte *)instr->operands[0]), (* (byte *)instr->operands[1]), \
      result, cpu_regs.F.Z, cpu_regs.F.N, cpu_regs.F.H, cpu_regs.F.C);
}

__always_inline static void instr_ADC_r_s(struct CPU_INSTRUCTION *instr) // Add register data8 + carry flag
{
    byte *reg = (byte *)instr->operands[0];
    byte *val = (byte *)instr->operands[1];

    uint16_t result = (* reg) + *val + cpu_regs.F.C;
    uint16_t half_result = ((* reg) & 0xF) + (*val & 0xF) + cpu_regs.F.C;

    cpu_regs.F.N = 0;
    cpu_regs.F.H = half_result > 0x0F;
    cpu_regs.F.C = result > 0xFF;

    *reg = (result & 0xFF);
    cpu_regs.F.Z = ((* reg) == 0);
}

__always_inline static void instr_ADD_rr_rr(struct CPU_INSTRUCTION *instr) // Add combined register combined register
{
    uint32_t result = (* instr->operands[0]).w + (* instr->operands[1]).w;
    cpu_regs.F.H = ((result & 0xFFF) < ((* instr->operands[0]).w & 0xFFF));
    cpu_regs.F.C = (result > 0xFFFF);

    (* instr->operands[0]).w = result & 0xFFFF;

    cpu_regs.F.N = 0;
}

__always_inline static void instr_SUB_r(struct CPU_INSTRUCTION *instr) // Sub register (from A); untested
{
    byte value = (* (byte *)instr->operands[0]);

    cpu_regs.F.H = ((cpu_regs.A & 0xF) < (value & 0xF) ? 1 : 0); // todo: verify that this is actually correct
    cpu_regs.F.C = (cpu_regs.A < value ? 1 : 0);

    cpu_regs.A -= value;

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 1;
}

__always_inline static void instr_SUB_ss(struct CPU_INSTRUCTION *instr) // Sub data8 (from A)
{
    byte value = mem_read((* instr->operands[0]).w);

    cpu_regs.F.H = ((cpu_regs.A & 0xF) < (value & 0xF) ? 1 : 0); // todo: verify that this is actually correct
    cpu_regs.F.C = (cpu_regs.A < value ? 1 : 0);

    cpu_regs.A -= value;

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 1;
}

__always_inline static void instr_XOR_s(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.A = cpu_regs.A ^ (* (byte *)instr->operands[0]);

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 0;
}

__always_inline static void instr_XOR_ss(struct CPU_INSTRUCTION *instr)
{
    byte value = mem_read((* instr->operands[0]).w);

    cpu_regs.A = cpu_regs.A ^ value;

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 0;
}

__always_inline static void instr_SBC_r_s(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];
    byte *val = (byte *)instr->operands[1];

    int16_t result = (* reg) - *val - cpu_regs.F.C;
    int16_t half_result = ((* reg) & 0xF) - (*val & 0xF) - cpu_regs.F.C;

    cpu_regs.F.N = 1;
    cpu_regs.F.H = half_result < 0;
    cpu_regs.F.C = result < 0;

    *reg = ((uint16_t)result & 0xFF);
    cpu_regs.F.Z = ((* reg) == 0);
}

__always_inline static void instr_CPL(struct CPU_INSTRUCTION *instr) // untested (guessing it's bitwise complement)
{
    cpu_regs.A = ~cpu_regs.A;

    cpu_regs.F.N = 1;
    cpu_regs.F.H = 1;
}

__always_inline static void instr_RLCA(struct CPU_INSTRUCTION *instr) // Rotate register A through carry left
{
    cpu_regs.F.C = (cpu_regs.A > 0x7F);

    cpu_regs.A = ((cpu_regs.A << 1) & 0xFF) | (cpu_regs.A >> 7);

    //cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RRCA(struct CPU_INSTRUCTION *instr) // Rotate register A through carry right (not tested)
{
    cpu_regs.F.C = cpu_regs.A & 1;

    cpu_regs.A = (cpu_regs.A >> 1) | ((cpu_regs.A << 7) & 0xFF);

    //cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RLA(struct CPU_INSTRUCTION *instr) // Rotate register A left
{
    byte carry = cpu_regs.F.C;
    cpu_regs.F.C = (cpu_regs.A > 0x7F);

    cpu_regs.A = ((cpu_regs.A << 1) & 0xFF) | carry;

    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RRA(struct CPU_INSTRUCTION *instr) // Rotate register A right
{
    byte carry = cpu_regs.F.C ? 0x80 : 0;
    cpu_regs.F.C = (cpu_regs.A) & 1;

    cpu_regs.A = (cpu_regs.A >> 1) | carry;

    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_DAA(struct CPU_INSTRUCTION *instr)
{
    uint16_t val = cpu_regs.A;

    if (!cpu_regs.F.N)
    {
        if (cpu_regs.F.H || (val & 0xF) > 0x9)
            val += 0x06;

        if (cpu_regs.F.C || val > 0x9F)
            val += 0x60;
    }
    else
    {
        if (cpu_regs.F.H)
            val = (val - 0x06) & 0xFF;

        if (cpu_regs.F.C)
            val -= 0x60;
    }

    cpu_regs.A = (val & 0xFF);

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.H = 0;
    cpu_regs.F.C = (val > 0xFF ? 1 : cpu_regs.F.C);
}

__always_inline static void instr_SCF(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 1;
}

__always_inline static void instr_CCF(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C ^= 1;
}

/* ---------------------------- */


/* UNIQUE INSTRUCTION HANDLERS */

__always_inline static void uinstr_LDD_A_lHL(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.A = mem_read(cpu_regs.HL);
    cpu_regs.HL--;
}

__always_inline static void uinstr_LDD_lHL_A(struct CPU_INSTRUCTION *instr)
{
    mem_write(cpu_regs.HL, cpu_regs.A);
    cpu_regs.HL--;
}

__always_inline static void uinstr_LDI_A_lHL(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.A = mem_read(cpu_regs.HL);
    cpu_regs.HL++;
}

__always_inline static void uinstr_LDI_lHL_A(struct CPU_INSTRUCTION *instr)
{
    mem_write(cpu_regs.HL, cpu_regs.A);
    cpu_regs.HL++;
}

/* --------------------------- */

/* UNSTABLE */

__always_inline static void push16(word data)
{
    cpu_regs.SP--;
    cpu_regs.SP--;
    mem_write_16(cpu_regs.SP, data);

    //printf("pushing 0x%04X onto stack @0x%04X\n", data, cpu_regs.SP);
}

__always_inline static word pop16()
{
    word value = mem_read_16(cpu_regs.SP);

    //printf("popping 0x%04X from stack @0x%04X\n", value.w, cpu_regs.SP);

    cpu_regs.SP++;
    cpu_regs.SP++;

    return value;
}

__always_inline static void push8(byte data)
{
    cpu_regs.SP--;
    mem_write(cpu_regs.SP, data);

    //printf("pushing 0x%02X onto stack @0x%04X\n", data, cpu_regs.SP);
}

__always_inline static byte pop8()
{
    byte value = mem_read(cpu_regs.SP);

    //printf("popping 0x%02X from stack @0x%04X\n", value, cpu_regs.SP);

    cpu_regs.SP++;

    return value;
}

__always_inline static void instr_RLC_r(struct CPU_INSTRUCTION *instr) // Rotate register A through carry left
{
    byte *reg = (byte *)instr->operands[0];

    cpu_regs.F.C = ((* reg) > 0x7F);

    *reg = (((* reg) << 1) & 0xFF) | ((* reg) >> 7);

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RLC_dd(struct CPU_INSTRUCTION *instr) // Rotate register A through carry left
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.C = (val > 0x7F);

    val = ((val << 1) & 0xFF) | (val >> 7);

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_RRC_r(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];

    cpu_regs.F.C = (* reg) & 1;

    *reg = ((* reg) >> 1) | (((* reg) << 7) & 0xFF);

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RRC_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.C = val & 1;

    val = (val >> 1) | ((val << 7) & 0xFF);

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_RL_r(struct CPU_INSTRUCTION *instr) // do NOT use this for RLA as it has different behavior with regards to setting the Z flag
{
    byte *reg = (byte *)instr->operands[0];

    byte carry = cpu_regs.F.C;
    cpu_regs.F.C = ((* reg) > 0x7F);

    *reg = (((* reg) << 1) & 0xFF) | carry;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RL_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    byte carry = cpu_regs.F.C;
    cpu_regs.F.C = (val > 0x7F);

    val = ((val << 1) & 0xFF) | carry;

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_RR_r(struct CPU_INSTRUCTION *instr) // do NOT use this for RRA as it has different behavior with regards to setting the Z flag
{
    byte *reg = (byte *)instr->operands[0];

    byte carry = cpu_regs.F.C ? 0x80 : 0;
    cpu_regs.F.C = (* reg) & 1;

    // todo: test
    *reg = ((* reg) >> 1) | carry;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_RR_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    byte carry = cpu_regs.F.C ? 0x80 : 0;
    cpu_regs.F.C = val & 1;

    // todo: test
    val = (val >> 1) | carry;

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_SLA_r(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];

    cpu_regs.F.C = ((* reg) & 0x80) >> 7;

    *reg = (* reg) << 1;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_SLA_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.C = (val & 0x80) >> 7;

    val = val << 1;

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_SRA_r(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];

    cpu_regs.F.C = (* reg) & 1;

    *reg = (* reg) >> 1 | ((* reg) & 0x80);

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_SRA_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.C = val & 1;

    val = val >> 1 | (val & 0x80);

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_SRL_r(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];

    cpu_regs.F.C = (* reg) & 1;

    // todo: test
    *reg = (* reg) >> 1;

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
}

__always_inline static void instr_SRL_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.C = val & 1;

    // todo: test
    val = val >> 1;

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_CP_s(struct CPU_INSTRUCTION *instr)
{
    byte previous = cpu_regs.A;
    byte value = (* (byte *)instr->operands[0]);

    cpu_regs.F.H = ((previous & 0xF) < (value & 0xF) ? 1 : 0); // todo: verify that this is actually correct
    cpu_regs.F.C = (previous < value ? 1 : 0);

    previous -= value;

    cpu_regs.F.Z = (previous == 0);
    cpu_regs.F.N = 1;
}

__always_inline static void instr_OR_s(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.A |= (* (byte *)instr->operands[0]);

    cpu_regs.F.Z = (cpu_regs.A == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 0;
}

__always_inline static void instr_RES_r(struct CPU_INSTRUCTION *instr)
{
    (* (byte *)instr->operands[0]) &= 0xFF - (* (byte *)instr->operands[1]);
}

__always_inline static void instr_RES_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);
    val &= 0xFF - (* (byte *)instr->operands[1]);
    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_BIT_r(struct CPU_INSTRUCTION *instr)
{
    cpu_regs.F.Z = (!((* (byte *)instr->operands[0]) & (* (byte *)instr->operands[1])));
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 1;
}

__always_inline static void instr_BIT_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    cpu_regs.F.Z = (!(val & (* (byte *)instr->operands[1])));
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 1;
}

__always_inline static void instr_SET_r(struct CPU_INSTRUCTION *instr)
{
    (* (byte *)instr->operands[0]) |= (* (byte *)instr->operands[1]);
}

__always_inline static void instr_SET_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);
    val |= (* (byte *)instr->operands[1]);
    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_SWAP_r(struct CPU_INSTRUCTION *instr)
{
    byte *reg = (byte *)instr->operands[0];

    // swap high and low nibbles
    (* reg) = (((* reg) & 0x0F) << 4) + (((* reg) & 0xF0) >> 4);

    cpu_regs.F.Z = ((* reg) == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 0;
}

__always_inline static void instr_SWAP_dd(struct CPU_INSTRUCTION *instr)
{
    byte val = mem_read((* instr->operands[0]).w);

    // swap high and low nibbles
    val = ((val & 0x0F) << 4) + ((val & 0xF0) >> 4);

    cpu_regs.F.Z = (val == 0);
    cpu_regs.F.N = 0;
    cpu_regs.F.H = 0;
    cpu_regs.F.C = 0;

    mem_write((* instr->operands[0]).w, val);
}

__always_inline static void instr_PUSH(struct CPU_INSTRUCTION *instr)
{
    push16((* instr->operands[0]));
}

__always_inline static void instr_POP(struct CPU_INSTRUCTION *instr)
{
    (* instr->operands[0]).w = pop16().w;
}

__always_inline static void instr_RST_l(struct CPU_INSTRUCTION *instr)
{
    push16(word(cpu_regs.PC + instr->operands_length + 1));
}

__always_inline static void uinstr_ADD_SP_s(struct CPU_INSTRUCTION *instr)
{
    // if you change anything in here, make sure to also adjust uinstr_LDHL_SP_s

    byte val = (* (byte *)instr->operands[0]);

    uint32_t result = cpu_regs.SP + (int8_t)val;

    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;

    // ADD SP,-1 == ADD SP,0xFF
    // carry from bit 3
    cpu_regs.F.H = (0xF - (cpu_regs.SP & 0xF)) < (val & 0xF);
    // carry from bit 7
    cpu_regs.F.C = (0xFF - (cpu_regs.SP & 0xFF)) < val;

    cpu_regs.SP = result & 0xFFFF;
}

__always_inline static void uinstr_LDHL_SP_s(struct CPU_INSTRUCTION *instr)
{
    // if you change anything in here, make sure to also adjust uinstr_ADD_SP_s

    byte val = (* (byte *)instr->operands[0]);

    uint32_t result = cpu_regs.SP + (int8_t)val;

    cpu_regs.F.Z = 0;
    cpu_regs.F.N = 0;

    // carry from bit 3
    cpu_regs.F.H = (0xF - (cpu_regs.SP & 0xF)) < (val & 0xF);
    // carry from bit 7
    cpu_regs.F.C = (0xFF - (cpu_regs.SP & 0xFF)) < val;

    cpu_regs.HL = result & 0xFFFF;
}

__always_inline static void uinstr_JP_NZ(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 0)
    {
        instr->clock_cycles = 16;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_JP_Z(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 1)
    {
        instr->clock_cycles = 16;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_JP_NC(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 0)
    {
        instr->clock_cycles = 16;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_JP_C(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 1)
    {
        instr->clock_cycles = 16;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_CALL(struct CPU_INSTRUCTION *instr)
{
    push16(word(cpu_regs.PC + instr->operands_length + 1));
}

__always_inline static void uinstr_CALL_NZ(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 0)
    {
        instr->clock_cycles = 24;
        push16(word(cpu_regs.PC + instr->operands_length + 1));
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_CALL_Z(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 1)
    {
        instr->clock_cycles = 24;
        push16(word(cpu_regs.PC + instr->operands_length + 1));
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_CALL_NC(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 0)
    {
        instr->clock_cycles = 24;
        push16(word(cpu_regs.PC + instr->operands_length + 1));
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_CALL_C(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 1)
    {
        instr->clock_cycles = 24;
        push16(word(cpu_regs.PC + instr->operands_length + 1));
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_RET_NZ(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 0)
    {
        instr->clock_cycles = 20;
        word value = pop16();
        instr->operands[0] = value.w;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_RET_Z(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.Z == 1)
    {
        instr->clock_cycles = 20;
        word value = pop16();
        instr->operands[0] = value.w;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_RET_NC(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 0)
    {
        instr->clock_cycles = 20;
        word value = pop16();
        instr->operands[0] = value.w;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_RET_C(struct CPU_INSTRUCTION *instr)
{
    if (cpu_regs.F.C == 1)
    {
        instr->clock_cycles = 20;
        word value = pop16();
        instr->operands[0] = value.w;
        instr->progresser = &prog_jmp;
    }
}

__always_inline static void uinstr_RETI(struct CPU_INSTRUCTION *instr)
{
    // same as EI RET, so transition to enabled immediately
    interrupt_master_enable = 1;
    DEBUG_PRINT(("returning from interrupt\n"));
}

__always_inline static void uinstr_DI(struct CPU_INSTRUCTION *instr)
{
    interrupt_master_enable = 0;
}

__always_inline static void uinstr_EI(struct CPU_INSTRUCTION *instr)
{
    interrupt_master_enable = 3;
}

/* EOF UNSTABLE */

__always_inline void fake_dmg_bootrom() // spoof the results of executing the gameboy (classic) bootrom
{
    cpu_regs.AF = 0x01B0; // GB/SGB: 0x01B0, GBP: 0xFFB0, GBC: 0x11B0
    cpu_regs.BC = 0x0013;
    cpu_regs.DE = 0x00D8;
    cpu_regs.HL = 0x014D;

    cpu_regs.PC = 0x0100; // if bootstrap's validation succeeds, this is where the cartridge's code takes control
    cpu_regs.SP = 0xFFFE;

    mem_write(0xFF05, 0x00);
    mem_write(0xFF06, 0x00);
    mem_write(0xFF07, 0x00);
    mem_write(0xFF10, 0x80);
    mem_write(0xFF11, 0xBF);
    mem_write(0xFF12, 0xF3);
    mem_write(0xFF14, 0xBF);
    mem_write(0xFF16, 0x3F);
    mem_write(0xFF17, 0x00);
    mem_write(0xFF19, 0xBF);
    mem_write(0xFF1A, 0x7F);
    mem_write(0xFF1B, 0xFF);
    mem_write(0xFF1C, 0x9F);
    mem_write(0xFF1E, 0xBF);
    mem_write(0xFF20, 0xFF);
    mem_write(0xFF21, 0x00);
    mem_write(0xFF22, 0x00);
    mem_write(0xFF23, 0xBF);
    mem_write(0xFF24, 0x77);
    mem_write(0xFF25, 0xF3);
    mem_write(0xFF26, 0xF1); // GB: 0xF1, SGB: 0xF0
    mem_write(0xFF40, 0x91);
    mem_write(0xFF42, 0x00);
    mem_write(0xFF43, 0x00);
    mem_write(0xFF45, 0x00);
    mem_write(0xFF47, 0xFC);
    mem_write(0xFF48, 0xFF);
    mem_write(0xFF49, 0xFF);
    mem_write(0xFF4A, 0x00);
    mem_write(0xFF4B, 0x00);
    mem_write(0xFFFF, 0x00);

    enable_bootrom = 0;
}

__always_inline void fake_cgb_bootrom()
{
    fake_dmg_bootrom();

    cpu_regs.AF = 0x11B0; // GB/SGB: 0x01B0, GBP: 0xFFB0, GBC: 0x11B0
}

void cpu_reset()
{
    cpu_alive = 1;

    cpu_regs.A = 0x00;
    cpu_regs.B = 0x00;
    cpu_regs.C = 0x00;
    cpu_regs.D = 0x00;
    cpu_regs.E = 0x00;
    cpu_regs.F.b = 0x00;
    cpu_regs.H = 0x00;
    cpu_regs.L = 0x00;

    cpu_regs.PC = 0x0000;
    cpu_regs.SP = 0x0000;

    enable_bootrom = 1;
    interrupt_master_enable = 0;
}

/* old code, removing soon
void cpu_run()
{
    cpu_alive = 1;

    if (clock_cycle_counter <= 0)
        clock_cycle_counter += 200; // test value

    while (cpu_alive == 1) // main run loop
    {
        cpu_step();
        ppu_step();
    }
}
*/

__always_inline uint32_t cpu_step() // advance one op
{
    struct CPU_INSTRUCTION *instr = cpu_next_instruction(); // fetch next instruction

    if (!cpu_int_halt && !cpu_dma_halt)
    {
        DEBUG_PRINT(("@($%04X): 0x%02X ", cpu_regs.PC, instr->opcode));
        if (instr->operands_length > 0)
            DEBUG_PRINT(("0x%02X ", mem_read(cpu_regs.PC + 1)));
        else
            DEBUG_PRINT(("     "));
        if (instr->operands_length > 1)
            DEBUG_PRINT(("0x%02X ", mem_read(cpu_regs.PC + 2)));
        else
            DEBUG_PRINT(("     "));
        DEBUG_PRINT(("; "));
        DEBUG_PRINT((instr->description));
        DEBUG_PRINT(("\n"));

        (* instr->handler)(instr); // execute next instruction
        (* instr->progresser)(instr);

        // hacky fix for POP AF
        // blargg instr test 1 tries to set these 4 bits to something
        // ...using POP AF, but they should always be 0
        cpu_regs.F.unused = 0;

        global_cycle_counter += instr->clock_cycles;
    }

    if (interrupt_master_enable > 1) // may need to do this after the interrupt handler, not before it (but probably not)
        interrupt_master_enable--;

    handle_interrupts();

    clock_cycle_counter += instr->clock_cycles;

    // recycle memory, so the next line is commented out
    //free(instr);
}

__always_inline int32_t cpu_exec_cycles(int32_t clock_cycles_to_execute)
{
#ifdef __DEBUG
    char input[2];
#endif

    for (clock_cycle_counter = 0; clock_cycle_counter < clock_cycles_to_execute && cpu_alive == 1;)
    {

#ifdef __DEBUG
        //if (cpu_regs.PC == 0x100)
            //single_steps = 1;

        if (activate_single_stepping_on_condition)
            printf("A: 0x%02X B: 0x%02X C: 0x%02X D: 0x%02X E: 0x%02X F: 0x%02X H: 0x%02X L: 0x%02X PC: 0x%04X SP: 0x%04X Z: %d N: %d H: %d C: %d\n", \
              cpu_regs.A, cpu_regs.B, cpu_regs.C, cpu_regs.D, cpu_regs.E, cpu_regs.F, cpu_regs.H, cpu_regs.L, cpu_regs.PC, cpu_regs.SP, cpu_regs.F.Z, \
              cpu_regs.F.N, cpu_regs.F.H, cpu_regs.F.C);

        if (till_zero && cpu_regs.F.Z == 1)
            single_steps = 1;

        if (till_carry && cpu_regs.F.C == 1)
            single_steps = 1;

        if (single_steps)
        {
            printf("-Paused-\n");
            fgets(input, sizeof(input), stdin);
            if (strcmp(input, "z") == 0)
            {
                till_zero = 1;
                single_steps = 0;
            }
            else if (strcmp(input, "c") == 0)
            {
                till_carry = 1;
                single_steps = 0;
            }
            else if (strcmp(input, "r") == 0)
            {
                till_zero = 0;
                till_carry = 0;
                single_steps = 0;
            }
            else
            {
                till_zero = 0;
                till_carry = 0;
            }
        }
#endif

        cpu_step();
    }

    return (clock_cycles_to_execute - clock_cycle_counter);
}

void cpu_break()
{
    cpu_alive = 0;
}

__always_inline void handle_interrupts()
{
    if (mem.map.interrupt_flag_reg.b > 0)
        cpu_int_halt = 0;

    if (interrupt_master_enable != 1)
        return;

    if (ppu_regs.stat->mode == 1 && SHOULD_INT(VBLANK))
    {
        // vblank interrupt
        interrupt_master_enable = 0;
        push16(word(cpu_regs.PC));
        cpu_regs.PC = 0x0040;
        mem.map.interrupt_flag_reg.VBLANK = 0;
        DEBUG_PRINT(("entering vblank interrupt\n"));
        return;
    }

    if (SHOULD_INT(LCD_STAT))
    {
        // lcd stat interrupt
        interrupt_master_enable = 0;
        push16(word(cpu_regs.PC));
        cpu_regs.PC = 0x0048;
        mem.map.interrupt_flag_reg.LCD_STAT = 0;
        DEBUG_PRINT(("entering lcd stat interrupt\n"));
        return;
    }

    if (SHOULD_INT(TIMER))
    {
        // timer interrupt
        interrupt_master_enable = 0;
        push16(word(cpu_regs.PC));
        cpu_regs.PC = 0x0050;
        mem.map.interrupt_flag_reg.TIMER = 0;
        DEBUG_PRINT(("entering timer interrupt\n"));
        return;
    }

    if (SHOULD_INT(SERIAL))
        // serial interrupt
        return;

    if (SHOULD_INT(JOYPAD))
    {
        // joypad interrupt
        interrupt_master_enable = 0;
        push16(word(cpu_regs.PC));
        cpu_regs.PC = 0x0060;
        mem.map.interrupt_flag_reg.JOYPAD = 0;
        DEBUG_PRINT(("entering joypad interrupt\n"));
        return;
    }
}

word inst_value = word(0x0000);
__always_inline struct CPU_INSTRUCTION *cpu_next_instruction()
{
    // recycle memory, so the next line is commented out
    //struct CPU_INSTRUCTION *instr = calloc(sizeof(struct CPU_INSTRUCTION), 1);

    struct CPU_INSTRUCTION *instr = &p_instr;

    instr->handler = &instr_nop;
    instr->progresser = &prog_default;
    instr->operands_length = 0;

    inst_value.w = 0;

    if (mem_read(cpu_regs.PC) != 0xCB)
    { // primary instruction table
        instr->opcode = mem_read(cpu_regs.PC);

        switch (instr->opcode)
        {
            case 0x00:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "NOP";
                break;

            case 0x01:
                instr->handler = &instr_LD_rr_ss;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                instr->operands[0] = (word *)&cpu_regs.BC;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD BC,d16";
                break;

            case 0x02:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.BC;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD (BC),A";
                break;

            case 0x03:
                instr->handler = &instr_INC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.BC;
                instr->description = "INC BC";
                break;

            case 0x04:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "INC B";
                break;

            case 0x05:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "DEC B";
                break;

            case 0x06:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD B,d8";
                break;

            case 0x07:
                instr->handler = &instr_RLCA;
                instr->clock_cycles = 4;
                instr->description = "RLCA";
                break;

            case 0x08:
                instr->handler = &instr_LD_dd_ss;
                instr->clock_cycles = 20;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->operands[1] = (word *)&cpu_regs.SP;
                instr->description = "LD (a16),SP";
                break;

            case 0x09:
                instr->handler = &instr_ADD_rr_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.BC;
                instr->description = "ADD HL,BC";
                break;

            case 0x0A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.BC);
                instr->operands[1] = &inst_value;
                instr->description = "LD A,(BC)";
                break;

            case 0x0B:
                instr->handler = &instr_DEC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.BC;
                instr->description = "DEC BC";
                break;

            case 0x0C:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "INC C";
                break;

            case 0x0D:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "DEC C";
                break;

            case 0x0E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD C,d8";
                break;

            case 0x0F:
                instr->handler = &instr_RRCA;
                instr->clock_cycles = 4;
                instr->description = "RRCA";
                break;

            case 0x10:
                instr->handler = &instr_STOP;
                instr->clock_cycles = 4;
                instr->description = "STOP";
                break;

            case 0x11:
                instr->handler = &instr_LD_rr_ss;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                instr->operands[0] = (word *)&cpu_regs.DE;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD DE,d16";
                break;

            case 0x12:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.DE;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD (DE),A";
                break;

            case 0x13:
                instr->handler = &instr_INC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.DE;
                instr->description = "INC DE";
                break;

            case 0x14:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "INC D";
                break;

            case 0x15:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "DEC D";
                break;

            case 0x16:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD D,d8";
                break;

            case 0x17:
                instr->handler = &instr_RLA;
                instr->clock_cycles = 4;
                instr->description = "RLA";
                break;

            case 0x18:
                instr->handler = &instr_nop;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 12;
                instr->operands_length = 1;
                inst_value.w = cpu_regs.PC + (int8_t)mem_read(cpu_regs.PC + 1) + 2;
                instr->operands[0] = inst_value.w;
                instr->description = "JR r8";
                break;

            case 0x19:
                instr->handler = &instr_ADD_rr_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.DE;
                instr->description = "ADD HL,DE";
                break;

            case 0x1A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.DE);
                instr->operands[1] = &inst_value;
                instr->description = "LD A,(DE)";
                break;

            case 0x1B:
                instr->handler = &instr_DEC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.DE;
                instr->description = "DEC DE";
                break;

            case 0x1C:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "INC E";
                break;

            case 0x1D:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "DEC E";
                break;

            case 0x1E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD E,d8";
                break;

            case 0x1F:
                instr->handler = &instr_RRA;
                instr->clock_cycles = 4;
                instr->description = "RRA";
                break;

            case 0x20:
                if (!cpu_regs.F.Z)
                {
                    instr->handler = &instr_nop;
                    instr->progresser = &prog_jmp;
                    instr->clock_cycles = 12; // or 8 if action is not taken (zero)
                    inst_value.w = cpu_regs.PC + (int8_t)mem_read(cpu_regs.PC + 1) + 2;
                    instr->operands[0] = inst_value.w;
                }
                else
                {
                    instr->handler = &instr_nop;
                    instr->clock_cycles = 8; // or 12 if action is taken (not zero)
                }
                instr->operands_length = 1;
                instr->description = "JR NZ,r8";
                break;

            case 0x21:
                instr->handler = &instr_LD_rr_ss;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD HL,d16";
                break;

            case 0x22:
                instr->handler = &uinstr_LDI_lHL_A;
                instr->clock_cycles = 8;
                instr->description = "LDI (HL),A";
                break;

            case 0x23:
                instr->handler = &instr_INC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "INC HL";
                break;

            case 0x24:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "INC H";
                break;

            case 0x25:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "DEC H";
                break;

            case 0x26:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD H,d8";
                break;

            case 0x27:
                instr->handler = &instr_DAA;
                instr->clock_cycles = 4;
                instr->description = "DAA";
                break;

            case 0x28:
                if (cpu_regs.F.Z)
                {
                    instr->handler = &instr_nop;
                    instr->progresser = &prog_jmp;
                    instr->clock_cycles = 12; // or 8 if action is not taken (not zero)
                    inst_value.w = cpu_regs.PC + (int8_t)mem_read(cpu_regs.PC + 1) + 2;
                    instr->operands[0] = inst_value.w;
                }
                else
                {
                    instr->handler = &instr_nop;
                    instr->clock_cycles = 8; // or 12 if action is taken (zero)
                }
                instr->operands_length = 1;
                instr->description = "JR Z,r8";
                break;

            case 0x29:
                instr->handler = &instr_ADD_rr_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.HL;
                instr->description = "ADD HL,HL";
                break;

            case 0x2A:
                instr->handler = &uinstr_LDI_A_lHL;
                instr->clock_cycles = 8;
                instr->description = "LDI A,(HL)";
                break;

            case 0x2B:
                instr->handler = &instr_DEC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "DEC HL";
                break;

            case 0x2C:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "INC L";
                break;

            case 0x2D:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "DEC L";
                break;

            case 0x2E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD L,d8";
                break;

            case 0x2F:
                instr->handler = &instr_CPL;
                instr->clock_cycles = 4;
                instr->description = "CPL";
                break;

            case 0x30:
                if (!cpu_regs.F.C)
                {
                    instr->handler = &instr_nop;
                    instr->progresser = &prog_jmp;
                    instr->clock_cycles = 12; // or 8 if action is not taken (carry)
                    inst_value.w = cpu_regs.PC + (int8_t)mem_read(cpu_regs.PC + 1) + 2;
                    instr->operands[0] = inst_value.w;
                }
                else
                {
                    instr->handler = &instr_nop;
                    instr->clock_cycles = 8; // or 12 if action is taken (not carry)
                }
                instr->operands_length = 1;
                instr->description = "JR NC,r8";
                break;

            case 0x31:
                instr->handler = &instr_LD_rr_ss;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                instr->operands[0] = (word *)&cpu_regs.SP;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD SP,d16";
                break;

            case 0x32:
                instr->handler = &uinstr_LDD_lHL_A;
                instr->clock_cycles = 8;
                instr->description = "LD (HL-),A";
                break;

            case 0x33:
                instr->handler = &instr_INC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.SP;
                instr->description = "INC SP";
                break;

            case 0x34:
                instr->handler = &instr_INC_dd;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "INC (HL)";
                break;

            case 0x35:
                instr->handler = &instr_DEC_dd;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "DEC (HL)";
                break;

            case 0x36:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 12;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = (word *)&inst_value;
                instr->description = "LD (HL),d8";
                break;

            case 0x37:
                instr->handler = &instr_SCF;
                instr->clock_cycles = 4;
                instr->description = "SCF";
                break;

            case 0x38:
                if (cpu_regs.F.C)
                {
                    instr->handler = &instr_nop;
                    instr->progresser = &prog_jmp;
                    instr->clock_cycles = 12; // or 8 if action is not taken (not carry)
                    inst_value.w = cpu_regs.PC + (int8_t)mem_read(cpu_regs.PC + 1) + 2;
                    instr->operands[0] = inst_value.w;
                }
                else
                {
                    instr->handler = &instr_nop;
                    instr->clock_cycles = 8; // or 12 if action is taken (carry)
                }
                instr->operands_length = 1;
                instr->description = "JR C,r8";
                break;

            case 0x39:
                instr->handler = &instr_ADD_rr_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.SP;
                instr->description = "ADD HL,SP";
                break;

            case 0x3A:
                instr->handler = &uinstr_LDD_A_lHL;
                instr->clock_cycles = 8;
                instr->description = "LDD A,(HL)";
                break;

            case 0x3B:
                instr->handler = &instr_DEC_rr;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.SP;
                instr->description = "DEC SP";
                break;

            case 0x3C:
                instr->handler = &instr_INC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "INC A";
                break;

            case 0x3D:
                instr->handler = &instr_DEC_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "DEC A";
                break;

            case 0x3E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "LD A,d8";
                break;

            case 0x3F:
                instr->handler = &instr_CCF;
                instr->clock_cycles = 4;
                instr->description = "CCF";
                break;

            case 0x40:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD B,B";
                break;

            case 0x41:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD B,C";
                break;

            case 0x42:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD B,D";
                break;

            case 0x43:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD B,E";
                break;

            case 0x44:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD B,H";
                break;

            case 0x45:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD B,L";
                break;

            case 0x46:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD B,(HL)";
                break;

            case 0x47:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD B,A";
                break;

            case 0x48:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD C,B";
                break;

            case 0x49:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD C,C";
                break;

            case 0x4A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD C,D";
                break;

            case 0x4B:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD C,E";
                break;

            case 0x4C:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD C,H";
                break;

            case 0x4D:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD C,L";
                break;

            case 0x4E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD C,(HL)";
                break;

            case 0x4F:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD C,A";
                break;

            case 0x50:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD D,B";
                break;

            case 0x51:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD D,C";
                break;

            case 0x52:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD D,D";
                break;

            case 0x53:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD D,E";
                break;

            case 0x54:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD D,H";
                break;

            case 0x55:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD D,L";
                break;

            case 0x56:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD D,(HL)";
                break;

            case 0x57:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD D,A";
                break;

            case 0x58:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD E,B";
                break;

            case 0x59:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD E,C";
                break;

            case 0x5A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD E,D";
                break;

            case 0x5B:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD E,E";
                break;

            case 0x5C:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD E,H";
                break;

            case 0x5D:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD E,L";
                break;

            case 0x5E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD E,(HL)";
                break;

            case 0x5F:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD E,A";
                break;

            case 0x60:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD H,B";
                break;

            case 0x61:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD H,C";
                break;

            case 0x62:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD H,D";
                break;

            case 0x63:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD H,E";
                break;

            case 0x64:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD H,H";
                break;

            case 0x65:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD H,L";
                break;

            case 0x66:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD H,(HL)";
                break;

            case 0x67:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD H,A";
                break;

            case 0x68:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD L,B";
                break;

            case 0x69:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD L,C";
                break;

            case 0x6A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD L,D";
                break;

            case 0x6B:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD L,E";
                break;

            case 0x6C:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD L,H";
                break;

            case 0x6D:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD L,L";
                break;

            case 0x6E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD L,(HL)";
                break;

            case 0x6F:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD L,A";
                break;

            case 0x70:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD (HL),B";
                break;

            case 0x71:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD (HL),C";
                break;

            case 0x72:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD (HL),D";
                break;

            case 0x73:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD (HL),E";
                break;

            case 0x74:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD (HL),H";
                break;

            case 0x75:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD (HL),L";
                break;

            case 0x76:
                instr->handler = &instr_HALT;
                instr->clock_cycles = 4;
                instr->description = "HALT";
                break;

            case 0x77:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD (HL),A";
                break;

            case 0x78:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "LD A,B";
                break;

            case 0x79:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "LD A,C";
                break;

            case 0x7A:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "LD A,D";
                break;

            case 0x7B:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "LD A,E";
                break;

            case 0x7C:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "LD A,H";
                break;

            case 0x7D:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "LD A,L";
                break;

            case 0x7E:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "LD A,(HL)";
                break;

            case 0x7F:
                instr->handler = &instr_nop;
                instr->clock_cycles = 4;
                instr->description = "LD A,A";
                break;

            case 0x80:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "ADD A,B";
                break;

            case 0x81:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "ADD A,C";
                break;

            case 0x82:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "ADD A,D";
                break;

            case 0x83:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "ADD A,E";
                break;

            case 0x84:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "ADD A,H";
                break;

            case 0x85:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "ADD A,L";
                break;

            case 0x86:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "ADD A,(HL)";
                break;

            case 0x87:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "ADD A,A";
                break;

            case 0x88:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "ADC A,B";
                break;

            case 0x89:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "ADC A,C";
                break;

            case 0x8A:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "ADC A,D";
                break;

            case 0x8B:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "ADC A,E";
                break;

            case 0x8C:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "ADC A,H";
                break;

            case 0x8D:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "ADC A,L";
                break;

            case 0x8E:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "ADC A,(HL)";
                break;

            case 0x8F:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "ADC A,A";
                break;

            case 0x90:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "SUB B";
                break;

            case 0x91:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "SUB C";
                break;

            case 0x92:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "SUB D";
                break;

            case 0x93:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "SUB E";
                break;

            case 0x94:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "SUB H";
                break;

            case 0x95:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "SUB L";
                break;

            case 0x96:
                instr->handler = &instr_SUB_ss;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "SUB (HL)";
                break;

            case 0x97:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "SUB A";
                break;

            case 0x98:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.B;
                instr->description = "SBC A,B";
                break;

            case 0x99:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.C;
                instr->description = "SBC A,C";
                break;

            case 0x9A:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.D;
                instr->description = "SBC A,D";
                break;

            case 0x9B:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.E;
                instr->description = "SBC A,E";
                break;

            case 0x9C:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.H;
                instr->description = "SBC A,H";
                break;

            case 0x9D:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.L;
                instr->description = "SBC A,L";
                break;

            case 0x9E:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[1] = &inst_value;
                instr->description = "SBC A,(HL)";
                break;

            case 0x9F:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "SBC A,A";
                break;

            case 0xA0:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "AND B";
                break;

            case 0xA1:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "AND C";
                break;

            case 0xA2:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "AND D";
                break;

            case 0xA3:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "AND E";
                break;

            case 0xA4:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "AND H";
                break;

            case 0xA5:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "AND L";
                break;

            case 0xA6:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 8;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[0] = &inst_value;
                instr->description = "AND (HL)";
                break;

            case 0xA7:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "AND A";
                break;

            case 0xA8:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "XOR B";
                break;

            case 0xA9:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "XOR C";
                break;

            case 0xAA:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "XOR D";
                break;

            case 0xAB:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "XOR E";
                break;

            case 0xAC:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "XOR H";
                break;

            case 0xAD:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "XOR L";
                break;

            case 0xAE:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 8;
                inst_value.w = mem_read(cpu_regs.HL);
                instr->operands[0] = &inst_value;
                instr->description = "XOR (HL)";
                break;

            case 0xAF:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "XOR A";
                break;

            /* UNSTABLE */

            case 0xB0:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "OR B";
                break;

            case 0xB1:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "OR C";
                break;

            case 0xB2:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "OR D";
                break;

            case 0xB3:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "OR E";
                break;

            case 0xB4:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "OR H";
                break;

            case 0xB5:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "OR L";
                break;

            case 0xB6:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 8;
                inst_value.w = mem_read(cpu_regs.HL);
                instr->operands[0] = &inst_value;
                instr->description = "OR (HL)";
                break;

            case 0xB7:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "OR A";
                break;

            case 0xB8:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "CP B";
                break;

            case 0xB9:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "CP C";
                break;

            case 0xBA:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "CP D";
                break;

            case 0xBB:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "CP E";
                break;

            case 0xBC:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "CP H";
                break;

            case 0xBD:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "CP L";
                break;

            case 0xBE:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 8;
                inst_value.b.l = mem_read(cpu_regs.HL);
                instr->operands[0] = &inst_value;
                instr->description = "CP (HL)";
                break;

            case 0xBF:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 4;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "CP A";
                break;

            case 0xC0:
                instr->handler = &uinstr_RET_NZ;
                instr->clock_cycles = 8;
                instr->description = "RET NZ";
                break;

            case 0xC1:
                instr->handler = &instr_POP;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.BC;
                instr->description = "POP BC";
                break;

            case 0xC2:
                instr->handler = &uinstr_JP_NZ;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "JP NZ,a16";
                break;

            case 0xC3:
                instr->handler = &instr_nop;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "JP a16";
                break;

            case 0xC4:
                instr->handler = &uinstr_CALL_NZ;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "CALL NZ,a16";
                break;

            case 0xC5:
                instr->handler = &instr_PUSH;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.BC;
                instr->description = "PUSH BC";
                break;

            case 0xC6:
                instr->handler = &instr_ADD_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "ADD A,d8";
                break;

            case 0xC7:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x00;
                instr->description = "RST 00H";
                break;

            case 0xC8:
                instr->handler = &uinstr_RET_Z;
                instr->clock_cycles = 8;
                instr->description = "RET Z";
                break;

            case 0xC9:
                instr->handler = &instr_nop;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                inst_value = pop16();
                instr->operands[0] = inst_value.w;
                instr->description = "RET";
                break;

            case 0xCA:
                instr->handler = &uinstr_JP_Z;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "JP Z,a16";
                break;

            case 0xCC:
                instr->handler = &uinstr_CALL_Z;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "CALL Z,a16";
                break;

            case 0xCD:
                instr->handler = &uinstr_CALL;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 24;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "CALL a16";
                break;

            case 0xCE:
                instr->handler = &instr_ADC_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "ADC A,d8";
                break;

            case 0xCF:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x08;
                instr->description = "RST 08H";
                break;

            case 0xD0:
                instr->handler = &uinstr_RET_NC;
                instr->clock_cycles = 8;
                instr->description = "RET NC";
                break;

            case 0xD1:
                instr->handler = &instr_POP;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.DE;
                instr->description = "POP DE";
                break;

            case 0xD2:
                instr->handler = &uinstr_JP_NC;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "JP NC,a16";
                break;

            case 0xD4:
                instr->handler = &uinstr_CALL_NC;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "CALL NC,a16";
                break;

            case 0xD5:
                instr->handler = &instr_PUSH;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.DE;
                instr->description = "PUSH DE";
                break;

            case 0xD6:
                instr->handler = &instr_SUB_r;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "SUB d8";
                break;

            case 0xD7:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x10;
                instr->description = "RST 10H";
                break;

            case 0xD8:
                instr->handler = &uinstr_RET_C;
                instr->clock_cycles = 8;
                instr->description = "RET C";
                break;

            case 0xD9:
                instr->handler = &uinstr_RETI;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                inst_value = pop16();
                instr->operands[0] = inst_value.w;
                instr->description = "RETI";
                break;

            case 0xDA:
                instr->handler = &uinstr_JP_C;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "JP C,a16";
                break;

            case 0xDC:
                instr->handler = &uinstr_CALL_C;
                instr->clock_cycles = 12;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = inst_value.w;
                instr->description = "CALL C,a16";
                break;

            case 0xDE:
                instr->handler = &instr_SBC_r_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[1] = &inst_value;
                instr->description = "SBC A,d8";
                break;

            case 0xDF:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x18;
                instr->description = "RST 18H";
                break;

            case 0xE0:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 12;
                instr->operands_length = 1;
                inst_value.w = 0xFF00;
                inst_value.w += mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LDH (a8),A";
                break;

            case 0xE1:
                instr->handler = &instr_POP;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "POP HL";
                break;

            case 0xE2:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 8;
                //instr->operands_length = 1;
                // todo: find out if ^this needs to be one
                inst_value = word(0xFF00 + cpu_regs.C);
                instr->operands[0] = &inst_value;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD (C),A";
                break;

            case 0xE5:
                instr->handler = &instr_PUSH;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "PUSH HL";
                break;

            case 0xE6:
                instr->handler = &instr_AND_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "AND d8";
                break;

            case 0xE7:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x20;
                instr->description = "RST 20H";
                break;

            case 0xE8:
                instr->handler = &uinstr_ADD_SP_s;
                instr->clock_cycles = 16;
                instr->operands_length = 1;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "ADD SP,r8";
                break;

            case 0xE9: // this is correct; not memory location HL, but value in
                instr->handler = &instr_nop;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 4;
                instr->operands[0] = cpu_regs.HL;
                instr->description = "JP (HL)";
                break;

            case 0xEA:
                instr->handler = &instr_LD_dd_s;
                instr->clock_cycles = 16;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->operands[1] = (word *)&cpu_regs.A;
                instr->description = "LD (a16),A";
                break;

            case 0xEE:
                instr->handler = &instr_XOR_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "XOR d8";
                break;

            case 0xEF:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x28;
                instr->description = "RST 28H";
                break;

            case 0xF0:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 12;
                instr->operands_length = 1;
                inst_value.w = 0xFF00;
                inst_value.w += mem_read(cpu_regs.PC + 1);
                inst_value.w = mem_read(inst_value.w);
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = &inst_value;
                instr->description = "LDH A,(a8)";
                break;

            case 0xF1:
                instr->handler = &instr_POP;
                instr->clock_cycles = 12;
                instr->operands[0] = (word *)&cpu_regs.AF;
                instr->description = "POP AF";
                break;

            case 0xF2:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 8;
                //instr->operands_length = 1;
                // todo: find out if ^this needs to be one
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = mem_read(0xFF00 + cpu_regs.C);
                instr->operands[1] = &inst_value;
                instr->description = "LD (C),A";
                break;

            case 0xF3:
                instr->handler = &uinstr_DI;
                instr->clock_cycles = 4;
                instr->description = "DI";
                break;

            case 0xF5:
                instr->handler = &instr_PUSH;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.AF;
                instr->description = "PUSH AF";
                break;

            case 0xF6:
                instr->handler = &instr_OR_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "OR d8";
                break;

            case 0xF7:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x30;
                instr->description = "RST 30H";
                break;

            case 0xF8:
                instr->handler = &uinstr_LDHL_SP_s;
                instr->clock_cycles = 12;
                instr->operands_length = 1;
                inst_value.w = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "LD HL,SP+r8";
                break;

            case 0xF9:
                instr->handler = &instr_LD_rr_ss;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.SP;
                instr->operands[1] = (word *)&cpu_regs.HL;
                instr->description = "LD SP,HL";
                break;

            case 0xFA:
                instr->handler = &instr_LD_r_s;
                instr->clock_cycles = 16;
                instr->operands_length = 2;
                inst_value = mem_read_16(cpu_regs.PC + 1);
                inst_value.w = mem_read(inst_value.w);
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->operands[1] = &inst_value;
                instr->description = "LD A,(a16)";
                break;

            case 0xFB:
                instr->handler = &uinstr_EI;
                instr->clock_cycles = 4;
                instr->description = "EI";
                break;

            case 0xFE:
                instr->handler = &instr_CP_s;
                instr->clock_cycles = 8;
                instr->operands_length = 1;
                inst_value.b.l = mem_read(cpu_regs.PC + 1);
                instr->operands[0] = &inst_value;
                instr->description = "CP d8";
                break;

            case 0xFF:
                instr->handler = &instr_RST_l;
                instr->progresser = &prog_jmp;
                instr->clock_cycles = 16;
                instr->operands[0] = 0x38;
                instr->description = "RST 38H";
                break;

            /* EOF UNSTABLE */

            default:
                instr->handler = &instr_illegal;
                instr->description = "ILLEGAL INSTRUCTION";
                break;
        }
    }
    else
    { // secondary instruction table
        DEBUG_PRINT(("@($%04X): 0xCB           ; Use secondary instruction table for next opcode\n", cpu_regs.PC));

        cpu_regs.PC++;
        instr->opcode = mem_read(cpu_regs.PC);

        switch (instr->opcode)
        {
            /* UNSTABLE */

            case 0x00:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "RLC B";
                break;

            case 0x01:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "RLC C";
                break;

            case 0x02:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "RLC D";
                break;

            case 0x03:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "RLC E";
                break;

            case 0x04:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "RLC H";
                break;

            case 0x05:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "RLC L";
                break;

            case 0x06:
                instr->handler = &instr_RLC_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "RLC (HL)";
                break;

            case 0x07:
                instr->handler = &instr_RLC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "RLC A";
                break;

            case 0x08:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "RRC B";
                break;

            case 0x09:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "RRC C";
                break;

            case 0x0A:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "RRC D";
                break;

            case 0x0B:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "RRC E";
                break;

            case 0x0C:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "RRC H";
                break;

            case 0x0D:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "RRC L";
                break;

            case 0x0E:
                instr->handler = &instr_RRC_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "RRC (HL)";
                break;

            case 0x0F:
                instr->handler = &instr_RRC_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "RRC A";
                break;

            case 0x10:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "RL B";
                break;

            case 0x11:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "RL C";
                break;

            case 0x12:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "RL D";
                break;

            case 0x13:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "RL E";
                break;

            case 0x14:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "RL H";
                break;

            case 0x15:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "RL L";
                break;

            case 0x16:
                instr->handler = &instr_RL_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "RL (HL)";
                break;

            case 0x17:
                instr->handler = &instr_RL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "RL A";
                break;

            case 0x18:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "RR B";
                break;

            case 0x19:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "RR C";
                break;

            case 0x1A:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "RR D";
                break;

            case 0x1B:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "RR E";
                break;

            case 0x1C:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "RR H";
                break;

            case 0x1D:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "RR L";
                break;

            case 0x1E:
                instr->handler = &instr_RR_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "RR (HL)";
                break;

            case 0x1F:
                instr->handler = &instr_RR_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "RR A";
                break;

            case 0x20:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "SLA B";
                break;

            case 0x21:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "SLA C";
                break;

            case 0x22:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "SLA D";
                break;

            case 0x23:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "SLA E";
                break;

            case 0x24:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "SLA H";
                break;

            case 0x25:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "SLA L";
                break;

            case 0x26:
                instr->handler = &instr_SLA_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "SLA (HL)";
                break;

            case 0x27:
                instr->handler = &instr_SLA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "SLA A";
                break;

            case 0x28:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "SRA B";
                break;

            case 0x29:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "SRA C";
                break;

            case 0x2A:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "SRA D";
                break;

            case 0x2B:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "SRA E";
                break;

            case 0x2C:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "SRA H";
                break;

            case 0x2D:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "SRA L";
                break;

            case 0x2E:
                instr->handler = &instr_SRA_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "SRA (HL)";
                break;

            case 0x2F:
                instr->handler = &instr_SRA_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "SRA A";
                break;

            case 0x30:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "SWAP B";
                break;

            case 0x31:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "SWAP C";
                break;

            case 0x32:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "SWAP D";
                break;

            case 0x33:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "SWAP E";
                break;

            case 0x34:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "SWAP H";
                break;

            case 0x35:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "SWAP L";
                break;

            case 0x36:
                instr->handler = &instr_SWAP_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "SWAP (HL)";
                break;

            case 0x37:
                instr->handler = &instr_SWAP_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "SWAP A";
                break;

            case 0x38:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                instr->description = "SRL B";
                break;

            case 0x39:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                instr->description = "SRL C";
                break;

            case 0x3A:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                instr->description = "SRL D";
                break;

            case 0x3B:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                instr->description = "SRL E";
                break;

            case 0x3C:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                instr->description = "SRL H";
                break;

            case 0x3D:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                instr->description = "SRL L";
                break;

            case 0x3E:
                instr->handler = &instr_SRL_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                instr->description = "SRL (HL)";
                break;

            case 0x3F:
                instr->handler = &instr_SRL_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                instr->description = "SRL A";
                break;

            case 0x40:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,B";
                break;

            case 0x41:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,C";
                break;

            case 0x42:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,D";
                break;

            case 0x43:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,E";
                break;

            case 0x44:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,H";
                break;

            case 0x45:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,L";
                break;

            case 0x46:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,(HL)";
                break;

            case 0x47:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 0,A";
                break;

            case 0x48:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,B";
                break;

            case 0x49:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,C";
                break;

            case 0x4A:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,D";
                break;

            case 0x4B:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,E";
                break;

            case 0x4C:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,H";
                break;

            case 0x4D:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,L";
                break;

            case 0x4E:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,(HL)";
                break;

            case 0x4F:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 1,A";
                break;

            case 0x50:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,B";
                break;

            case 0x51:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,C";
                break;

            case 0x52:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,D";
                break;

            case 0x53:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,E";
                break;

            case 0x54:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,H";
                break;

            case 0x55:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,L";
                break;

            case 0x56:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,(HL)";
                break;

            case 0x57:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 2,A";
                break;

            case 0x58:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,B";
                break;

            case 0x59:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,C";
                break;

            case 0x5A:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,D";
                break;

            case 0x5B:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,E";
                break;

            case 0x5C:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,H";
                break;

            case 0x5D:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,L";
                break;

            case 0x5E:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,(HL)";
                break;

            case 0x5F:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 3,A";
                break;

            case 0x60:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,B";
                break;

            case 0x61:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,C";
                break;

            case 0x62:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,D";
                break;

            case 0x63:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,E";
                break;

            case 0x64:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,H";
                break;

            case 0x65:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,L";
                break;

            case 0x66:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,(HL)";
                break;

            case 0x67:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 4,A";
                break;

            case 0x68:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,B";
                break;

            case 0x69:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,C";
                break;

            case 0x6A:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,D";
                break;

            case 0x6B:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,E";
                break;

            case 0x6C:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,H";
                break;

            case 0x6D:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,L";
                break;

            case 0x6E:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,(HL)";
                break;

            case 0x6F:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 5,A";
                break;

            case 0x70:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,B";
                break;

            case 0x71:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,C";
                break;

            case 0x72:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,D";
                break;

            case 0x73:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,E";
                break;

            case 0x74:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,H";
                break;

            case 0x75:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,L";
                break;

            case 0x76:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,(HL)";
                break;

            case 0x77:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 6,A";
                break;

            case 0x78:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,B";
                break;

            case 0x79:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,C";
                break;

            case 0x7A:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,D";
                break;

            case 0x7B:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,E";
                break;

            case 0x7C:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,H";
                break;

            case 0x7D:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,L";
                break;

            case 0x7E:
                instr->handler = &instr_BIT_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,(HL)";
                break;

            case 0x7F:
                instr->handler = &instr_BIT_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "BIT 7,A";
                break;

            case 0x80:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,B";
                break;

            case 0x81:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,C";
                break;

            case 0x82:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,D";
                break;

            case 0x83:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,E";
                break;

            case 0x84:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,H";
                break;

            case 0x85:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,L";
                break;

            case 0x86:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,(HL)";
                break;

            case 0x87:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "RES 0,A";
                break;

            case 0x88:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,B";
                break;

            case 0x89:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,C";
                break;

            case 0x8A:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,D";
                break;

            case 0x8B:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,E";
                break;

            case 0x8C:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,H";
                break;

            case 0x8D:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,L";
                break;

            case 0x8E:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,(HL)";
                break;

            case 0x8F:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "RES 1,A";
                break;

            case 0x90:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,B";
                break;

            case 0x91:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,C";
                break;

            case 0x92:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,D";
                break;

            case 0x93:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,E";
                break;

            case 0x94:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,H";
                break;

            case 0x95:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,L";
                break;

            case 0x96:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,(HL)";
                break;

            case 0x97:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "RES 2,A";
                break;

            case 0x98:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,B";
                break;

            case 0x99:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,C";
                break;

            case 0x9A:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,D";
                break;

            case 0x9B:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,E";
                break;

            case 0x9C:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,H";
                break;

            case 0x9D:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,L";
                break;

            case 0x9E:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,(HL)";
                break;

            case 0x9F:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 3,A";
                break;

            case 0xA0:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,B";
                break;

            case 0xA1:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,C";
                break;

            case 0xA2:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,D";
                break;

            case 0xA3:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,E";
                break;

            case 0xA4:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,H";
                break;

            case 0xA5:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,L";
                break;

            case 0xA6:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,(HL)";
                break;

            case 0xA7:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 4,A";
                break;

            case 0xA8:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,B";
                break;

            case 0xA9:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,C";
                break;

            case 0xAA:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,D";
                break;

            case 0xAB:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,E";
                break;

            case 0xAC:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,H";
                break;

            case 0xAD:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,L";
                break;

            case 0xAE:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,(HL)";
                break;

            case 0xAF:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 5,A";
                break;

            case 0xB0:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,B";
                break;

            case 0xB1:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,C";
                break;

            case 0xB2:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,D";
                break;

            case 0xB3:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,E";
                break;

            case 0xB4:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,H";
                break;

            case 0xB5:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,L";
                break;

            case 0xB6:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,(HL)";
                break;

            case 0xB7:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 6,A";
                break;

            case 0xB8:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,B";
                break;

            case 0xB9:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,C";
                break;

            case 0xBA:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,D";
                break;

            case 0xBB:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,E";
                break;

            case 0xBC:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,H";
                break;

            case 0xBD:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,L";
                break;

            case 0xBE:
                instr->handler = &instr_RES_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,(HL)";
                break;

            case 0xBF:
                instr->handler = &instr_RES_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.b.l = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "RES 7,A";
                break;

            case 0xC0:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,B";
                break;

            case 0xC1:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,C";
                break;

            case 0xC2:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,D";
                break;

            case 0xC3:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,E";
                break;

            case 0xC4:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,H";
                break;

            case 0xC5:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,L";
                break;

            case 0xC6:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,(HL)";
                break;

            case 0xC7:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000001;
                instr->operands[1] = &inst_value;
                instr->description = "SET 0,A";
                break;

            case 0xC8:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,B";
                break;

            case 0xC9:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,C";
                break;

            case 0xCA:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,D";
                break;

            case 0xCB:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,E";
                break;

            case 0xCC:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,H";
                break;

            case 0xCD:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,L";
                break;

            case 0xCE:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,(HL)";
                break;

            case 0xCF:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000010;
                instr->operands[1] = &inst_value;
                instr->description = "SET 1,A";
                break;

            case 0xD0:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,B";
                break;

            case 0xD1:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,C";
                break;

            case 0xD2:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,D";
                break;

            case 0xD3:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,E";
                break;

            case 0xD4:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,H";
                break;

            case 0xD5:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,L";
                break;

            case 0xD6:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,(HL)";
                break;

            case 0xD7:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00000100;
                instr->operands[1] = &inst_value;
                instr->description = "SET 2,A";
                break;

            case 0xD8:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,B";
                break;

            case 0xD9:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,C";
                break;

            case 0xDA:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,D";
                break;

            case 0xDB:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,E";
                break;

            case 0xDC:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,H";
                break;

            case 0xDD:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,L";
                break;

            case 0xDE:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,(HL)";
                break;

            case 0xDF:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00001000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 3,A";
                break;

            case 0xE0:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,B";
                break;

            case 0xE1:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,C";
                break;

            case 0xE2:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,D";
                break;

            case 0xE3:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,E";
                break;

            case 0xE4:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,H";
                break;

            case 0xE5:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,L";
                break;

            case 0xE6:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,(HL)";
                break;

            case 0xE7:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00010000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 4,A";
                break;

            case 0xE8:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,B";
                break;

            case 0xE9:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,C";
                break;

            case 0xEA:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,D";
                break;

            case 0xEB:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,E";
                break;

            case 0xEC:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,H";
                break;

            case 0xED:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,L";
                break;

            case 0xEE:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,(HL)";
                break;

            case 0xEF:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b00100000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 5,A";
                break;

            case 0xF0:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,B";
                break;

            case 0xF1:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,C";
                break;

            case 0xF2:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,D";
                break;

            case 0xF3:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,E";
                break;

            case 0xF4:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,H";
                break;

            case 0xF5:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,L";
                break;

            case 0xF6:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,(HL)";
                break;

            case 0xF7:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b01000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 6,A";
                break;

            case 0xF8:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.B;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,B";
                break;

            case 0xF9:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.C;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,C";
                break;

            case 0xFA:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.D;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,D";
                break;

            case 0xFB:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.E;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,E";
                break;

            case 0xFC:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.H;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,H";
                break;

            case 0xFD:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.L;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,L";
                break;

            case 0xFE:
                instr->handler = &instr_SET_dd;
                instr->clock_cycles = 16;
                instr->operands[0] = (word *)&cpu_regs.HL;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,(HL)";
                break;

            case 0xFF:
                instr->handler = &instr_SET_r;
                instr->clock_cycles = 8;
                instr->operands[0] = (word *)&cpu_regs.A;
                inst_value.w = 0b10000000;
                instr->operands[1] = &inst_value;
                instr->description = "SET 7,A";
                break;

            /* EOF UNSTABLE */

            default:
                instr->handler = &instr_illegal;
                instr->description = "ILLEGAL INSTRUCTION";
                break;
        }
    }

    return instr;
}
