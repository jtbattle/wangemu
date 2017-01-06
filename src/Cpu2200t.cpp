// emulate the Wang 2200T micromachine

#include "Ui.h"
#include "Cpu2200.h"
#include "IoCardKeyboard.h"
#include "Scheduler.h"
#include "System2200.h"
#include "ucode_2200B.h"
#include "ucode_2200T.h"

// if this is defined as 0, a few variables get initialized
// unnecessarily, which may very slightly slow down the emulation,
// but which will result in the compiler complaining about potentially
// uninitialized variables.
#define NO_LINT_WARNINGS 1

#ifdef _MSC_VER
#pragma warning( disable : 4127 )  // constant expressions are OK
#endif

// ------------------------------------------------------------------------
//  assorted notes
// ------------------------------------------------------------------------
//
// status 1 bits: input device inhibit, special function key sense,
//                input bus strobe (p4-16)
enum { ST1_MASK_CARRY=1, ST1_MASK_CPB=2, ST1_MASK_SF=4, ST1_MASK_ROM=8 };
//
//    0: ALU carry bit
//
//    1: "CPB" (cpu busy).  this bit is set to 1 while the cpu is thinking
//       and doesn't wish to be disturbed.  if the CPU is ready to accept
//       input from the currently selected I/O device, CPB goes inactive.
//       The microcode is still running, mind you.  When the selected
//       device has something to send, it drives IBS along with the 9b
//       datum on the I/O bus.  This is latched in the K register and the
//       CPB bit is set back high by hardware.  when the microcode sees
//       this CPB go back to 1, it knows it has the requested data.
//
//       the 2200 service manual says (p 4-16, sec. 4.1) that the CPU
//       drives CBS to request input from the currently selected I/O
//       device, but this certainly isn't the case for all devices.
//       the 6374 (paper tape reader) *does* behave this way.  the cpu
//       selects the device and apparently keeps sending CBS pulses.
//       the CBS is gated by the card's ready status, and when CBS
//       arrives while read, IBS is driven, clocking the data to the K
//       register and clearing the CPB bit.  it seems dangerous to me
//       since the card's ready status is probably asynchronous to the
//       CBS strobe, which might result in a runt IBS pulse.
//
//       the vp manual describes the CPB bit this way:
//          CPB (set by hw or ucode)
//          0=allow input from selected device (ie, CPU is ready)
//          1=inhibit all input from devices (ie, CPU is busy)
//
//    2: when IBS goes active, if IB9 is high, this bit gets set.
//       microcode can also write it directly.  it is used to flag
//       the special function keys when polling the keyboard.
//
//    3: driven by the ucode.  0=memory accesses are to RAM
//                             1=memory accesses are to ROM

// status 2 bits: set by ucode to indicate the phase and processing mode
// it is just a r/w register with no special meaning to hardware.

// status 3 bits: senses halt/step, senses I/O device busy, and other I/O
// operations
enum { ST3_MASK_DEVRDY=1, ST3_MASK_INBIT0=2, ST3_MASK_HALT=4, ST3_MASK_HORZ=8 };
//
//    0: this is a ready bit (1=ready, 0=busy).  busy means that the
//       currently selected I/O device either isn't ready to accept
//       another command or that it isn't ready to supply expected data.
//
//    1: carl: Hardware sets this flag using an unused (unknown function)
//       I/O bus line. The output of this flag (a 7476 flip flop), is
//       unconnected, so it's just a bit bucket.  However, reading this
//       bit reads a copy of the I/O data input bus bit IB5 (0x10).
//       [ source: 6311 schematic, "I/O Control" ]
//           accessed by ucode in two places: 4BFD and 0B6E
//           0B6E is a subroutine called from seven other places
//
//    2: carl: Set to 1 when Halt/Step is pressed.
//
//    3: 0=vertical RAM addressing; 1=horizontal RAM addressing

// status 4 bits: set by ucode during I/O operations
// it is just a r/w register with no special meaning to hardware.

// ------------------------------------------------------------------------
//  pre-decode instructions for faster interpretation
// ------------------------------------------------------------------------

enum {
    // misc
    OP_ILLEGAL,         // illegal instruction

    // register instructions
    OP_OR,
    OP_XOR,
    OP_AND,
    OP_DSC,
    OP_A,
    OP_AC,
    OP_DA,
    OP_DAC,

    // register immediate instructions
    OP_ORI,
    OP_XORI,
    OP_ANDI,
    OP_AI,
    OP_ACI,
    OP_DAI,
    OP_DACI,

    // branch instructions
    OP_BER_INC,
    OP_BER,
    OP_BNR_INC,
    OP_BNR,
    OP_SB,
    OP_B,
    OP_BT,
    OP_BF,
    OP_BEQ,
    OP_BNE,

    // mini instructions
    OP_CIO,
    OP_SR,
    OP_TPI,
    OP_TIP,
    OP_TMP,
    OP_TP,
    OP_TA,
    OP_XP
};

enum {
    FETCH_B   = 0x80000000,     // load b_op  according to uop[24:20]
    FETCH_A   = 0x40000000,     // load a_op  according to uop[7:4]
    FETCH_AB  = 0xC0000000      // fetch a_op and b_op
};

// swap the two nibbles of a byte
#define nibble_swap(v) ((uint8)( (((v)&0xF)<<4) | (((v)>>4)&0xF) ))

// decode the M field and if a memory op is going to occur,
// decode the A field and check it for legality
#define CRACK_M_FIELD(oper,param)                               \
        do {                                                    \
            if (m_field > 1) {                                  \
                illegal = (a_field >= 9) && (a_field != 12);    \
                m_ucode[addr].ucode |= FETCH_A;                 \
            }                                                   \
            m_ucode[addr].op  = (uint8)(oper);                  \
            m_ucode[addr].p16 = (uint16)(param);                \
        } while (false)

// copy bits [14:10] to [24:20]
// [13:10] contain the B field specifier, and [14] is the X bit
#define REPACK_B_FIELD(dst, uop)                \
    do {                                        \
        (dst) |= ((uop) << 10) & 0x01F00000;    \
    } while (false)

// as above, but there is no X bit and B comes from [15:12] instead of [13:10]
#define REPACK_B2_FIELD(dst, uop)               \
    do {                                        \
        (dst) |= ((uop) << 8) & 0x00F00000;     \
    } while (false)

