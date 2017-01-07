// emulate the Wang 2200 VP micromachine
//
// TODO:
//   *) there are some instruction interpretation issues that aren't
//      clear to me and I'm not sure if I have them all right.
//      specifically, if an instruction sets PH or PL and the A operand
//      also increments or decrements PC, is the increment/decrement on
//      the value of PC before or after C-bus has been saved to PC?
//      everything seems to work as-is, but it might be worthwhile to
//      confirm these assumptions (eg, change the behavior and see
//      if diags still pass).

#include "Ui.h"
#include "Cpu2200.h"
#include "IoCardKeyboard.h"
#include "Scheduler.h"
#include "System2200.h"
#include "ucode_2200.h"

// control which functions get inlined
#define INLINE_STORE_C 1
#define INLINE_MEM_RD  1  // FIXME: this doesn't work, due to static declaration
#define INLINE_DD_OP   1  // FIXME: this doesn't work, due to static declaration
#define INLINE_GET_Hb  1

bool g_dbg_trace = false;

// if this is defined as 0, a few variables get initialized
// unnecessarily, which may very slightly slow down the emulation,
// but which will result in the compiler complaining about potentially
// uninitialized variables.
#define NO_LINT_WARNINGS 1

#ifdef _MSC_VER
#pragma warning( disable : 4127 )       // constant expressions are OK
#endif

// ------------------------------------------------------------------------
//  assorted notes
// ------------------------------------------------------------------------
//
// status high bits
enum { SH_MASK_CARRY  = 0x01,   // CARRY (H/M)
                                //    0 = no carry
                                //    1 = carry
       SH_MASK_CPB    = 0x02,   // CRB ((H/M) (alias KBD)
                                //    0 = allow input from KBD or selected
                                //        device (i.e., CPU is ready)
       SH_MASK_SF     = 0x04,   // KFN (H/M)
                                //    set to 1 when input received from KBD
                                //    is special function code.  it is a
                                //    9th data bit for input.
       SH_MASK_DEVRDY = 0x08,   // RB (H)
                                //    0 = device not enabled or busy
                                //    1 = device enabled and ready
       SH_MASK_30MS   = 0x10,   // TIMER (H/M)
                                //    0 = timer not running
                                //    1 = timer running
                                // this timer is triggered by a CIO operation
                                // and stays high for 30 ms.  if retriggered,
                                // the 30 ms period begins from that point.
                                // it is used by the MVP timeslicing code.
       SH_MASK_HALT   = 0x20,   // HALT (H/M)
                                //    set to 1 when halt/step pressed on KBD
       SH_MASK_PARITY = 0x40,   // PARITY (H/M)
                                //    set to 1 when a parity error occurs
                                //    on control or data memory
       SH_MASK_DPRTY  = 0x80    // DPRTY (M)
                                //    0 = trap if data parity error
                                //    1 = do not trap if data parity error
        };

// NOTE: (M)   = set by microprogram only.
//       (H)   = set by hardware only (d.c. level).
//       (H/M) = set by microprogram or hardware.

// ------------------------------------------------------------------------
// implementation types -- don't need to be exposed in the interface
// ------------------------------------------------------------------------

#define TRAP_PECM  0x8000       // parity error in control memory
#define TRAP_RESET 0x8001       // warm start
#define TRAP_PEDM  0x8002       // parity error in data memory
#define TRAP_POWER 0x8003       // cold start

// ------------------------------------------------------------------------
// write_ucode() must be called to write anything to the ucode store.
// besides setting the specified entry to the specified value, some
// predecoding is performed and saved to speed up instruction cracking.
// ------------------------------------------------------------------------

typedef enum {

    // misc
    OP_PECM,            // bad control memory parity
    OP_ILLEGAL,         // illegal instruction

    // register instructions
    OP_OR,  OP_ORX,
    OP_XOR, OP_XORX,
    OP_AND, OP_ANDX,
    OP_SC,  OP_SCX,
    OP_DAC, OP_DACX,
    OP_DSC, OP_DSCX,
    OP_AC,  OP_ACX,
    OP_M,   OP_MX,
    OP_SH,  OP_SHX,

    // register immediate instructions
    OP_ORI,
    OP_XORI,
    OP_ANDI,
    OP_AI,
    OP_DACI,
    OP_DSCI,
    OP_ACI,
    OP_MI,

    // mini instructions
    OP_TAP,
    OP_TPA,
    OP_XPA,
    OP_TPS,
    OP_TSP,
    OP_RCM,
    OP_WCM,
    OP_SR,
    OP_CIO,
    OP_LPI,

    // mask branch instructions
    OP_BT,
    OP_BF,
    OP_BEQ,
    OP_BNE,

    // register branch instructions
    OP_BLR,  OP_BLRX,
    OP_BLER, OP_BLERX,
    OP_BER,
    OP_BNR,

    // branch instructions
    OP_SB,
    OP_B

} op_t;

enum {
    FETCH_B  = 0x80000000,      // load b_op  according to uop[3:0]
    FETCH_A  = 0x40000000,      // load a_op  according to uop[7:4]
    FETCH_AB = 0xC0000000,      // fetch a_op and b_op
    FETCH_X  = 0x20000000,      // get a_op, a_op2, b_op, b_op2
    FETCH_CY = 0x10000000       // perform CY operation
};

// 10b page branch target address
#define PAGEBR(uop) ((uint16)(((addr) & 0xFC00) | (((uop) >> 8) & 0x03FF)))
// 16b full branch target address
#define FULLBR(uop) ((uint16)((((uop) >> 8) & 0x03FF) | (((uop) << 8) & 0xFC00)))

// 8b immediate
#define IMM8(uop) ((((uop) >> 10) & 0xF0) | (((uop) >> 4) & 0xF))