// extract the branch target for an unconditional branch
#define FULL_TARGET(u) \
    ((uint16)(((u) & 0xF00F) | (((u)<<4) & 0x0F00) | (((u)>>4) & 0x00F0)))

// branch target within 8b page
#define BRANCH_TARGET(ic,uop) \
        (uint16)(((ic) & 0xFF00) | (((uop) >> 4) & 0xF0) | ((uop) & 0x0F))

// store the microcode word to the given microstore address.
// the microinstruction is checked for validity and the instruction is
// predecoded to make subsequent interpretation faster.
void
Cpu2200t::write_ucode(int addr, uint32 uop)
{
    static const int8 pc_adjust_tbl[16] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         0, -1, +1, -1,  0, -1, +1, +1 };
    #define PC_ADJUST(a_field) (pc_adjust_tbl[(a_field)])

    const int opcode1 = (uop >> 15) & 0x1F;     // primary op
    const int opcode2 = (uop >> 10) & 0x1F;     // mini-op
    const int m_field = (uop >>  8) & 0x3;
    const int a_field = (uop >>  4) & 0xF;
    const int c_field = (uop >>  0) & 0xF;

    int illegal = 0;    // innocent until proven guilty
    int8 pcinc;

    assert(addr >=0 && addr < MAX_UCODE);

    uop &= 0x000FFFFF;  // only 20b are meaningful

    m_ucode[addr].ucode = uop;
    m_ucode[addr].p8    = 0;    // default
    m_ucode[addr].p16   = 0;    // default

    switch (opcode1) {

    // register instructions:

    case 0x00:  // OR
    case 0x01:  // XOR
    case 0x02:  // AND
    case 0x03:  // decimal subtract w/ carry
    case 0x04:  // binary add
    case 0x05:  // binary add w/ carry
    case 0x06:  // decimal add
    case 0x07:  // decimal add w/ carry
        illegal = (c_field == 13) || (c_field == 14);
        m_ucode[addr].ucode |= FETCH_AB;
        REPACK_B_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op = (uint8)(OP_OR + (opcode1 - 0x00));
        break;

    case 0x08:  // or immediate
    case 0x09:  // xor immediate
    case 0x0A:  // and immediate
    case 0x0C:  // binary add immediate
    case 0x0D:  // binary add immediate w/ carry
    case 0x0E:  // decimal add immediate
    case 0x0F:  // decimal add immediate w/ carry
        m_ucode[addr].ucode |= FETCH_B;
        REPACK_B_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op = (uint8)( (opcode1 < 0x0B) ?
                                        (OP_ORI + (opcode1 - 0x08)) :
                                        (OP_AI  + (opcode1 - 0x0C)) );
        break;

    // mini instruction decode
    case 0x0B:
        switch (opcode2) {

        case 0x00: // control I/O (CIO)
            // the ucode instruction set table (4-1) on page 4-18 claims
            // M is meaningful, but only read would make sense to me.
            illegal = ((uop & 0x00200) != 0);   // not a mem write
            m_ucode[addr].op = OP_CIO;
            break;

        case 0x01: // subroutine return (SR)
            CRACK_M_FIELD(OP_SR, 0);
            break;

        case 0x05: // transfer PC to IC (TPI)
            CRACK_M_FIELD(OP_TPI, 0);
            break;
        case 0x06: // transfer IC to PC (TIP)
            CRACK_M_FIELD(OP_TIP, 0);
            break;

        case 0x07: // transfer memory size to PC (TMP)
            // note: the resulting value is in nibbles
            // p 2-11 contains the switch settings for various RAM sizes:
            //   4K = 000,  8K=001, 12K=010, 16K=011
            //  20K = 100, 24K=101, 28K=110, 32K=111
            // that is, it should be (#4K blocks - 1)
            CRACK_M_FIELD(OP_TMP, 0);
            break;

        case 0x02: // transfer PC to Aux (TP)
            CRACK_M_FIELD(OP_TP, 0);
            break;
        case 0x08: // transfer PC to Aux,+1 (TP+1)
            CRACK_M_FIELD(OP_TP, +1);
            break;
        case 0x09: // transfer PC to Aux,-1 (TP-1)
            CRACK_M_FIELD(OP_TP, -1);
            break;
        case 0x0A: // transfer PC to Aux,+2 (TP+2)
            CRACK_M_FIELD(OP_TP, +2);
            break;
        case 0x0B: // transfer PC to Aux,-2 (TP-2)
            CRACK_M_FIELD(OP_TP, -2);
            break;

        case 0x03: // transfer Aux to PC (TA)
            CRACK_M_FIELD(OP_TA, 0);
            break;

        case 0x04: // exchange PC and Aux (XP)
            CRACK_M_FIELD(OP_XP, 0);
            break;
        case 0x0C: // exchange PC and Aux,+1 (XP+1)
            CRACK_M_FIELD(OP_XP, +1);
            break;
        case 0x0D: // exchange PC and Aux,-1 (XP-1)
            CRACK_M_FIELD(OP_XP, -1);
            break;
        case 0x0E: // exchange PC and Aux,+2 (XP+2)
            CRACK_M_FIELD(OP_XP, +2);
            break;
        case 0x0F: // exchange PC and Aux,-2 (XP-2)
            CRACK_M_FIELD(OP_XP, -2);
            break;

        default:
            illegal = 1;
            break;
        }
        break;

    // branch instructions:

    case 0x10: case 0x11:       // BER: branch if R[AAAA] == R[BBBB]
        pcinc = PC_ADJUST(a_field);
        m_ucode[addr].ucode |= FETCH_AB;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)((pcinc == 0) ? OP_BER : OP_BER_INC);
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x12: case 0x13:       // BNR: branch if R[AAAA] != R[BBBB]
        pcinc = PC_ADJUST(a_field);
        m_ucode[addr].ucode |= FETCH_AB;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)((pcinc == 0) ? OP_BNR : OP_BNR_INC);
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x1C: case 0x1D:       // BEQ: branch if == to mask
        m_ucode[addr].ucode |= FETCH_B;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)OP_BEQ;
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x1E: case 0x1F:       // BNE: branch if != to mask
        m_ucode[addr].ucode |= FETCH_B;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)OP_BNE;
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x18: case 0x19:       // BT: branch if true bittest
        m_ucode[addr].ucode |= FETCH_B;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)OP_BT;
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x1A: case 0x1B:       // BF: branch if false bittest
        m_ucode[addr].ucode |= FETCH_B;
        REPACK_B2_FIELD(m_ucode[addr].ucode, uop);
        m_ucode[addr].op  = (uint8)OP_BF;
        m_ucode[addr].p16 = BRANCH_TARGET(addr,uop);
        break;

    case 0x14: case 0x15:       // subroutine branch (SB)
        m_ucode[addr].op  = (uint8)OP_SB;
        m_ucode[addr].p16 = FULL_TARGET(uop);
        break;

    case 0x16: case 0x17:       // unconditional branch (B)
        m_ucode[addr].op  = (uint8)OP_B;
        m_ucode[addr].p16 = FULL_TARGET(uop);
        break;

    default:
        illegal = 1;
        break;
    }

    if (illegal) {
        m_ucode[addr].ucode &= 0x000FFFFF;      // clear flags we might have set
        m_ucode[addr].op     = OP_ILLEGAL;
        m_ucode[addr].p16    = 0;
    }
}


// ------------------------------------------------------------------------
//  misc utilities
// ------------------------------------------------------------------------

// return 0 or 1 based on the st1 carry flag
#define CARRY_BIT ((m_cpu.st1&ST1_MASK_CARRY)?1:0)

#if 0
// dump the most important contents of the uP state
void
Cpu2200t::dump_state(int fulldump)
{
    if (fulldump) {
        dbglog("---------------------------------------------\n");
    }

    dbglog("ic=%04X, pc=%04X, c=%02X, k=%02X, ab=%02X, ab_sel=%02X, cy=%X\n",
        m_cpu.ic, m_cpu.pc, m_cpu.c, m_cpu.k, m_cpu.ab, m_cpu.ab_sel, CARRY_BIT);
    if (!fulldump) {
        return;
    }

    dbglog("st1=%X, st2=%X, st3=%X, st4=%X\n",
            m_cpu.st1, m_cpu.st2, m_cpu.st3, m_cpu.st4);
    dbglog("r0=%X, r1=%X, r2=%X, r3=%X, r4=%X, r5=%X, r6=%X, r7=%X\n",
            m_cpu.reg[0], m_cpu.reg[1], m_cpu.reg[2], m_cpu.reg[3],
            m_cpu.reg[4], m_cpu.reg[5], m_cpu.reg[6], m_cpu.reg[7]);
    dbglog("AUX0=%04X, AUX1=%04X, AUX2=%04X, AUX3=%04X\n",
            m_cpu.aux[0], m_cpu.aux[1], m_cpu.aux[2], m_cpu.aux[3]);
    dbglog("AUX4=%04X, AUX5=%04X, AUX6=%04X, AUX7=%04X\n",
            m_cpu.aux[4], m_cpu.aux[5], m_cpu.aux[6], m_cpu.aux[7]);
    dbglog("AUX8=%04X, AUX9=%04X, AUXA=%04X, AUXB=%04X\n",
            m_cpu.aux[8], m_cpu.aux[9], m_cpu.aux[10], m_cpu.aux[11]);
    dbglog("AUXC=%04X, AUXD=%04X, AUXE=%04X, AUXF=%04X\n",
            m_cpu.aux[12], m_cpu.aux[13], m_cpu.aux[14], m_cpu.aux[15]);
    dbglog("stack depth=%d\n", ICSTACK_TOP-m_cpu.icsp);
    if (m_cpu.icsp < ICSTACK_TOP) {
        int num = ICSTACK_TOP - m_cpu.icsp;
        int todo = (num > 6) ? 6 : num;
        int i;
        dbglog("    recent: ");
        for(i=ICSTACK_TOP; i>ICSTACK_TOP-todo; i--) {
            dbglog("%04X ", m_cpu.icstack[i]);
        }
        dbglog("\n");
    }
    dbglog("---------------------------------------------\n");
}
#endif


#if 0
// dump a floating point number (16 nibbles)
void
Cpu2200t::dump_16n(int addr)
{
    char buff[30];
    int i;
    addr &= ~1;
    sprintf(buff, "RAM[%04X] = ", addr);
    for(i=0; i<8; i++) {
        sprintf(&buff[12+i],"%X", nibble_swap(m_RAM[addr+i]));
    }
    dbglog("%s\n", buff);
}
#endif

// ------------------------------------------------------------------------
//  private functions
// ------------------------------------------------------------------------

// set the st1 carry flag in accordance with bit 4 of v
#define SET_CARRY(v)                                                    \
        m_cpu.st1 = (uint4)( (m_cpu.st1 & ~ST1_MASK_CARRY) |            \
                           (((v) & 0x10) ? ST1_MASK_CARRY : 0) )


// read from the specified address.
// there are two memory spaces: ROM and RAM
// for RAM, there are two modes: "horizontal" and "vertical".
uint8
Cpu2200t::mem_read(uint16 addr) const
{
    int rv;

    if (m_cpu.st1 & ST1_MASK_ROM) {
        // ROM address space
        int ROMaddr = (addr >> 1) & 0x7FF;
        if (addr & 1) {
            rv = m_kROM[ROMaddr];
        } else {
            // yes, we swap in the even case, not the odd case
            rv = nibble_swap(m_kROM[ROMaddr]);
        }
#if 0
        if (rv < 32) {
            dbglog(">>kROM[%03X] = %02X, at ic=%04X\n", ROMaddr, (uint16)rv, m_cpu.ic);
        } else if (rv < 128) {
            dbglog(">>kROM[%03X] = %02X ('%c'), at ic=%04X\n", ROMaddr, (uint16)rv, (char)rv, m_cpu.ic);
        } else if (rv < 128+32) {
            dbglog(">>kROM[%03X] = %02X, at ic=%04X\n", ROMaddr, (uint16)rv, m_cpu.ic);
        } else {
            dbglog(">>kROM[%03X] = %02X (0x80+'%c'), at ic=%04X\n", ROMaddr, (uint16)rv, (char)(0x7F & rv), m_cpu.ic);
        }
#endif
    } else {

        // RAM address space

        // note that addr[15:0] is a nibble address
        //
        // CL always loads the nibble addressed by PC,
        // independent of horizontal/vertical mode.
        //
        // in horizontal mode, CH loads the nibble
        // addressed by complementing addr bit 0.
        // in vertical mode, CH loads the nibble
        // addressed by complementing addr bit 4.

        int RAMaddr = (addr >> 1);
        assert(RAMaddr < (m_memsize_KB<<10));

        if (m_cpu.st3 & ST3_MASK_HORZ) {
            // horizontal addressing
            rv = (addr & 1) ? nibble_swap(m_RAM[RAMaddr])
                            : m_RAM[RAMaddr];
        } else {
            // vertical addressing
            if (addr & 1) {
                // read the upper nibble from two bytes 8 bytes apart
                rv = ((m_RAM[RAMaddr^0x0] & 0xF0) >> 4)
                   | ((m_RAM[RAMaddr^0x8] & 0xF0) >> 0);
            } else {
                // read the lower nibble from two bytes 8 bytes apart
                rv = ((m_RAM[RAMaddr^0x0] & 0x0F) << 0)
                   | ((m_RAM[RAMaddr^0x8] & 0x0F) << 4);
            }
        }
    }

    return (uint8)rv;
}