void
Cpu2200vp::write_ucode(uint16 addr, uint32 uop)
{
    static const int pc_adjust_tbl[16] = {
         0,  0, 0, 0,  0,  0,  0,  0,
        -1, -1, 0, 0, +1, +1, +1, -1 };
    #define PC_ADJUST(a_field) (pc_adjust_tbl[(a_field)])

    // 3b field map to adjust pc on store
    static const int incmap[8] = { 0, +1, +2, +3, 0, -1, -2, -3 };

    const int a_field = (uop >>  4) & 0xF;
    const int c_field = (uop >>  8) & 0xF;
    const int d_field = (uop >> 12) & 0x3;

    const int lpi_op  = ((uop & 0x790000) == 0x190000);
    const int mini_op = ((uop & 0x618000) == 0x018000);
    const int shft_op = ((uop & 0x71C000) == 0x004000);

    int illegal = 0;    // innocent until proven guilty

    uint32 fold;

    uop &= 0x00FFFFFF;  // only 24b are meaningful

    m_ucode[addr].ucode = uop;
    m_ucode[addr].p8    = 0;    // default
    m_ucode[addr].p16   = 0;    // default

    // check parity
    fold = (uop << 16) ^ uop;
    fold ^= (fold << 8);
    fold ^= (fold << 4);
    fold ^= (fold << 2);
    fold ^= (fold << 1);
    if (~fold & 0x80000000) {

        m_ucode[addr].op  = OP_PECM;    // bad parity

    } else if (lpi_op) {

        if (d_field == 1) {
            m_ucode[addr].ucode |= FETCH_B;
        }
        m_ucode[addr].op  = OP_LPI;
        m_ucode[addr].p16 =
                  (uint16)(   ((uop >> 3) & 0xC000)     // [18:17] -> [15:14]
                            | ((uop >> 2) & 0x3000)     // [15:14] -> [13:12]
                            | ((uop >> 0) & 0x0FFF) );  // [11: 0] -> [11: 0]

    } else if (mini_op) {

        int inc;

        switch ((uop >> 17) & 0xF) {

            case 0x5:   // TAP
                illegal = (uop & 0x7F8000) != 0x0B8000;
                if (d_field >= 2) {
                    m_ucode[addr].ucode |= FETCH_B;
                }
                m_ucode[addr].op  = OP_TAP;
                break;

            case 0x0:   // TPA
                illegal = (uop & 0x7F8800) != 0x018000;
                inc = ((uop >> 12) & 4)         // sign
                    | ((uop >>  9) & 3);        // offset
                if (d_field >= 2) {
                    m_ucode[addr].ucode |= FETCH_B;
                }
                m_ucode[addr].op  = OP_TPA;
                m_ucode[addr].p16 = (uint16)incmap[inc];
                break;

            case 0x1:   // XPA
                illegal = (uop & 0x7F8800) != 0x038000;
                inc = ((uop >> 12) & 4)         // sign
                    | ((uop >>  9) & 3);        // offset
                if (d_field >= 2) {
                    m_ucode[addr].ucode |= FETCH_B;
                }
                m_ucode[addr].op  = OP_XPA;
                m_ucode[addr].p16 = (uint16)incmap[inc];
                break;

            case 0x2:   // TPS
                illegal = (uop & 0x7F8800) != 0x058000;
                inc = ((uop >> 12) & 4)         // sign
                    | ((uop >>  9) & 3);        // offset
                if (d_field >= 2) {
                    m_ucode[addr].ucode |= FETCH_B;
                }
                m_ucode[addr].op  = OP_TPS;
                m_ucode[addr].p16 = (uint16)incmap[inc];
                break;

            case 0x6:   // TSP
                illegal = (uop & 0x7F8800) != 0x0D8000;
                if (d_field >= 2) {
                    m_ucode[addr].ucode |= FETCH_B;
                }
                m_ucode[addr].op  = OP_TSP;
                break;

            case 0x3:   // SR (subroutine return)
                if ((uop & 0x7F8E00) == 0x078600) {
                    // SR,RCM (read control memory and subroutine return)
                    m_ucode[addr].op  = OP_RCM;
                } else if ((uop & 0x7F8E00) == 0x078400) {
                    // SR,WCM (write control memory and subroutine return)
                    m_ucode[addr].op  = OP_WCM;
                } else if ((uop & 0x7F8C00) == 0x078000) {
                    // perform subroutine return
                    if (d_field >= 2) {
                        m_ucode[addr].ucode |= FETCH_B;
                    }
                    m_ucode[addr].op  = OP_SR;
                } else {
                    illegal = 1;
                    m_ucode[addr].op  = OP_ILLEGAL;
                }
                break;

            case 0xB:   // CIO (control input/output)
                illegal = (uop & 0x7FB000) != 0x178000;
                m_ucode[addr].op  = OP_CIO;
                break;

            default:
                illegal = 1;
                break;
        }

    } else if (shft_op) {

        const int x_field = (uop >> 17) & 1;

        if (x_field) {
            illegal = (c_field == 9) || (c_field == 10) || (c_field == 11);
            m_ucode[addr].ucode |= FETCH_X;
            m_ucode[addr].op  = OP_SHX;
        } else {
            illegal = (c_field == 10) || (c_field == 11);
            m_ucode[addr].ucode |= FETCH_AB;
            m_ucode[addr].op  = OP_SH;
            m_ucode[addr].p16 = (uint16)(PC_ADJUST(a_field));
        }

    } else { // neither lpi nor mini_op nor shift

        const int op = (uop >> 18) & 0x1F;
        int x_field;
        switch (op) {

        // register instructions:

            case 0x00:  // OR
            case 0x01:  // XOR
            case 0x02:  // AND
            case 0x03:  // SC: subtract w/ carry; cy=0 means borrow; cy=1 is no borrow
            case 0x04:  // DAC: decimal add w/ carry
            case 0x05:  // DSC: decimal subtract w/ carry
            case 0x06:  // AC: binary add w/ carry
                if (((uop >> 14) & 3) >= 2) {
                    m_ucode[addr].ucode |= FETCH_CY;    // clear or set
                }
            case 0x07:  // M: multiply
                illegal = (uop & 0x010000) != 0x000000;
                x_field = (uop >> 17) & 1;
                if (x_field) {
                    illegal |= (c_field == 9) || (c_field == 10) || (c_field == 11);
                    m_ucode[addr].ucode |= FETCH_X;
                    m_ucode[addr].op  = (uint8)(OP_ORX + 2*op);
                } else {
                    illegal |= (c_field == 10) || (c_field == 11);
                    m_ucode[addr].ucode |= FETCH_AB;
                    m_ucode[addr].op  = (uint8)(OP_OR + 2*op);
                    m_ucode[addr].p16 = (uint16)(PC_ADJUST(a_field));
                }
                break;

        // register immediate instructions:

            case 0x08:  // ORI: or immediate
            case 0x09:  // XORI: xor immediate
            case 0x0A:  // ANDI: and immediate
            case 0x0B:  // AI: binary add immediate
            case 0x0C:  // DACI: decimal add immediate w/ carry
            case 0x0D:  // DSCI: decimal subtract immediate w/ carry
            case 0x0E:  // ACI: binary add immediate w/ carry
            case 0x0F:  // MI: binary multiply immediate
                illegal |= (c_field == 10) || (c_field == 11);
                m_ucode[addr].ucode |= FETCH_B;
                m_ucode[addr].op  = (uint8)(OP_ORI + (op-0x08));
                break;

        // register branch instructions:

            case 0x10: case 0x11:       // BLR: branch if R[AAAA] < R[BBBB]
            case 0x12: case 0x13:       // BLER: branch if R[AAAA] <= R[BBBB]
            case 0x14:                  // BEQ: branch if R[AAAA] == R[BBBB]
            case 0x16:                  // BNE: branch if R[AAAA] != R[BBBB]
                x_field = (uop >> 18) & 1;
                if (x_field) {
                    m_ucode[addr].ucode |= FETCH_X;
                    m_ucode[addr].op  = (uint8)
                                      ( (op <= 0x11) ? OP_BLRX
                                                     : OP_BLERX );
                } else {
                    m_ucode[addr].ucode |= FETCH_AB;
                    m_ucode[addr].op  = (uint8)
                                      ( (op <= 0x11) ? OP_BLR
                                      : (op <= 0x13) ? OP_BLER
                                      : (op == 0x14) ? OP_BER
                                                     : OP_BNR );
                }
                m_ucode[addr].p8  = (uint8)(PC_ADJUST(a_field));
                m_ucode[addr].p16 = PAGEBR(uop);
                break;

        // branch instructions:

            case 0x15:  // subroutine branch
                m_ucode[addr].op  = OP_SB;
                m_ucode[addr].p16 = FULLBR(uop);
                break;
            case 0x17:  // unconditional branch
                m_ucode[addr].op  = OP_B;
                m_ucode[addr].p16 = FULLBR(uop);
                break;

        // mask branch instructions:

            case 0x18: case 0x19:       // branch if true
                m_ucode[addr].ucode |= FETCH_B;
                m_ucode[addr].op  = OP_BT;
                m_ucode[addr].p16 = PAGEBR(uop);
                break;
            case 0x1A: case 0x1B:       // branch if false
                m_ucode[addr].ucode |= FETCH_B;
                m_ucode[addr].op  = OP_BF;
                m_ucode[addr].p16 = PAGEBR(uop);
                break;
            case 0x1C: case 0x1D:       // branch if = to mask
                m_ucode[addr].ucode |= FETCH_B;
                m_ucode[addr].op  = OP_BEQ;
                m_ucode[addr].p16 = PAGEBR(uop);
                break;
            case 0x1E: case 0x1F:       // branch if != to mask
                m_ucode[addr].ucode |= FETCH_B;
                m_ucode[addr].op  = OP_BNE;
                m_ucode[addr].p16 = PAGEBR(uop);
                break;

            default: // impossible
                assert(false);
                break;
        }

    } // all other ops

    if (illegal) {
        m_ucode[addr].ucode &= 0x00FFFFFF;      // clear flags we might have set
        m_ucode[addr].op     = OP_ILLEGAL;
        m_ucode[addr].p8     = 0;
        m_ucode[addr].p16    = 0;
    }
}


// ------------------------------------------------------------------------
// instruction interpretation subroutines
// ------------------------------------------------------------------------

// return 0 or 1 based on the st1 carry flag
#define CARRY_BIT ((m_cpu.sh & SH_MASK_CARRY)?1:0)

// set the sh carry flag in accordance with bit 8 of v
#define SET_CARRY(v)                                              \
        do {                                                      \
        m_cpu.sh = (uint8)( (m_cpu.sh & ~SH_MASK_CARRY) |         \
                            (((v) & 0x100) ? SH_MASK_CARRY : 0)); \
        } while (false)

// increment and decrement the ucode subroutine stack
#define INC_ICSP                                        \
        do {                                            \
        if (++m_cpu.icsp >= STACKSIZE) m_cpu.icsp = 0;  \
        } while (false)

#define DEC_ICSP                                        \
        do {                                            \
        if (--m_cpu.icsp < 0) m_cpu.icsp = STACKSIZE-1; \
        } while (false)

// setting SL can have more complicated side effects.
// we keep shadow state of the memory bank addressing bits.
void
Cpu2200vp::set_sl(uint8 value)
{
    m_cpu.sl = (value & 0xFF);

    if (m_memsize_KB <= 64) {
        m_cpu.bank_offset = 0;
    } else if (m_memsize_KB <= 128) {
        m_cpu.bank_offset = ((value >> 6) & 1) << 16; // bit [6]
    } else if (m_memsize_KB <= 256) {
        m_cpu.bank_offset = ((value >> 6) & 3) << 16; // bits [7:6]
    } else if (m_memsize_KB <= 512) {
        m_cpu.bank_offset = (((value >> 6) & 3) << 16)  // bits [7:6]
                          | (((value >> 5) & 1) << 18); // bit [5]
    } else {
        assert(false);
        m_cpu.bank_offset = 0;
    }
}


// setting SH can have more complicated side effects.
// also, microcode can't affect certain bits.
void
Cpu2200vp::set_sh(uint8 value)
{
    int cpb_changed = ((m_cpu.sh ^ value) & SH_MASK_CPB);

    uint8 mask = SH_MASK_DEVRDY;        // ucode can't change this
    m_cpu.sh = (uint8)(   (~mask & value)
                        | ( mask & m_cpu.sh) );

    if (cpb_changed) {
        m_sys->cpu_CPB( !!(m_cpu.sh & SH_MASK_CPB) );
    }
}


// 9b result: carry out and 8b result
uint16
Cpu2200vp::decimal_add8(int a_op, int b_op, int ci) const
{
    int a_op_low  = (a_op >> 0) & 0xF;
    int b_op_low  = (b_op >> 0) & 0xF;
    int a_op_high = (a_op >> 4) & 0xF;
    int b_op_high = (b_op >> 4) & 0xF;
    int sum_low, sum_high;
    int co;

#if 0   // MVP diagnostics actually hit "illegal" cases
    assert(a_op_low < 10);
    assert(b_op_low < 10);
    assert(a_op_high < 10);
    assert(b_op_high < 10);
#endif

    sum_low = a_op_low + b_op_low + ci; // ranges from binary 0 to 19
    co      = (sum_low > 9);
    if (co) {
        sum_low -= 10;
    }

    sum_high = a_op_high + b_op_high + co; // ranges from binary 0 to 19
    co       = (sum_high > 9);
    if (co) {
        sum_high -= 10;
    }

    return (uint16)((co << 8) + (sum_high << 4) + sum_low);
}


// 9b result: carry out and 8b result
// if ci is 0, it means compute a-b.
// if ci is 1, it means compute a-b-1.
// msb of result is new carry bit: 1=borrow, 0=no borrow
uint16
Cpu2200vp::decimal_sub8(int a_op, int b_op, int ci) const
{
    int a_op_low  = (a_op >> 0) & 0xF;
    int b_op_low  = (b_op >> 0) & 0xF;
    int a_op_high = (a_op >> 4) & 0xF;
    int b_op_high = (b_op >> 4) & 0xF;
    int sum_low, sum_high;
    int borrow;

#if 0   // MVP diagnostics actually hit "illegal" cases
    assert(a_op_low < 10);
    assert(b_op_low < 10);
    assert(a_op_high < 10);
    assert(b_op_high < 10);
#endif

    b_op_low  = 9 - b_op_low;
    b_op_high = 9 - b_op_high;

 // the +1 below is for 10's complement
 // sum_low = a_op_low + b_op_low + 1 - ci; // ranges from binary 0 to 19
 // but the (+1 - ci) can be collapsed
    sum_low = a_op_low + b_op_low + !ci; // ranges from binary 0 to 19
    if (sum_low > 9) {
        sum_low -= 10;
        borrow = 0;
    } else {
        borrow = 1;
    }

    sum_high = a_op_high + b_op_high + !borrow; // ranges from binary 0 to 19
    if (sum_high > 9) {
        sum_high -= 10;
        borrow = 0;
    } else {
        borrow = 1;
    }

    return (uint16)((borrow << 8) + (sum_high << 4) + sum_low);
}


// store results into the specified register
#define inlined_store_c(c_field, val)                                   \
    do {                                                                \
        int v = (val) & 0xFF; /* often 9b from carry out */             \
        int cf  = c_field;                                              \
        switch (cf) {                                                   \
            case 0: case 1: case 2: case 3:                             \
            case 4: case 5: case 6: case 7:                             \
                m_cpu.reg[cf] = (uint8)v;                               \
                break;                                                  \
            case  8: m_cpu.pc = (uint16)((m_cpu.pc & 0xFF00) |  v);     break; /* PL */ \
            case  9: m_cpu.pc = (uint16)((m_cpu.pc & 0x00FF) | (v<<8)); break; /* PH */ \
            case 10: break;     /* CL; illegal */                       \
            case 11: break;     /* CH; illegal */                       \
            case 12: set_sl((uint8)v); break;                           \
            case 13: set_sh((uint8)v); break;                           \
            case 14: m_cpu.k  = (uint8)v;  break;                       \
            case 15: break;     /* dummy (don't save results) */        \
        }                                                               \
    } while (false)

#if INLINE_STORE_C
    #define store_c(c_field,val) inlined_store_c(c_field,val)
#else
    // store results into the specified register
    static void
    store_c(unsigned int c_field, int val) { inlined_store_c(c_field,val); }
#endif


// addresses < 8KB always refer to bank 0.
// otherwise, add the bank offset, and force the addr to zero if it is too big
#define inline_map_address(addr)   \
    (   ((addr) < 8192) ? (addr)     \
      : ((addr) + m_cpu.bank_offset < (m_memsize_KB<<10)) ? (m_cpu.bank_offset+(addr)) \
      : (0) \
    )