// write to the specified address.
// there are two memory spaces: ROM and RAM
// for RAM, there are two modes: "horizontal" and "vertical".
// write2=0 corresponds to a WRITE1 opcode.
// write2=1 corresponds to a WRITE2 opcode.
//
// WRITE1 ignored vert/horiz mode and just always writes to the
// nibble literally addressed by the PC.
//
// WRITE2 flips A0 in horizontal mode.
// WRITE2 flips A4 in vertical mode.
void
Cpu2200t::mem_write(uint16 addr, uint4 wr_value, int write2)
{
    if (m_cpu.st1 & ST1_MASK_ROM) {
        // ROM address space
        assert(false);    // ucode shouldn't ever write to ROM
    } else {
        // RAM address space
        int RAMaddr;

        if (write2) {
            addr ^= (m_cpu.st3 & ST3_MASK_HORZ) ? 0x0001  // horizontal mode
                                                : 0x0010; // vertical mode
        }

        RAMaddr = (addr >> 1);
        assert(RAMaddr < (m_memsize_KB<<10));

        if (addr & 1) {
            m_RAM[RAMaddr] = (uint8)((m_RAM[RAMaddr]&0x0F) | (wr_value<<4));
        } else {
            m_RAM[RAMaddr] = (uint8)((m_RAM[RAMaddr]&0xF0) | (wr_value<<0));
        }
        // dbglog("WR%d %04X, RAM[0x%04X] = 0x%02X\n", 1+write2, addr, RAMaddr, m_RAM[RAMaddr]);
    }
}


// reading ST3 is a subroutine because it must return state that wasn't
// what was written.  (ST3&2) reflects the instantaneous state of IB5
// (counting from IB1).  What is it used for?
//
// ANSWER: the 7011 80x24 CRT controller drives IB5 active whenever
//         it is selected, while the 64x16 CRT controller doesn't
//         drive it at all, letting it get pulled inactive.
//         the initialization routine at 0x4BFD selects device 005
//         and tests if IB5 is set or not.  If IB5 is set, then
//         the device table is modified to make the line 80 chars
//         wide, otherwise it leaves it at the default 64 chars.
//         I don't know if there are any other uses like this.
//         There is a routine at 0x0B6E that also tests this bit,
//         but I haven't seen it get triggered.
//
// FIXME: this violates the normal mechanism of I/O.  to really
//        support this, there needs to be a different mechanism
//        in the emulator, or a very specific hack must be used.
uint4
Cpu2200t::read_st3() const
{
    int ib5 = m_sys.cpu_poll_IB5();
    return (uint4)(  (m_cpu.st3 & 0x8)  // 1=horizontal RAM addressing
                   | (m_cpu.st3 & 0x4)  // 1=halt/step is pressed
                   | (ib5 << 1)         // wr: nop, rd: (I/O data bus & 0x10)
                   | (m_cpu.st3 & 0x1)  // 1=I/O device is ready
                );
}


// setting ST1.1 can have more complicated side effects
void
Cpu2200t::set_st1(uint4 value)
{
    int cpb_changed = ((m_cpu.st1 ^ value) & ST1_MASK_CPB);
    m_cpu.st1 = value;

    if (cpb_changed) {
        m_sys.cpu_CPB( !!(m_cpu.st1 & ST1_MASK_CPB) );
    }
}


// store the 4b value to the place selected by the C field.
// return a flag if the op is illegal.
int
Cpu2200t::store_C_operand(uint32 uop, uint4 value)
{
    const int xbit  = ((uop >> 14) & 0x1);
    const int field = ((uop >>  0) & 0xF);

    if (field < 8) {
        m_cpu.reg[field] = value;
        return 0;       // legal
    }

    if (xbit) {
        switch (field) {
#if 0
    // FIXME: this would seem correct, but it leads to problems.  if halt/step
    //        is pressed, it appears that the thing remains "pressed",
    //        and by using the "wrong" code below, the bits is cleared.
    //
    //        look at the schematics: how does the HW behave?
            case  8: m_cpu.st3 = (uint4)((m_cpu.st3 & 0x6) | (value & 0x9)); break;
#else
            case  8: m_cpu.st3 = value; break;
#endif
            case  9: m_cpu.st4 = value; break;
            case 10: m_cpu.pc  = (uint16)((m_cpu.pc & 0xFF0F) | (value <<  4)); break; // PC2
            case 11: m_cpu.pc  = (uint16)((m_cpu.pc & 0xF0FF) | (value <<  8)); break; // PC3
            case 12: m_cpu.pc  = (uint16)((m_cpu.pc & 0x0FFF) | (value << 12)); break; // PC4
            case 13: return 1;  // illegal
            case 14: return 1;  // illegal
            case 15: break;     // dummy destination
        }
    } else {
        switch (field) {
            case  8: m_cpu.k   = (uint8)((m_cpu.k & 0x0F) | (value << 4)); break;   // KH
            case  9: m_cpu.k   = (uint8)((m_cpu.k & 0xF0) | (value << 0)); break;   // KL
            case 10: set_st1(value); break;
            case 11: m_cpu.st2 = value; break;
            case 12: m_cpu.pc  = (uint16)((m_cpu.pc & 0xFFF0) | (value << 0)); break; // PC1
            case 13: return 1;  // illegal
            case 14: return 1;  // illegal
            case 15: break;     // dummy destination
        }
    }

    return 0;   // legal
}