// read from the specified address
#define inlined_mem_read8(addr) m_RAM[inline_map_address(addr)]

#if INLINE_MEM_RD
    #define mem_read8(addr) inlined_mem_read8(addr)
#else
    static uint8
    mem_read8(int addr)
    {
        int rv;
        assert(addr >= 0);
        rv = inlined_mem_read8(addr);
        //dbglog("READ RAM[0x%04X] = 0x%02X\n", addr, rv);
        return (uint8)rv;
    }
#endif


// write to the specified address.
// addresses < 8 KB always map to bank 0,
// otherwise we add the bank offset.
// there are two modes: write 1 and write 2
// write1 means write to the specified address.
// write2 means write to (address ^ 1).
#define inlined_mem_write(addr,wr_value,write2) \
    do {                                        \
        int la = (addr);                        \
        if (la < 8192) {                        \
            la ^= write2;                       \
            m_RAM[la] = (uint8)wr_value;        \
        } else if (la + m_cpu.bank_offset < (m_memsize_KB<<10)) { \
            la += m_cpu.bank_offset;            \
            la ^= write2;                       \
            m_RAM[la] = (uint8)wr_value;        \
        }                                       \
    } while (false)

// return the chosen bits of B and A, returns with the bits
// of b in [7:4] and the bits of A in [3:0]
uint8
Cpu2200vp::get_HbHa(int HbHa, int a_op, int b_op) const
{
    int rslt;

    switch (HbHa) {
        case 0: // Hb=0, Ha=0
            rslt = ((b_op << 4) & 0xF0)
                 | ((a_op >> 0) & 0x0F);
            break;
        case 1: // Hb=0, Ha=1
            rslt = ((b_op << 4) & 0xF0)
                 | ((a_op >> 4) & 0x0F);
            break;
        case 2: // Hb=1, Ha=0
            rslt = ((b_op << 0) & 0xF0)
                 | ((a_op >> 0) & 0x0F);
            break;
        case 3: // Hb=1, Ha=1
            rslt = ((b_op << 0) & 0xF0)
                 | ((a_op >> 4) & 0x0F);
            break;
        default:
            assert(false);
            rslt = 0;   // keep lint happy
            break;
    }
    return (uint8)rslt;
}


#define inlined_get_Hb(Hb, b_op)                \
    ( ((Hb)&1) ? (((b_op) >> 4) & 0xF)          \
               : (((b_op) >> 0) & 0xF) )

#if INLINE_GET_Hb
    #define get_Hb(Hb, b_op) inlined_get_Hb(Hb, b_op)
#else
    static int
    get_Hb(int Hb, int b_op)
    {
        return inlined_get_Hb(Hb, b_op);
    }
#endif



// decode the DD field and perform memory rd/wr op if specified
#define inlined_perform_dd_op(uop,wr_val)                       \
    {                                                           \
        int d_field = ((uop) >> 12) & 0x3;                      \
        switch (d_field) {                                      \
            case 0: /* nothing */                               \
                break;                                          \
            case 1: /* read */                                  \
                m_cpu.ch = mem_read8(m_cpu.orig_pc);            \
                m_cpu.cl = mem_read8(m_cpu.orig_pc ^ 1);        \
                break;                                          \
            default:                                            \
                inlined_mem_write(m_cpu.orig_pc, wr_val, d_field==3); \
                break;                                          \
        }                                                       \
    }

#if INLINE_DD_OP
    #define perform_dd_op(uop,wr_val)           \
        do {                                    \
            inlined_perform_dd_op(uop,wr_val)   \
        } while (false)
#else
    static void
    perform_dd_op(uint32 uop, int wr_val) { inlined_perform_dd_op(uop,wr_val); }
#endif


// ------------------------------------------------------------------------
//  private functions
// ------------------------------------------------------------------------

// perform one instruction and return.
// returns the number of ticks the instruction took.
// on an error, ticks is set to a huge value EXEC_ERR.
#define EXEC_ERR (1<<30)
int
Cpu2200vp::exec_one_op()
{
    ucode_t *puop = &m_ucode[m_cpu.ic];
    const uint32 uop = puop->ucode;

    int ticks = 6;      // almost all instructions take 6 cycles (600 ns)

    int a_field, b_field, c_field, s_field, t_field, HbHa;
    int a_op, b_op, a_op2, b_op2, imm, rslt, rslt2;
    int idx;
    uint16 tmp16;

    // internally, the umachine makes a copy of the start PC value
    // since memory read and write are done relative to that state
    // in the case that the instruction modifies PH or PL itself.
    m_cpu.orig_pc = m_cpu.pc;

#if NO_LINT_WARNINGS
    a_op = a_op2 = b_op = b_op2 = 0;
#endif

    if (uop & FETCH_CY) {
        // set or clear carry
        // we must do this before FETCH_A/B because it can affect SH state
        switch ((uop >> 14) & 3) {
            case 2: m_cpu.sh &= ~SH_MASK_CARRY; break;    // clear
            case 3: m_cpu.sh |=  SH_MASK_CARRY; break;    // set
            case 0:     // no change, but this shouldn't be called then
            default:
                assert(false);
                break;
        }
    }

    // fetch argA and argB as required
    if (uop & FETCH_B) {

        b_field = uop & 0xF;
        switch (b_field) {
            case 0: case 1: case 2: case 3:
            case 4: case 5: case 6: case 7:
                b_op = m_cpu.reg[b_field];
                break;
            case  8: b_op = (uint8)((m_cpu.pc >> 0) & 0xFF); break; // PL
            case  9: b_op = (uint8)((m_cpu.pc >> 8) & 0xFF); break; // PH
            case 10: b_op = m_cpu.cl; break;
            case 11: b_op = m_cpu.ch; break;
            case 12: b_op = m_cpu.sl; break;
            case 13: b_op = m_cpu.sh; break;
            case 14: b_op = m_cpu.k; break;
            case 15: b_op = 0x00; break; // dummy
            default:
                assert(false);
                b_op = 0x00;
                break;
        }

        // A is fetched only if B is fetched as well
        if (uop & FETCH_A) {
            a_field = (uop >> 4) & 0xF;
            switch (a_field) {
                case 0: case 1: case 2: case 3:
                case 4: case 5: case 6: case 7:
                    a_op = m_cpu.reg[a_field];
                    break;
                case  8: case 10: case 12:
                    a_op = m_cpu.cl;
                    break;
                case  9: case 11: case 13:
                    a_op = m_cpu.ch;
                    break;
                case 14: case 15:
                    a_op = 0;
                    break;
                default:
                    assert(false);
                    a_op = 0;
                    break;
            }
            a_op2 = 0;  // keep lint happy
        }

    } else if (uop & FETCH_X) {

        b_field = uop & 0xF;
        switch (b_field) {
            case 0: case 1: case 2: case 3:
            case 4: case 5: case 6:
                b_op  = m_cpu.reg[b_field];
                b_op2 = m_cpu.reg[b_field+1];
                break;
            case 7:
                b_op  = m_cpu.reg[7];
                b_op2 = (uint8)((m_cpu.pc >> 0) & 0xFF);  // PL
                break;
            case  8:
                b_op  = (uint8)((m_cpu.pc >> 0) & 0xFF);  // PL
                b_op2 = (uint8)((m_cpu.pc >> 8) & 0xFF);  // PH
                break;
            case  9:
                b_op  = (uint8)((m_cpu.pc >> 8) & 0xFF);  // PH
                b_op2 = m_cpu.cl;
                break;
            case 10:
                b_op  = m_cpu.cl;
                b_op2 = m_cpu.ch;
                break;
            case 11:
                b_op  = m_cpu.ch;
                b_op2 = m_cpu.sl;
                break;
            case 12:
                b_op  = m_cpu.sl;
                b_op2 = m_cpu.sh;
                break;
            case 13:
                b_op  = m_cpu.sh;
                b_op2 = m_cpu.k;
                break;
            case 14:
                b_op  = m_cpu.k;
                b_op2 = 0x00; // dummy
                break;
            case 15:
                b_op  = 0x00; // dummy
                b_op2 = m_cpu.reg[0];
                break;
            default:
                assert(false);
                b_op = b_op2 = 0;
                break;
        }

        a_field = (uop >> 4) & 0xF;
        switch (a_field) {
            case 0: case 1: case 2: case 3:
            case 4: case 5: case 6:
                a_op  = m_cpu.reg[a_field];
                a_op2 = m_cpu.reg[a_field+1];
                break;
            case 7:
                a_op  = m_cpu.reg[7];
                a_op2 = m_cpu.cl;
                break;
            case  8:
            case 10:
            case 12:
                a_op  = m_cpu.cl;
                a_op2 = m_cpu.ch;
                break;
            case  9:
            case 11:
                a_op  = m_cpu.ch;
                a_op2 = m_cpu.cl;
                break;
            case 13:
                a_op  = m_cpu.ch;
                a_op2 = 0;
                break;
            case 14:
                a_op  = 0;
                a_op2 = 0;
                break;
            case 15:
                a_op  = 0;
                a_op2 = m_cpu.reg[0];
                break;
            default:
                assert(false);
                a_op = a_op2 = 0;
                break;
        }
    }


    // carry out the instruction
    switch (puop->op) {

    case OP_PECM:
        // 1) set SH6 = 1
        m_cpu.sh |= SH_MASK_PARITY;
        if (~m_cpu.sh & SH_MASK_DPRTY) {
            // data parity trap is enabled
            // 2) instruction addr+1 is pushed on subroutine return stack
            m_cpu.icstack[m_cpu.icsp] = (uint16)(m_cpu.ic + 1);
            DEC_ICSP;
            // 3) trap to location 0x8000
            m_cpu.ic = TRAP_PECM;
        }
        break;

    case OP_ILLEGAL:
        {
            char buff[200];
            (void)dasm_one_vp(buff, m_cpu.ic, m_ucode[m_cpu.ic].ucode);
            UI_Error("%s\nIllegal op at ic=%04X", buff, m_cpu.ic);
        }
        return EXEC_ERR;

    case OP_LPI:
        m_cpu.pc = puop->p16;
        m_cpu.orig_pc = m_cpu.pc;       // LPI is a special case where change
                                        //    of PC is seen by R and W
        perform_dd_op(uop, 0x00);       // force B field to pick 0
        m_cpu.ic++;
        ticks = 11;
        break;

    case OP_TAP:
        perform_dd_op(uop, b_op);
        idx = (uop >> 4) & 0x1F;
        m_cpu.pc = m_cpu.aux[idx];
        m_cpu.ic++;
        break;

    case OP_TPA:
        perform_dd_op(uop, b_op);
        idx = (uop >> 4) & 0x1F;
        m_cpu.aux[idx] = (uint16)(m_cpu.pc + (int16)puop->p16);
        m_cpu.ic++;
        break;

    case OP_XPA:
        perform_dd_op(uop, b_op);
        idx = (uop >> 4) & 0x1F;
        tmp16 = m_cpu.aux[idx];
        m_cpu.aux[idx] = (uint16)(m_cpu.pc + (int16)puop->p16);
        m_cpu.pc = tmp16;
        m_cpu.ic++;
        break;

    case OP_TPS:
        perform_dd_op(uop, b_op);
        m_cpu.icstack[m_cpu.icsp] = (uint16)(m_cpu.pc + (int16)puop->p16);
        DEC_ICSP;
        m_cpu.ic++;
        break;

    case OP_TSP:
        perform_dd_op(uop, b_op);
        INC_ICSP;
        m_cpu.pc = m_cpu.icstack[m_cpu.icsp];
        m_cpu.ic++;
        break;

    case OP_RCM:
        // SR,RCM (read control memory and subroutine return)
        INC_ICSP;
        tmp16 = m_cpu.icstack[m_cpu.icsp];
        m_cpu.k  = (uint8)((m_ucode[tmp16].ucode >> 16) & 0xFF);
        m_cpu.pc = (uint16)(m_ucode[tmp16].ucode & 0xFFFF);
        // perform subroutine return
        INC_ICSP;
        m_cpu.ic = m_cpu.icstack[m_cpu.icsp];
        ticks = 16;     // 16 ticks (1.6 us)
        break;

    case OP_WCM:
        // SR,WCM (write control memory and subroutine return)
        INC_ICSP;
        tmp16 = m_cpu.icstack[m_cpu.icsp];
        // BASIC-3/COBOL required larger control memories, but
        // the boot rom is still stuck in the middle
        if ( (tmp16 < MAX_UCODE) &&
             !((tmp16 >= 0x8000) && (tmp16 < 0x9000)) ) {
            write_ucode(tmp16, ((~m_cpu.k & 0xFF) << 16) | m_cpu.pc);
        }
        // perform subroutine return
        INC_ICSP;
        m_cpu.ic = m_cpu.icstack[m_cpu.icsp];
        ticks = 16;     // 16 ticks (1.6 us)
        break;

    case OP_SR:
        // perform subroutine return
        perform_dd_op(uop, b_op);
        INC_ICSP;
        m_cpu.ic = m_cpu.icstack[m_cpu.icsp];
        ticks = 8;      // 8 ticks (800 ns)
        break;

    case OP_CIO:
        s_field = (uop >> 11) & 0x1;
        t_field = (uop >>  4) & 0x7F;
        if (s_field) {
            m_cpu.ab = m_cpu.k;     // I/O address bus register takes K reg value
        }
        if ((uop & 0xC) == 0xC) {
            // this is not documented in the arch manual, but it appears
            // in the MVP CPU schematic.  if bits 3:2 are both one, the
            // 30 ms one shot gets retriggered.
            m_cpu.sh |= SH_MASK_30MS;     // one shot output rises
            m_tmr_30ms = m_scheduler->TimerCreate( TIMER_MS(27),
                                                   [&](){ tcb30msDone(); } );
// FIXME: if I set the timer to TIMER_MS(30), MVP Basic-2 2.6.2 reports
//        the timeslice as being 36 ms.  what gives?
//        TIMER_MS(29) reports "35 MS TICK".
//        TIMER_MS(28) downto TIMER_MS(22) don't generate any warning.
//        TIMER_MS(17) reports "18 MS TICK" warning.
//        TIMER_MS(15) reports "15/17/18 MS TICK" warning.
        }
        switch (t_field) {
            case 0x40: // ABS
                m_cpu.ab_sel = m_cpu.ab;
                if (m_dbg) {
                    dbglog("-ABS with AB=%02X, ic=0x%04X\n", m_cpu.ab_sel, m_cpu.ic);
                }
                //UI_Info("CPU:ABS when AB=%02X", m_cpu.ab);
                m_sys->cpu_ABS(m_cpu.ab_sel);  // address bus strobe
                break;
            case 0x10: // CBS
                if (m_dbg) {
                    if (m_cpu.k < 32 || m_cpu.k > 128) {
                        dbglog("-CBS when AB=%02X, K=%02X\n", m_cpu.ab_sel, m_cpu.k);
                    } else {
                        dbglog("-CBS when AB=%02X, K=%02X ('%c')\n", m_cpu.ab_sel, m_cpu.k, m_cpu.k);
                    }
                }
                //UI_Info("CPU:CBS when AB=%02X, AB_SEL=%02X, K=%02X", m_cpu.ab, m_cpu.ab_sel, m_cpu.k);
                m_sys->cpu_CBS(m_cpu.k);    // control bus strobe
                break;
            case 0x20: // OBS
                if (m_dbg) {
                    if (m_cpu.k < 32 || m_cpu.k > 128) {
                        dbglog("-OBS when AB=%02X, K=%02X\n", m_cpu.ab_sel, m_cpu.k);
                    } else {
                        dbglog("-OBS when AB=%02X, K=%02X ('%c')\n", m_cpu.ab_sel, m_cpu.k, m_cpu.k);
                    }
                }
                //UI_Info("CPU:OBS when AB=%02X, AB_SEL=%02X, K=%02X", m_cpu.ab, m_cpu.ab_sel, m_cpu.k);
                m_sys->cpu_OBS(m_cpu.k);  // output data bus strobe
                break;
            case 0x08: // empirical behavior
                // the VP BASIC issues this operation in three places.
                //     978080 : CIO       ??? (ILLEGAL)
                // this corresponds to a mask of 0x08.  it would appear
                // that this causes the IN bus to be sampled into K, with
                // no other side effect.  it is use to sample the IN bus
                // with the display board selected in order to determine
                // if it is a 64x16 or 80x24 display (bit 5 indicates it).
                // FIXME: I'm not sure if other mask values produce similar
                //        results, but this is what the BASIC code uses.
                {
                    // This is just a hack to fix the specific known case
                    int ib5 = m_sys->cpu_poll_IB5();
                    m_cpu.k = (uint8)(ib5 << 4);
                }
                break;
            case 0x00: // no strobe
                break;
            default:
                // looking at the 6793 schematic (MVP registers & IO) the
                // 30 ms timeslice one shot is triggered by a CIO operation
                // with (R2&R3), i.e. (((ucode >> 2) & 3) == 0x3), bits that
                // are normally don't care.
            #if 0
                UI_Error("Bad CIO strobe: tt=0x%02X, ic=0x%04X", t_field, m_cpu.ic);
            #else
                // ignore it
            #endif
                break;
        } // t_field
        m_cpu.ic++;
        break;

#define PREAMBLE1       \
        c_field = (uop >> 8) & 0xF

#define POSTAMBLE1      \
        store_c(c_field, rslt);                                 \
        perform_dd_op(uop, rslt);       /* mem rd/wr */         \
        m_cpu.pc = (uint16)(m_cpu.pc + (int16)(puop->p16));     \
        m_cpu.ic++

    case OP_OR:
        PREAMBLE1;
        rslt = a_op | b_op;
        POSTAMBLE1;
        break;

    case OP_XOR:
        PREAMBLE1;
        rslt = a_op ^ b_op;
        POSTAMBLE1;
        break;

    case OP_AND:
        PREAMBLE1;
        rslt = a_op & b_op;
        POSTAMBLE1;
        break;

    case OP_SC: // subtract w/ carry; cy=0 means borrow; cy=1 is no borrow
        PREAMBLE1;
        rslt = a_op + (0xff ^ b_op) + CARRY_BIT;
        SET_CARRY(rslt);
        POSTAMBLE1;
        break;

    case OP_DAC: // decimal add w/ carry
        PREAMBLE1;
        rslt = decimal_add8(a_op, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        POSTAMBLE1;
        break;

    case OP_DSC: // decimal subtract w/ carry
        PREAMBLE1;
        rslt = decimal_sub8(a_op, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        POSTAMBLE1;
        break;

    case OP_AC: // binary add w/ carry
        PREAMBLE1;
        rslt = a_op + b_op + CARRY_BIT;
        SET_CARRY(rslt);
        POSTAMBLE1;
        break;

    case OP_M:
        PREAMBLE1;
        HbHa    = (uop >> 14) & 3;
        rslt = get_HbHa(HbHa, a_op, b_op);
        rslt = ((rslt>>4)&0xF) * (rslt&0xF);
        POSTAMBLE1;
        break;

    case OP_SH:
        PREAMBLE1;
        HbHa = (uop >> 18) & 3;
        rslt = get_HbHa(HbHa, a_op, b_op);
        POSTAMBLE1;
        break;

#define PREAMBLE2 \
        c_field = (uop >> 8) & 0xF

#define POSTAMBLE2                                              \
        store_c(c_field, rslt);                                 \
        store_c((c_field+1) & 0xF, rslt2);                      \
        perform_dd_op(uop, rslt2);      /* mem rd/wr */         \
        m_cpu.ic++

    case OP_ORX:
        PREAMBLE2;
        rslt  = a_op  | b_op;
        rslt2 = a_op2 | b_op2;
        POSTAMBLE2;
        break;

    case OP_XORX:
        PREAMBLE2;
        rslt  = a_op  ^ b_op;
        rslt2 = a_op2 ^ b_op2;
        POSTAMBLE2;
        break;

    case OP_ANDX:
        PREAMBLE2;
        rslt  = a_op  & b_op;
        rslt2 = a_op2 & b_op2;
        POSTAMBLE2;
        break;

    case OP_SCX:
        PREAMBLE2;
        rslt  = a_op  + (0xff ^ b_op)  + CARRY_BIT;
        rslt2 = a_op2 + (0xff ^ b_op2) + ((rslt >> 8) & 1);
        SET_CARRY(rslt2);
        POSTAMBLE2;
        break;

    case OP_DACX:
        PREAMBLE2;
        rslt  = decimal_add8(a_op,  b_op,  CARRY_BIT);
        rslt2 = decimal_add8(a_op2, b_op2, ((rslt >> 8) & 1) );
        SET_CARRY(rslt2);
        POSTAMBLE2;
        break;

    case OP_DSCX:
        PREAMBLE2;
        rslt  = decimal_sub8(a_op,  b_op,  CARRY_BIT);
        rslt2 = decimal_sub8(a_op2, b_op2, ((rslt >> 8) & 1) );
        SET_CARRY(rslt2);
        POSTAMBLE2;
        break;

    case OP_ACX:
        PREAMBLE2;
        rslt  = a_op  + b_op  + CARRY_BIT;
        rslt2 = a_op2 + b_op2 + ((rslt >> 8) & 1) ;
        SET_CARRY(rslt2);
        POSTAMBLE2;
        break;

    case OP_MX:
        PREAMBLE2;
        HbHa = (uop >> 14) & 3;
        rslt  = get_HbHa(HbHa, a_op, b_op);
        rslt2 = get_HbHa(HbHa, a_op2, b_op2);
        rslt  = ((rslt >>4)&0xF) * (rslt &0xF);
        rslt2 = ((rslt2>>4)&0xF) * (rslt2&0xF);
        POSTAMBLE2;
        break;

    case OP_SHX:
        PREAMBLE2;
        HbHa = (uop >> 18) & 3;
        rslt  = get_HbHa(HbHa, a_op,  b_op);
        rslt2 = get_HbHa(HbHa, a_op2, b_op2);
        POSTAMBLE2;
        break;

#define PREAMBLE3                       \
        c_field = (uop >> 8) & 0xF;     \
        imm = IMM8(uop)

#define POSTAMBLE3                      \
        store_c(c_field, rslt);         \
        perform_dd_op(uop, rslt);       \
        m_cpu.ic++

    case OP_ORI:        // or immediate
        PREAMBLE3;
        rslt = imm | b_op;
        POSTAMBLE3;
        break;

    case OP_XORI:       // xor immediate
        PREAMBLE3;
        rslt = imm ^ b_op;
        POSTAMBLE3;
        break;

    case OP_ANDI:       // and immediate
        PREAMBLE3;
        rslt = imm & b_op;
        POSTAMBLE3;
        break;

    case OP_AI:         // binary add immediate
        PREAMBLE3;
        rslt = imm + b_op;
        // manual says carry is set, but if I do, diags fail
        //SET_CARRY(rslt);
        POSTAMBLE3;
        break;

    case OP_DACI:       // decimal add immediate w/ carry
        PREAMBLE3;
        rslt = decimal_add8(imm, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        POSTAMBLE3;
        break;

    case OP_DSCI:       // decimal subtract immediate w/ carry
        PREAMBLE3;
        rslt = decimal_sub8(imm, b_op, CARRY_BIT);
        SET_CARRY(rslt);
        POSTAMBLE3;
        break;

    case OP_ACI:        // binary add immediate w/ carry
        PREAMBLE3;
        rslt = imm + b_op + CARRY_BIT;
        SET_CARRY(rslt);
        POSTAMBLE3;
        break;

    case OP_MI:         // binary multiply immediate w/ carry
        PREAMBLE3;
        imm  = (uop >> 4) & 0xF;
        b_op = get_Hb(uop >> 15, b_op);
        rslt = imm * b_op;
        POSTAMBLE3;
        break;

#define PREAMBLE4                       \
        imm  = (uop >> 4) & 0xF;        \
        b_op = get_Hb(uop >> 18, b_op)

    case OP_BT:         // branch if true
        PREAMBLE4;
        if ((b_op & imm) == imm) m_cpu.ic = puop->p16;
                            else m_cpu.ic++;
        break;

    case OP_BF:         // branch if false
        PREAMBLE4;
        if ((b_op & imm) == 0) m_cpu.ic = puop->p16;
                          else m_cpu.ic++;
        break;

    case OP_BEQ:        // branch if = to mask
        PREAMBLE4;
        if (b_op == imm) m_cpu.ic = puop->p16;
                    else m_cpu.ic++;
        break;

    case OP_BNE:        // branch if != to mask
        PREAMBLE4;
        if (b_op != imm) m_cpu.ic = puop->p16;
                    else m_cpu.ic++;
        break;

    case OP_BLR:        // BLR: branch if R[AAAA] < R[BBBB]
        m_cpu.pc = (uint16)(m_cpu.pc + (int8)(puop->p8));
        if (a_op < b_op) m_cpu.ic = puop->p16;
                    else m_cpu.ic++;
        break;

    case OP_BLRX:       // BLRX: branch if R[AAAA] < R[BBBB]
        a_op = (a_op2<<8) | a_op;
        b_op = (b_op2<<8) | b_op;
        if (a_op < b_op) m_cpu.ic = puop->p16;
                    else m_cpu.ic++;
        ticks = 8;      // 8 ticks (800 ns)
        break;

    case OP_BLER:       // BLER: branch if R[AAAA] <= R[BBBB]
        m_cpu.pc = (uint16)(m_cpu.pc + (int8)(puop->p8));
        if (a_op <= b_op) m_cpu.ic = puop->p16;
                     else m_cpu.ic++;
        break;

    case OP_BLERX:      // BLERX: branch if R[AAAA] <= R[BBBB]
        a_op = (a_op2<<8) | a_op;
        b_op = (b_op2<<8) | b_op;
        if (a_op <= b_op) m_cpu.ic = puop->p16;
                     else m_cpu.ic++;
        ticks = 8;      // 8 ticks (800 ns)
        break;

    case OP_BER:        // BEQ: branch if R[AAAA] == R[BBBB]
        if (a_op == b_op) m_cpu.ic = puop->p16;
                     else m_cpu.ic++;
        m_cpu.pc = (uint16)(m_cpu.pc + (int8)(puop->p8));
        break;

    case OP_BNR:        // BNE: branch if R[AAAA] != R[BBBB]
        if (a_op != b_op) m_cpu.ic = puop->p16;
                     else m_cpu.ic++;
        m_cpu.pc = (uint16)(m_cpu.pc + (int8)(puop->p8));
        break;

    case OP_SB:         // subroutine call
        m_cpu.icstack[m_cpu.icsp] = (uint16)(m_cpu.ic + 1);
        DEC_ICSP;
        m_cpu.ic = puop->p16;
        break;

    case OP_B:          // unconditional branch
        m_cpu.ic = puop->p16;
        break;

    default:
        assert(false);
        break;

    } // op

    // at this point we know how long each instruction is
    m_scheduler->TimerTick(ticks);  // tell the scheduler that time has passed
    return ticks;
}


// =======================================================
// externally visible CPU module interface
// =======================================================

// constructor
// ramsize should be a multiple of 4.
// subtype *must* be 2200VP, at least presently
Cpu2200vp::Cpu2200vp(System2200 *const sys,
                     std::shared_ptr<Scheduler> scheduler,
                     int ramsize, int cpu_subtype) :
    Cpu2200(),  // init base class
    m_sys(sys),
    m_scheduler(scheduler),
    m_tmr_30ms(nullptr),
    m_memsize_KB(ramsize),
    m_dbg(false)
{
    assert(cpu_subtype == CPUTYPE_2200VP);
    cpu_subtype = cpu_subtype;  // suppress 'unused' warning

    assert(ramsize ==  32 || ramsize == 64 || ramsize == 128 ||
           ramsize == 256 || ramsize == 512 );
    assert((ramsize&0xF) == 0);         // multiple of 15

    // init microcode
    for(int i=0; i<MAX_RAM; i++) {
        write_ucode((uint16)i, 0);
    }
    for(int i=0; i<1024; i++) {
        write_ucode((uint16)(0x8000+i), ucode_2200vp[i]);
    }

#if 0
    // disassemble boot ROM
    {
        char buff[200];
        uint16 pc;
        for(pc=0x8000; pc<0x8400; pc++) {
            (void)dasm_one_vp(buff, pc, m_ucode[pc].ucode);
            dbglog(buff);
        }
    }
#endif

    reset(true);
}


// free any allocated resources at the end of time
Cpu2200vp::~Cpu2200vp()
{
    // ...
}


// report CPU type
int
Cpu2200vp::getCpuType() const
{
    return CPUTYPE_2200VP;
}


// report how much memory the CPU has, in KB
int
Cpu2200vp::getRamSize() const
{
    return m_memsize_KB;
}


// true=hard reset, false=soft reset
void
Cpu2200vp::reset(bool hard_reset)
{
    m_cpu.ic   = (uint16)((hard_reset) ? TRAP_POWER : TRAP_RESET);
    m_cpu.icsp = STACKSIZE-1;

    if (hard_reset) {
        int i;
        for(i=0; i<(m_memsize_KB<<10); i++) {
            m_RAM[i] = 0xFF;
        }
#if 0
        m_cpu.pc = 0;
        m_cpu.orig_pc;
        for(i=0; i<32; i++) {
            m_cpu.aux[32];
        }
        for(i=0; i<8; i++) {
            m_cpu.reg[i];
        }
        for(i=0; i<STACKSIZE; i++) {
            m_cpu.icstack[i] = 0;
        }
        m_cpu.ch = 0;
        m_cpu.cl = 0;
        m_cpu.k = 0;
        m_cpu.ab = 0;
        m_cpu.ab_sel = 0;
#endif
        set_sl(0);  // make sure bank select is 0
        m_cpu.sh &= ~0x80;        // only SH7 one bit is affected

        // actually, the one-shot isn't reset, but let's be safe
        m_cpu.sh &= ~SH_MASK_30MS;
        m_tmr_30ms = nullptr;
    }

    m_status = CPU_RUNNING;
}


// run for ticks*100ns
void
Cpu2200vp::run(int ticks)
{
    int op_ticks = 0;

    do {
#ifdef _DEBUG
        static int m_num_ops = 0;
        if (g_dbg_trace) {
            char buff[200];
            int illegal;
            if (++m_num_ops >= 0*127000) {
                dump_state( 1 );
                illegal = dasm_one_vp(buff, m_cpu.ic, m_ucode[m_cpu.ic].ucode);
                dbglog("cycle %5d: %s", m_num_ops, buff);
                if (illegal) {
                    break;
                }
            }
        }
#endif

        op_ticks = exec_one_op();
        ticks -= op_ticks;

    } while (ticks > 0);

    m_status = (op_ticks == EXEC_ERR) ? CPU_HALTED : CPU_RUNNING;
}


// this function is called by a device to return requested data.
// in the real hardware, the selected IO device drives the IBS signal active
// for 7 uS via a one-shot.  In the emulator, the strobe is effectively
// instantaneous.
void
Cpu2200vp::IoCardCbIbs(int data)
{
    // we shouldn't receive an IBS while the cpu is busy
    assert( (m_cpu.sh & SH_MASK_CPB) == 0 );
    m_cpu.k = (uint8)(data & 0xFF);
    m_cpu.sh |= SH_MASK_CPB;            // CPU busy; inhibit IBS
    m_sys->cpu_CPB( true );             // we are busy now

    // return special status if it is a special function key
    if (data & IoCardKeyboard::KEYCODE_SF) {
        m_cpu.sh |= SH_MASK_SF;         // special function key
    }
}


// when a card is selected, or its status changes, it uses this function
// to notify the core emulator about the new status.
// I'm not sure what the names should be in general, but these are based
// on what the keyboard uses them for.
void
Cpu2200vp::halt()
{
    // set the halt/step key notification
    m_cpu.sh = (uint8)(m_cpu.sh | SH_MASK_HALT);
}


// this signal is called by the currently active I/O card
// when its busy/ready status changes.  If no card is selected,
// it floats to one (it is an open collector bus signal).
void
Cpu2200vp::setDevRdy(bool ready)
{
    m_cpu.sh = (uint8)
             ( (ready) ? (m_cpu.sh |  SH_MASK_DEVRDY)      /* set */
                       : (m_cpu.sh & ~SH_MASK_DEVRDY) );   /* clear */
}


// the disk controller is odd in that it uses the AB bus to signal some
// information after the card has been selected.  this lets it peek into
// that part of the cpu state.
uint8
Cpu2200vp::getAB() const
{
    return m_cpu.ab;
}


// this callback occurs when the 30 ms timeslicing one-shot times out.
void
Cpu2200vp::tcb30msDone()
{
    m_cpu.sh &= ~SH_MASK_30MS;    // one shot output falls
    m_tmr_30ms = nullptr;         // dead timer
}


// ------------------------------------------------------------------------
//  misc utilities
// ------------------------------------------------------------------------

#if HAVE_FILE_DUMP
#include <fstream>
#include <iomanip>

void
Cpu2200vp::dump_ram(const std::string &filename)
{
    // open the file, discarding contents of file if it already exists
    std::ofstream ofs( filename.c_str(), std::ofstream::out | std::ofstream::trunc);
    if (!ofs.is_open()) {
        UI_Error("Error writing to file '%s'", filename.c_str());
        return;
    }

    ofs.fill('0');
    for(int addr=0; addr < (m_memsize_KB<<10); addr += 16) {
        ofs << std::setw(4) << std::hex << std::uppercase << addr << ":";
        for(int i=0; i<16; i++) {
            ofs << " " << std::setw(2) << std::hex << std::uppercase << int(m_RAM[addr+i]);
        }
        ofs << std::endl;
    }

    ofs << std::endl << std::endl;
    ofs << "===============================================" << std::endl << std::endl;
    for(int addr=0; addr<0x8000; addr++) {
        char buff[200];
        (void)dasm_one_vp(buff, addr, m_ucode[addr].ucode);
        ofs << buff;
    }

    ofs.close();
}
#endif


#ifdef _DEBUG
// dump the most important contents of the uP state
void
Cpu2200vp::dump_state(int fulldump)
{
    if (fulldump) {
        dbglog("---------------------------------------------\n");
    }

    dbglog(" K SH SL CH CL PH PL F7 F6 F5 F4 F3 F2 F1 F0\n");
    dbglog("%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            m_cpu.k, m_cpu.sh, m_cpu.sl, m_cpu.ch, m_cpu.cl,
            (m_cpu.pc >> 8) & 0xFF, (m_cpu.pc >> 0) & 0xFF,
            m_cpu.reg[7], m_cpu.reg[6], m_cpu.reg[5], m_cpu.reg[4],
            m_cpu.reg[3], m_cpu.reg[2], m_cpu.reg[1], m_cpu.reg[0]);
    dbglog("    AB=%02X, AB_SEL=%02X, cy=%d\n",
            m_cpu.ab, m_cpu.ab_sel, CARRY_BIT);

    if (!fulldump) {
        return;
    }

    dbglog("AUX 00-07   %04X %04X %04X %04X %04X %04X %04X %04X\n",
            m_cpu.aux[0], m_cpu.aux[1], m_cpu.aux[2], m_cpu.aux[3],
            m_cpu.aux[4], m_cpu.aux[5], m_cpu.aux[6], m_cpu.aux[7]);
    dbglog("AUX 08-0F   %04X %04X %04X %04X %04X %04X %04X %04X\n",
            m_cpu.aux[8], m_cpu.aux[9], m_cpu.aux[10], m_cpu.aux[11],
            m_cpu.aux[12], m_cpu.aux[13], m_cpu.aux[14], m_cpu.aux[15]);
    dbglog("AUX 10-17   %04X %04X %04X %04X %04X %04X %04X %04X\n",
            m_cpu.aux[16], m_cpu.aux[17], m_cpu.aux[18], m_cpu.aux[19],
            m_cpu.aux[20], m_cpu.aux[21], m_cpu.aux[22], m_cpu.aux[23]);
    dbglog("AUX 18-1F   %04X %04X %04X %04X %04X %04X %04X %04X\n",
            m_cpu.aux[24], m_cpu.aux[25], m_cpu.aux[26], m_cpu.aux[27],
            m_cpu.aux[28], m_cpu.aux[29], m_cpu.aux[30], m_cpu.aux[31]);

    dbglog("stack depth=%d\n", STACKSIZE-1 - m_cpu.icsp);
    if (m_cpu.icsp < STACKSIZE-1) {
        int num = STACKSIZE-1 - m_cpu.icsp;
        int todo = (num > 6) ? 6 : num;
        int i;
        dbglog("    recent: ");
        for(i=STACKSIZE-1; i>STACKSIZE-1-todo; i--) {
            dbglog("%04X ", m_cpu.icstack[i]);
        }
        dbglog("\n");
    }
    dbglog("---------------------------------------------\n");
}
#endif

// vim: ts=8:et:sw=4:smarttab