// decode the M field.
// if it is a read, the data goes into the C data read register.
// if it is a write, the data written comes from wr_value.
#define decode_M_field(uop, wr_value)                   \
    do {                                                \
        switch (((uop) >> 8) & 0x3) {                   \
             case 0:    /* no memory op */              \
                break;                                  \
             case 1:    /* memory read */               \
                m_cpu.c = mem_read(m_cpu.pc);           \
                break;                                  \
             case 2:    /* write MEML */                \
                mem_write(m_cpu.pc, (wr_value), 0);     \
                break;                                  \
             case 3:    /* write MEMH */                \
                mem_write(m_cpu.pc, (wr_value), 1);     \
                break;                                  \
        }                                               \
    } while (false)

uint8
Cpu2200t::decimal_add(uint4 a_op, uint4 b_op, int ci) const
{
    int sum;
    int co;

    #ifdef _DEBUG
    // these are known to fire (eg, running diags), yet something
    // detects the problem and doesn't use the result.
    assert(a_op < 10);
    assert(b_op < 10);   // "tomlake.w22" triggers this one, as does diagnostics disk
    #endif

    sum = a_op + b_op + ci; // ranges from binary 0 to 19
    co = (sum > 9);

    if (co) {
        sum -= 10;
    }

    return (uint8)((co << 4) + sum);
}


// see p 4-79 of service manual for one example of operation
// (don't be confused by inverter from ALU CO going to bin->bcd
//  corrector -- this is because if you use the '181 with
//  active high data/controls, the carry in & out are then
//  active low).
// cy=1 effectively means no borrow; cy=0 means borrow
uint8
Cpu2200t::decimal_sub(uint4 a_op, uint4 b_op, int ci) const
{
    int ninecomp;

    #ifdef _DEBUG
    // these are known to fire (eg, running diags), yet something
    // detects the problem and doesn't use the result.
    assert(a_op < 10);
    assert(b_op < 10);
    #endif

    ninecomp = 9-b_op;  // form 9's complement

    return decimal_add(a_op, (uint4)ninecomp, ci);
}


// add offset to LS nibble of pc
#define NIBBLE_INC(pc,inc)                                      \
        do {                                                    \
            (pc) = (uint16)( ((pc)                & 0xFFF0) |   \
                            (((pc) + (int8)(inc)) & 0x000F) );  \
        } while (false)

#define IMM4(uop) ((uint4)((uop >> 4) & 0xF))

// perform one instruction and return.
// returns EXEC_ERR if we hit an illegal op, otherwise return # of ticks
#define EXEC_ERR (1<<30)
#define EXEC_OK  (0)
int
Cpu2200t::exec_one_op()
{
    const ucode_t *puop = &m_ucode[m_cpu.ic];
    const uint32 uop  = puop->ucode;
    int r_field;
    uint16 tmp_pc;
    uint4 a_op, b_op;
    uint8 rslt;
    uint16 newic;
    int pcinc;

    // ==================== NEW CODE ============================

    if (uop & FETCH_A) {
        const int field = ((uop >> 4) & 0xF);
        switch (field) {

        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
            pcinc = 0;
            a_op = m_cpu.reg[field];
            break;

        case  8: pcinc =  0; a_op = (uint4)((m_cpu.c >> 4) & 0xF);      break;  // CH
        case  9: pcinc = -1; a_op = (uint4)((m_cpu.c >> 4) & 0xF);      break;  // CH
        case 10: pcinc = +1; a_op = (uint4)((m_cpu.c >> 4) & 0xF);      break;  // CH
        case 11: pcinc = -1; a_op = (uint4)(0x0);                       break;  // dummy
        case 12: pcinc =  0; a_op = (uint4)((m_cpu.c >> 0) & 0xF);      break;  // CL
        case 13: pcinc = -1; a_op = (uint4)((m_cpu.c >> 0) & 0xF);      break;  // CL
        case 14: pcinc = +1; a_op = (uint4)((m_cpu.c >> 0) & 0xF);      break;  // CL
        case 15: pcinc = +1; a_op = (uint4)(0x0);                       break;  // dummy

        default:
            assert(false);
            pcinc = 0;
            a_op  = (uint4)0x0;
            break;
        }
    }
#if NO_LINT_WARNINGS
    else {
        pcinc = 0;
        a_op  = 0;
    }
#endif

    if (uop & FETCH_B) {
        const int field = ((uop >> 20) & 0x1F);
        switch (field) {

        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
            b_op = m_cpu.reg[field];
            break;

        case  8: b_op = (uint4)((m_cpu.k >> 4) & 0xF);  break;  // KH
        case  9: b_op = (uint4)((m_cpu.k >> 0) & 0xF);  break;  // KL
        case 10: b_op = (uint4)(m_cpu.st1);             break;  // ST1
        case 11: b_op = (uint4)(m_cpu.st2);             break;  // ST2
        case 12: b_op = (uint4)((m_cpu.pc >> 0) & 0xF); break;  // PC1
        case 13: b_op = (uint4)((m_cpu.c  >> 4) & 0xF); break;  // CH
        case 14: b_op = (uint4)((m_cpu.c  >> 0) & 0xF); break;  // CL
        case 15: b_op = (uint4)(0x0);                   break;  // dummy

        case 16: case 17: case 18: case 19:
        case 20: case 21: case 22: case 23:
            b_op = m_cpu.reg[field-16];
            break;

        case 24: b_op = read_st3();                      break;  // ST3
        case 25: b_op = m_cpu.st4;                       break;  // ST4
        case 26: b_op = (uint4)((m_cpu.pc >>  4) & 0xF); break;  // PC2
        case 27: b_op = (uint4)((m_cpu.pc >>  8) & 0xF); break;  // PC3
        case 28: b_op = (uint4)((m_cpu.pc >> 12) & 0xF); break;  // PC4
        case 29: b_op = (uint4)((m_cpu.c  >>  4) & 0xF); break;  // CH
        case 30: b_op = (uint4)((m_cpu.c  >>  0) & 0xF); break;  // CL
        case 31: b_op = (uint4)(0x0);                    break;  // dummy

        default:
            assert(false);
            b_op = (uint4)0x0;
            break;
        }
    } // fetch b_op
#if NO_LINT_WARNINGS
    else {
        b_op = 0;
    }
#endif

    // primary instruction decode
    switch (puop->op) {

    case OP_ILLEGAL:
        {
            char buff[200];
            (void)dasm_one(buff, m_cpu.ic, m_ucode[m_cpu.ic].ucode);
            UI_Error("%s\nIllegal op at ic=%04X", buff, m_cpu.ic);
        }
        return EXEC_ERR;

    // register instructions:

    case OP_OR:
        rslt = (uint8)(a_op | b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        // TODO: what happens if store_C_operand() twiddles pc
        //       AND the pc increment is non-zero?
        // switching the order of store_C_operand() and NIBBLE_INC() didn't
        // cause any problems when running the 2200T diagnostic suite.  it
        // probably never happens, but it would be nice to have an assertion.
        m_cpu.ic++;
        break;

    case OP_XOR:
        rslt = (uint8)(a_op ^ b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_AND:
        rslt = (uint8)(a_op & b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_DSC:        // decimal subtract w/ carry
        rslt  = decimal_sub(a_op, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_A:          // binary add
        rslt  = (uint8)(a_op + b_op);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_AC:         // binary add w/ carry
        rslt  = (uint8)(a_op + b_op + CARRY_BIT);
        SET_CARRY(rslt);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_DA:         // decimal add
        rslt  = decimal_add(a_op, b_op, 0);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_DAC:        // decimal add w/ carry
        rslt  = decimal_add(a_op, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        NIBBLE_INC(m_cpu.pc, pcinc);
        m_cpu.ic++;
        break;

    case OP_ORI:        // or immediate
        a_op  = IMM4(uop);
        rslt  = (uint8)(a_op | b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_XORI:       // xor immediate
        a_op  = IMM4(uop);
        rslt  = (uint8)(a_op ^ b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_ANDI:       // and immediate
        a_op  = IMM4(uop);
        rslt  = (uint8)(a_op & b_op);
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_AI:         // binary add immediate
        a_op  = IMM4(uop);
        rslt  = (uint8)(a_op + b_op);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_ACI:        // binary add immediate w/ carry
        a_op  = IMM4(uop);
        rslt  = (uint8)(a_op + b_op + CARRY_BIT);
        SET_CARRY(rslt);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_DAI:        // decimal add immediate
        a_op  = IMM4(uop);
        rslt  = decimal_add(a_op, b_op, 0);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    case OP_DACI:       // decimal add immediate w/ carry
        a_op  = IMM4(uop);
        rslt  = decimal_add(a_op, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        rslt &= 0xF;
        decode_M_field(uop, (uint4)rslt);
        (void)store_C_operand(uop, (uint4)rslt);
        m_cpu.ic++;
        break;

    // branch instructions:

    case OP_BER_INC:    // branch if R[AAAA] == R[BBBB]
        NIBBLE_INC(m_cpu.pc, pcinc);
    case OP_BER:        // branch if R[AAAA] == R[BBBB]
        if (a_op == b_op) m_cpu.ic = (uint16)(puop->p16);
                     else m_cpu.ic++;
        break;

    case OP_BNR_INC:    // branch if R[AAAA] != R[BBBB]
        NIBBLE_INC(m_cpu.pc, pcinc);
    case OP_BNR:        // branch if R[AAAA] != R[BBBB]
        if (a_op != b_op) m_cpu.ic = (uint16)(puop->p16);
                     else m_cpu.ic++;
        break;

    case OP_BEQ:        // branch if == to mask (BEQ)
        a_op = IMM4(uop);
        if (a_op == b_op) m_cpu.ic = (uint16)(puop->p16);
                     else m_cpu.ic++;
        break;

    case OP_BNE:        // branch if != to mask
        a_op = IMM4(uop);
        if (a_op != b_op) m_cpu.ic = (uint16)(puop->p16);
                     else m_cpu.ic++;
        break;

    case OP_BT: // branch if true bittest
        // for each 1 bit in the imm mask,
        // BT means the corresponding op_b bit must be 1
        // BF means the corresponding op_b bit must be 0
        a_op = IMM4(uop);
        if ((a_op & b_op) == a_op) m_cpu.ic = (uint16)(puop->p16);
                              else m_cpu.ic++;
        break;

    case OP_BF: // branch if false bittest
        // for each 1 bit in the imm mask,
        // BT means the corresponding op_b bit must be 1
        // BF means the corresponding op_b bit must be 0
        a_op = IMM4(uop);
        b_op ^= 0xF;
        if ((a_op & b_op) == a_op) m_cpu.ic = (uint16)(puop->p16);
                              else m_cpu.ic++;
        break;

    case OP_SB: // subroutine branch
        if (m_cpu.prev_sr) {
            // the calling address is what gets pushed on a SR.
            // on a return, the micromachine actually executes
            // the calling SB again, but the SR set a flop that
            // causes the cycle to effectively be a noop.
            m_cpu.prev_sr = false;
            m_cpu.ic++;
        } else {
            // the ic stack pointer is post decremented.
            // it is preincremented on subroutine return.
            newic = FULL_TARGET(uop);
            m_cpu.icstack[m_cpu.icsp] = m_cpu.ic;
            m_cpu.icsp = (m_cpu.icsp - 1) & ICSTACK_MASK;   // wraps
            m_cpu.ic = newic;
        }
        break;

    case OP_B:  // unconditional branch
        newic = FULL_TARGET(uop);
        m_cpu.ic = newic;
        break;

    // miniop instructions:

    case OP_CIO: // control I/O

        // emit address and address strobe if requested
        if (uop & 0x80) {
            m_cpu.ab = m_cpu.k;
        }

        switch (uop & 0x7F) {

            case 0x00: // noop
                break;

            case 0x10: // generate -CBS
                if (m_dbg) {
                    dbglog("-CBS when AB=%02X\n", m_cpu.ab_sel);
                    //UI_Info("CPU:CBS when AB=%02X\n, AB_SEL=%02X", m_cpu.ab, m_cpu.ab_sel);
                }
                m_sys.cpu_CBS();  // control bus strobe
                break;

            case 0x20: // generate -OBS
                if (m_dbg) {
                    char buf[8];
                    if (m_cpu.k >= 32 && m_cpu.k < 128) {
                        sprintf(buf, " ('%c')", m_cpu.k);
                    }
                    dbglog("-OBS when AB=%02X, K=%02X%s\n", m_cpu.ab_sel, m_cpu.k, buf);
                }
                //UI_Info("CPU:OBS when AB=%02X, AB_SEL=%02X, K=%02X\n", m_cpu.ab, m_cpu.ab_sel, m_cpu.k);
                m_sys.cpu_OBS(m_cpu.k);  // output data bus strobe
                break;

            case 0x40: // generate -ABS
                m_cpu.ab_sel = m_cpu.ab;
                if (m_dbg) {
                    dbglog("-ABS with AB=%02X\n", m_cpu.ab_sel);
                }
                //UI_Info("CPU:ABS when AB=%02X\n", m_cpu.ab);
                m_sys.cpu_ABS(m_cpu.ab_sel);  // address bus strobe
                break;

            default:
                break;
        }
        decode_M_field(uop, 0x0);
        m_cpu.ic++;
        break;

    case OP_SR: // subroutine return
        decode_M_field(uop, a_op);
        m_cpu.icsp = (m_cpu.icsp + 1) & ICSTACK_MASK;       // wraps
#if 1
        // the real design pushes the address of the calling ucode
        // and on return goes back to that same address; however, a
        // flag is set on the return that ignores the subroutine
        // call of the next cycle.
        m_cpu.ic      = m_cpu.icstack[m_cpu.icsp];
        m_cpu.prev_sr = true;
#else
        // for the sake of clarity, for now, skip the unusual
        // return shenanigans and just adjust ic directly.
        m_cpu.ic      = (uint16)(m_cpu.icstack[m_cpu.icsp] + 1);
        m_cpu.prev_sr = false;
#endif
        break;

    case OP_TPI: // transfer PC to IC
        decode_M_field(uop, a_op);
        m_cpu.ic = m_cpu.pc;
        break;

    case OP_TIP: // transfer IC to PC
        decode_M_field(uop, a_op);
        m_cpu.pc = m_cpu.ic;
        m_cpu.ic++;
        break;

    case OP_TMP: // transfer memory size to PC
        // note: the resulting value is in nibbles
        // p 2-11 contains the switch settings for various RAM sizes:
        //   4K = 000,  8K=001, 12K=010, 16K=011
        //  20K = 100, 24K=101, 28K=110, 32K=111
        // that is, it should be (#4K blocks - 1)
        decode_M_field(uop, a_op);
        m_cpu.pc = (uint16)( (((m_memsize_KB>>2)-1) << 13) | (1<<12) );
        m_cpu.ic++;
        break;

    case OP_TP: // transfer PC to Aux
        r_field = (uop & 0xF);
        decode_M_field(uop, a_op);
        m_cpu.aux[r_field] = (uint16)(m_cpu.pc + (int16)(puop->p16));
        m_cpu.ic++;
        break;

    case OP_TA: // transfer Aux to PC
        r_field = (uop & 0xF);
        // NOTE: PC must be updated *after* memory access
        decode_M_field(uop, a_op);
        m_cpu.pc = m_cpu.aux[r_field];
        m_cpu.ic++;
        break;

    case OP_XP: // exchange PC and Aux
        r_field = (uop & 0xF);
        // NOTE: PC must be updated *after* memory access
        decode_M_field(uop, a_op);
        // swap
        tmp_pc = m_cpu.pc;
        m_cpu.pc = m_cpu.aux[r_field];
        m_cpu.aux[r_field] = (uint16)(tmp_pc + (int16)(puop->p16));
        m_cpu.ic++;
        break;

    default:
        assert(false);
        return EXEC_ERR;
    }

    // instruction ended normally
    m_scheduler->TimerTick(16);  // all operations take 16 100ns ticks
    return 16;                   // let event loop know
}


// =======================================================
// externally visible CPU module interface
// =======================================================

// create a CPU instance.
// ramsize should be a multiple of 4.
// subtype selects between the flavors of the cpu
Cpu2200t::Cpu2200t(System2200 &sys,
                   std::shared_ptr<Scheduler> scheduler,
                   int ramsize, int cpu_subtype) :
    Cpu2200(),  // init base class
    m_sys(sys),
    m_scheduler(scheduler),
    m_cpuType(cpu_subtype),
    m_ucode_size( (m_cpuType == CPUTYPE_2200B) ? UCODE_WORDS_2200B
                                               : UCODE_WORDS_2200T ),
    m_krom_size(  (m_cpuType == CPUTYPE_2200B) ?  KROM_WORDS_2200B
                                               :  KROM_WORDS_2200T ),
    m_memsize_KB(ramsize),
    m_dbg(false)
{
    assert(ramsize >= 4 && ramsize <= 32);
    assert((ramsize&3) == 0);           // multiple of 4

    // initialize ucode store from built-in image
    switch (m_cpuType) {
        int i;
        case CPUTYPE_2200B:
            for(i=0; i<m_ucode_size; i++) {
                write_ucode(i, ucode_2200B[i]);
            }
            for(i=0; i<UCODE_WORDS_2200BX; i++) {
                write_ucode(0x7E00+i, ucode_2200BX[i]);
            }
            for(i=0; i<m_krom_size; i++) {
                m_kROM[i] = kROM_2200B[i];
            }
            break;
        case CPUTYPE_2200T:
            for(i=0; i<m_ucode_size; i++) {
                write_ucode(i, ucode_2200T[i]);
            }
            for(i=0; i<m_krom_size; i++) {
                m_kROM[i] = kROM_2200T[i];
            }
            break;
        default:
            assert(false);
    }

#if 0
    // disassemble all microcode
    {
        char buff[200];
        uint16 pc;
        for(pc=0x0000; pc<m_ucode_size; pc++) {
            (void)dasm_one(buff, pc, m_ucode[pc].ucode & 0x000FFFFF);
            dbglog(buff);
        }
        if (m_cpuType == CPUTYPE_2200B) {
            // disassemble the patch ROM
            for(pc=0x7E00; pc<0x7E00+UCODE_WORDS_2200BX; pc++) {
                (void)dasm_one(buff, pc, m_ucode[pc].ucode & 0x000FFFFF);
                dbglog(buff);
            }
        }
    }
#endif

    reset(true);
}


// frees any allocated resources at the end of the simulation
Cpu2200t::~Cpu2200t()
{
    // ...
}


// report CPU type
int
Cpu2200t::getCpuType() const
{
    return m_cpuType;
}


// report how much memory the CPU has, in KB
int
Cpu2200t::getRamSize() const
{
    return m_memsize_KB;
}


// true=cold boot (power cycle), false=warm restart
void
Cpu2200t::reset(bool hard_reset)
{
    int i;

    m_cpu.ic        = 0x0000;
    m_cpu.icsp      = ICSTACK_TOP;
    m_cpu.prev_sr   = false;
    m_cpu.wolf_trap = hard_reset;

#if 0
    // the real HW doesn't reset these, but do it anyway for purity
    m_cpu.pc = 0;
#if 0 // interestingly, this must not be reset, or warm reset doesn't work
    for(int i=0; i<16; i++) {
        m_cpu.aux[i] = 0x0000;
    }
#endif
    for(int i=0; i<8; i++) {
        m_cpu.reg[i] = 0x0;
    }
    for(int i=0; i<ICSTACK_SIZE; i++) {
        m_cpu.icstack[i] = 0x0000;
    }
    m_cpu.c = 0x00;
    m_cpu.k = 0x00;
    m_cpu.ab = 0x00;
    m_cpu.ab_sel = 0x00;
    m_cpu.st1 = 0x0;
    m_cpu.st2 = 0x0;
    m_cpu.st3 = 0x0;
    m_cpu.st4 = 0x0;
#endif

    if (hard_reset) {
        for(i=0; i<(m_memsize_KB<<10); i++) {
            m_RAM[i] = 0xFF;
            // it appears that either bit 0 or bit 4 must be set
            // otherwise bad things happen.
            // 0x00 causes "SYSTEM ERROR!"
            // 0x01 is OK
            // 0x02 causes some type of weird crash that resolves OK
            // 0x04 causes some type of weird crash that fills the screen with "@"
            // 0x08 causes some type of weird crash that fills the screen with "LIST "
            // 0x10 is OK
            // 0x11 is OK
            // 0x20 causes "SYSTEM ERROR!"
            // 0x21 is OK
            // 0x40 causes "SYSTEM ERROR!"
            // 0x41 is OK
            // 0x80 causes "SYSTEM ERROR!"
            // 0x81 is OK
            // 0xE0 causes "SYSTEM ERROR!"
            // 0xE1 is OK
            // 0xE8 fills the screen with "DISK "
            // 0xEC fills the screen with "DEFFN"
            // 0xEE fills the screen with "?"
            // 0xCD is OK
            // 0xFE is OK
            // 0xFE is a bad crash
            // 0xFF is OK
        }
    }

    m_status = CPU_RUNNING;
}


// this function is called by a device to return requested data.
// in the real hardware, the selected IO device drives the IBS signal active
// for 7 uS via a one-shot.  In the emulator, the strobe is effectively
// instantaneous.
void
Cpu2200t::IoCardCbIbs(int data)
{
    // we shouldn't receive an IBS while the cpu is busy
    assert( (m_cpu.st1 & ST1_MASK_CPB) == 0 );
    m_cpu.k = (uint8)(data & 0xFF);
    m_cpu.st1 |= ST1_MASK_CPB;          // CPU busy; inhibit IBS
    m_sys.cpu_CPB( true );              // the cpu is busy now

    // return special status if it is a special function key
    if (data & IoCardKeyboard::KEYCODE_SF) {
        m_cpu.st1 |= ST1_MASK_SF;       // special function key
    }
}


// when a card is selected, or its status changes, it uses this function
// to notify the core emulator about the new status.
// I'm not sure what the names should be in general, but these are based
// on what the keyboard uses them for.  data_avail shows up at
// haltstep: st3 bit 2 and is used to indicate the halt/step key is pressed.
//   (perhaps this is always connected and doesn't depend on device selection)
//    ready: st3 bit 0 and is used to indicate the device has data ready.
void
Cpu2200t::halt()
{
    // set the halt/step key notification
    m_cpu.st3 = (uint4)(m_cpu.st3 | ST3_MASK_HALT);
}


// this signal is called by the currently active I/O card
// when its busy/ready status changes.  If no card is selected,
// it floats to one (it is an open collector bus signal).
void
Cpu2200t::setDevRdy(bool ready)
{
    m_cpu.st3 = (uint4)( (m_cpu.st3 & ~ST3_MASK_DEVRDY) |
                         (ready ? ST3_MASK_DEVRDY : 0) );
}


// the disk controller is odd in that it uses the AB bus to signal some
// information after the card has been selected.  this lets it peek into
// that part of the cpu state.
uint8
Cpu2200t::getAB() const
{
    return m_cpu.ab;
}


// run for ticks*100ns
void
Cpu2200t::run(int ticks)
{
    int op_ticks = 0;

    // special behavior on a cold start.
    // this could be done inside the loop below, but it robs performance
    // and only happens once after reset.  this approach works only
    // because reset can't appear during the middle of a time slice,
    // only between them.
    if (m_cpu.wolf_trap) {
        op_ticks = exec_one_op();
        ticks -= op_ticks;
        if (ticks > 0) {
            m_cpu.ic = 0x0001;
            m_cpu.wolf_trap = 0;
        }
    }

    while (ticks > 0) {
#if 0
        if (m_dbg) {
            static int g_num_ops = 0;
            char buff[200];
            int illegal;
            dump_state( 1 );
            illegal = dasm_one(buff, m_cpu.ic, m_ucode[m_cpu.ic].ucode & 0x000FFFFF);
            dbglog("cycle %5d: %s", g_num_ops, buff);
            g_num_ops++;
            if (illegal) {
                break;
            }
        }
#endif
        op_ticks = exec_one_op();
        ticks -= op_ticks;
    }

    m_status = (op_ticks == EXEC_ERR) ? CPU_HALTED : CPU_RUNNING;
}

// vim: ts=8:et:sw=4:smarttab
