// Wang 2200-VP microcode disassembler.

#include "w2200.h"

#include <fstream>

// ======================================================================
// compile time options
// ======================================================================

// define as 1 to dasm some special cases
#define DASM_PSEUDO_OPS 1

// define as 1 to use relative branch addresses when nearby (*+1)
#define DASM_REL_BRANCH 1

// which column the operation parameters begin
#define PARAM_COL 10

// which column the comments begin
#define COMMENT_COL 32

// ======================================================================
// data structures, constants, macros
// ======================================================================

static char *a_regs[] = {
        "F0", "F1", "F2", "F3",
        "F4", "F5", "F6", "F7",
        "CL-", "CH-", "CL", "CH",
        "CL+", "CH+", "+", "-"
        };

static char *ax_regs[] = {
        "F1F0", "F2F1", "F3F2", "F4F3",
        "F5F4", "F6F5", "F7F6", "CLF7",
        "CHCL", "CLCH", "CHCL", "CLCH",
        "CHCL", "DCH",  "DD",   "F0D"
        };

static char *bc_regs[] = {
        "F0", "F1", "F2", "F3",
        "F4", "F5", "F6", "F7",
        "PL", "PH", "CL", "CH",
        "SL", "SH", "K",  ""
        };

static char c_illegal_regs[] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 1, 1,
        0, 0, 0, 0
        };

static char *bcx_regs[] = {
        "F1F0", "F2F1", "F3F2", "F4F3",
        "F5F4", "F6F5", "F7F6", "PLF7",
        "PHPL", "CLPH", "CHCL", "SLCH",
        "SHSL", "KSH",  "DK",   "F0D"
        };

static char cx_illegal_regs[] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 1, 1, 1,
        0, 0, 0, 0
        };

#define noXbit (~(1<<17))

// 10b page branch target address
#define PAGEBR(ic,uop) static_cast<uint16>(((ic) & 0xFC00) | (((uop) >> 8) & 0x03FF))
#define FULLBR(uop)    static_cast<uint16>((((uop) >> 8) & 0x03FF) | (((uop) << 8) & 0xFC00))

// 8b immediate
#define IMM8(uop) ((((uop) >> 10) & 0xF0) | (((uop) >> 4) & 0xF))


// ======================================================================
// code
// ======================================================================

// fill out the buffer to the specified number of spaces
static int
pad_spaces(char *buf, int idx, int limit) noexcept
{
    assert(buf != nullptr);
    int off = idx;
    while (off < limit) {
        buf[off] = ' ';
        off++;
    }
    buf[off] = '\0';
    return off-idx;
}


// stuff in a hex value at the buffer location "offset",
// updating offset in the process, 'digits' long.
// if the number starts with A-F, precede it with a bonus 0.
static int
dasm_hex(char *buf, int value, int digits) noexcept
{
    assert(buf != nullptr);

    // do we need a leading zero?
    const int first_dig = (value >> 4*(digits-1)) & 0xF;
    if (first_dig >= 10) {
        digits++;
    }

    char fmtstr[16];
    sprintf(&fmtstr[0], "%%0%dX", digits);
    sprintf(buf, &fmtstr[0], value);

    return digits;
}


// stuff in the target address, in hex.  however, if the address
// is very near the current address, list it as relative.
static int
dasm_addr(char *buf, int cur_pc, int new_pc) noexcept
{
    assert(buf != nullptr);
#if DASM_REL_BRANCH
    const int window = 2;  // how big of a window to use relative addr
    const int diff = new_pc - cur_pc;

    int len = 0;
    if (diff == 0) {
        len = sprintf(buf, "*");
    } else if (diff > 0 && diff <= window) {
        len = sprintf(buf, "*+%d", diff);
    } else if (diff < 0 && diff >= -window) {
        len = sprintf(buf, "*%d", diff);
    } else {
#endif
        len = dasm_hex(buf, new_pc, 4);
    }

    return len;
}


// disassemble the A input bus field
// return the number of bytes printed out
static int
dasm_a_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int xbit  = ((uop >> 17) & 0x1);
    const int field = ((uop >>  4) & 0xF);
    const char *str = (xbit) ? ax_regs[field]
                             : a_regs[field];
    strcpy(buf, str);
    return strlen(str);
}


// disassemble the B input bus field
// return the number of bytes printed out
static int
dasm_b_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int xbit  = ((uop >> 17) & 0x1);
    const int field = ((uop >>  0) & 0xF);
    const char *str = (xbit) ? bcx_regs[field]
                             : bc_regs[field];
    strcpy(buf, str);
    return strlen(str);
}


// disassemble the C output bus field
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasm_c_field(char *buf, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int xbit  = ((uop >> 17) & 0x1);
    const int field = ((uop >>  8) & 0xF);
    const char *str = (xbit) ? bcx_regs[field]
                             : bc_regs[field];
    illegal = (xbit) ? cx_illegal_regs[field]
                     : c_illegal_regs[field];

    if (illegal) {
        strcpy(buf, "???");
        return 3;
    } else {
        strcpy(buf, str);
        return strlen(str);
    }
}


// disassemble the I input bus field
// return the number of bytes printed out
static int
dasm_i4_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int field = (uop >> 4) & 0xF;
    int len = dasm_hex(buf, field, 1);
    return len;
}


// disassemble the I input bus field
// return the number of bytes printed out
static int
dasm_i8_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int field = IMM8(uop);
    int len = dasm_hex(buf, field, 2);
    return len;
}


// disassemble the immediate 5b aux address
// return the number of bytes printed out
static int
dasm_ai5_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int field = (uop >> 4) & 0x1F;
    return sprintf(buf, "%02X", field);
}


// disassemble the PC increment value
// return the number of bytes printed out
static int
dasm_pcinc_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int minus = (uop >> 14) & 1;
    const int count = (uop >>  9) & 3;
    if (count > 0) {
        *buf++ = (char)((minus) ? '-' : '+');
        *buf++ = (char)('0' + count);
        *buf = '\0';
        return 2;
    }
    return 0;
}


// disassemble the memory access field.
// return the number of bytes printed out
static int
dasm_dd_field(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int dd_field = ((uop >> 12) & 0x3);

    char *str;
    switch (dd_field) {
         case 0: str = "";    break;
         case 1: str = ",R";  break;
         case 2: str = ",W1"; break;
        default: str = ",W2"; break;
    }

    strcpy(buf, str);
    return strlen(str);
}


// disassemble the carry bit manipulation field.
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_cy_field(char *buf, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int cy_field = ((uop >> 14) & 0x3);
    illegal = false;

    char *str;
    switch (cy_field) {
          case 0: return 0;
          case 1: str = ",x"; illegal = true; break;
          case 2: str = ",0"; break;
         default: str = ",1"; break;
    }

    strcpy(buf, str);
    return 2;
}


// disassemble ALU op reg to reg
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_type1(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int x_field = (uop >> 17) & 1;
    bool bad1, bad2;

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    if (x_field) {
        buf[len++] = 'X'; buf[len] = '\0';
    }
    len += dasm_dd_field(&buf[len], uop);        // ,R/,W1/,W2
    len += dasm_cy_field(&buf[len], bad1, uop);  // ,0/,1
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_a_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], bad2, uop);

    illegal = bad1 | bad2;
    return len;
}


// disassemble special case: ORX DD,FwFx,FyFz == MVX FwFx,FyFz
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_type1a(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int x_field = (uop >> 17) & 1;
    bool bad1, bad2;

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    if (x_field) {
        buf[len++] = 'X'; buf[len] = '\0';
    }
    len += dasm_dd_field(&buf[len], uop);        // ,R/,W1/,W2
    len += dasm_cy_field(&buf[len], bad1, uop);  // ,0/,1
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_b_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], bad2, uop);

    illegal = bad1 | bad2;
    return len;
}


// disassemble SHFT
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_typeSHFT(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int Ha = ((uop >> 18) & 1);
    const int Hb = ((uop >> 19) & 1);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    buf[len++] = (char)(Hb ? 'H' : 'L');
    buf[len++] = (char)(Ha ? 'H' : 'L');
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_a_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, uop);

    return len;
}


// disassemble M
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_typeM(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int Ha = ((uop >> 14) & 1);
    const int Hb = ((uop >> 15) & 1);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    buf[len++] = (char)(Hb ? 'H' : 'L');
    buf[len++] = (char)(Ha ? 'H' : 'L');
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_a_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, uop);

    return len;
}


// disassemble ALU op with immediate
// return the number of bytes printed out
static int
dasm_type2(char *buf, const char *mnemonic, bool &illegal, uint32 uop)
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_i8_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], (uop & noXbit));
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, (uop & noXbit));

    return len;
}


// disassemble special case: ORI n,,Fy == MVI n,Fy
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_type2a(char *buf, const char *mnemonic, bool &illegal, uint32 uop)
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_dd_field(&buf[len], uop);       // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_i8_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, (uop & noXbit));

    return len;
}


// disassemble special case: ORI 00,Fx,Fy == MV Fx,Fy
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_type2b(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_b_field(&buf[len], (uop & noXbit));
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, (uop & noXbit));

    return len;
}


// disassemble MI
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasm_typeMI(char *buf, const char *mnemonic, bool &illegal, uint32 uop)
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int Hb = ((uop >> 15) & 1);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    buf[len++] = (char)(Hb ? 'H' : 'L');
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_i4_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], (uop & noXbit));
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_c_field(&buf[len], illegal, (uop & noXbit));

    return len;
}


// disassemble conditional branch, reg to reg compare
// return the number of bytes printed out
static int
dasm_type3(char *buf, const char *mnemonic, uint32 ic, uint32 uop)
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const uint32 newic = PAGEBR(ic,uop);
    const int x_field = (uop >> 18) & 1;
    // move the X bit from bit 18 to bit 17 for A and B field disassembly
    const uint32 munged_uop = (uop & noXbit) | (x_field << 17);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    if (x_field) {
        buf[len++] = 'X';
    }
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_a_field(&buf[len], munged_uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], munged_uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_addr(&buf[len], ic, newic);

    return len;
}


static int
dasm_SH_bitfield(char *buf, uint8 bits) noexcept
{
    assert(buf != nullptr);

    bool many = false;
    int len = 0;

    for(int i=0; i<8; i++) {
        if (bits & (1 << i)) {
            if (many) {
                strcpy(&buf[len], ", ");
                len += 2;
            }
            many = true;
            char *str;
            switch(i) {
                 case 0: str = "carry";      break;
                 case 1: str = "CRB";        break;
                 case 2: str = "KFN";        break;
                 case 3: str = "RB";         break;
                 case 4: str = "30ms timer"; break;
                 case 5: str = "halt";       break;
                 case 6: str = "parity";     break;
                default: str = "parity en";  break;
            }
            strcpy(&buf[len], str);
            len += strlen(str);
        }
    }

    return len;
}


// disassemble conditional branch, reg vs immediate
// return the number of bytes printed out
static int
dasm_type4(char *buf, const char *mnemonic, uint32 ic, uint32 uop)
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const uint32 newic = PAGEBR(ic, uop);
    const int Hb = ((uop >> 18) & 1);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    buf[len++] = (char)((Hb) ? 'H' : 'L');
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_i4_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], (uop & noXbit));
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_addr(&buf[len], ic, newic);

    if ((uop & 0x70000F) == 0x60000D) {         // BT or BF of SH
        uint8 bitfield = static_cast<uint8>(uop & 0xF0);
        bitfield >>= (Hb) ? 0 : 4;
        len += pad_spaces(buf, len, COMMENT_COL);
        len += sprintf(&buf[len], "; testing: ");
        len += dasm_SH_bitfield(&buf[len], bitfield);
    }

    return len;
}


// disassemble TAP instructions
// return the number of bytes printed out
static int
dasm_type5(char *buf, const char *mnemonic, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_ai5_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], (uop & noXbit));

    return len;
}


// disassemble TPA/XPA/TPS instructions
// return the number of bytes printed out
static int
dasm_type6(char *buf, const char *mnemonic, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_pcinc_field(&buf[len], uop);            // +/- 0,1,2,3
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_ai5_field(&buf[len], uop);
    buf[len++] = ','; buf[len] = '\0';
    len += dasm_b_field(&buf[len], (uop & noXbit));

    return len;
}


// disassemble TSP instructions
// disassemble SR/SR,RCM/SR,WCM instructions
// return the number of bytes printed out
static int
dasm_type7(char *buf, const char *mnemonic, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    strcpy(buf, mnemonic);
    int len = strlen(buf);
    len += dasm_dd_field(&buf[len], uop);               // ,R/,W1/,W2
    len += pad_spaces(buf, len, PARAM_COL);

    len += dasm_b_field(&buf[len], (uop & noXbit));

    return len;
}


// disassemble one microinstruction into static buffer
// return false if OK, true if error.
bool
dasm_op_vp(char *buf, uint16 ic, uint32 uop)
{
    assert(buf != nullptr);

    const bool lpi_op  = ((uop & 0x790000) == 0x190000);
    const bool mini_op = ((uop & 0x618000) == 0x018000);
    const bool shft_op = ((uop & 0x71C000) == 0x004000);
    uint16 newic;

    int parity = 0;
    for(int bit=0; bit<24; bit++) {
        parity ^= (uop >> bit) & 1;
    }

    // primary instruction decode
    int len = 0;
    bool illegal = false;  // default
    if (lpi_op) {

        const uint32 addr = ((uop >> 3) & 0xC000)     // [18:17] -> [15:14]
                          | ((uop >> 2) & 0x3000)     // [15:14] -> [13:12]
                          | ((uop >> 0) & 0x0FFF);    // [11: 0] -> [11: 0]
        strcpy(buf, "LPI");
        len = 3;
        len += dasm_dd_field(&buf[len], uop);           // ,R/,W1/,W2
        len += pad_spaces(buf, len, PARAM_COL);
        len += dasm_hex(&buf[len], addr, 4);

    } else if (mini_op) {

        const int op = (uop >> 17) & 0xF;

        switch (op) {
            case 0x5:   // TAP
                illegal = (uop & 0x7F8000) != 0x0B8000;
                len = dasm_type5(buf, "TAP", uop);
                break;
            case 0x0:   // TPA
                illegal = (uop & 0x7F8800) != 0x018000;
                len = dasm_type6(buf, "TPA", uop);
                break;
            case 0x1:   // XPA
                illegal = (uop & 0x7F8800) != 0x038000;
                len = dasm_type6(buf, "XPA", uop);
                break;
            case 0x2:   // TPS
                illegal = (uop & 0x7F8800) != 0x058000;
                len = dasm_type6(buf, "TPS", uop);
                break;
            case 0x6:   // TSP
                illegal = (uop & 0x7F8800) != 0x0D8000;
                len = dasm_type7(buf, "TSP", uop);
                break;
            case 0x3:   // SR (subroutine return)
                if ((uop & 0x7F8E00) == 0x078600) {
                    // SR,RCM (read control memory and subroutine return)
                    len = dasm_type7(buf, "SR,RCM", uop);
                } else if ((uop & 0x7F8E00) == 0x078400) {
                    // SR,WCM (write control memory and subroutine return)
                    len = dasm_type7(buf, "SR,WCM", uop);
                } else if ((uop & 0x7F8C00) == 0x078000) {
                    // SR (subroutine return)
                    len = dasm_type7(buf, "SR", uop);
                } else {
                    len = sprintf(buf, "bad SR");
                    illegal = true;
                }
                break;
            case 0xB:   // CIO (control input/output)
            {
                illegal = (uop & 0x7FB000) != 0x178000;
                const int s_field = (uop >> 11) & 0x1;
                const int t_field = (uop >>  4) & 0x7F;
                strcpy(buf, "CIO");
                len = 3;
                len += pad_spaces(buf, len, PARAM_COL);
                if (s_field) {
                    len += sprintf(&buf[len], "AB=K,");
                }
                switch (t_field) {
                    case 0x40: // ABS
                        strcpy(&buf[len], "ABS");
                        len += 3;
                        break;
                    case 0x20: // OBS
                        strcpy(&buf[len], "OBS");
                        len += 3;
                        break;
                    case 0x10: // CBS
                        strcpy(&buf[len], "CBS");
                        len += 3;
                        break;
                    case 0x08: // SRS (status request strobe)
                        strcpy(&buf[len], "SRS");
                        len += 3;
                    case 0x00: // no strobe
                        break;
                    default:
                        strcpy(&buf[len], "???");
                        len += 3;
                        illegal = true;
                        break;
                }
                break;
            }
            default:
                // illegal, or maybe impossible
                illegal = true;
                break;
        }

    } else if (shft_op) {

        assert( (uop & 0x010000) == 0x000000);
        len = dasm_typeSHFT(buf, "SH", illegal, uop);

    } else { // neither lpi nor mini_op

        const int op = (uop >> 18) & 0x1F;

        switch (op) {

        // register instructions:

            case 0x00:  // OR
                illegal = ((uop & 0x010000) != 0x000000);
                if (DASM_PSEUDO_OPS & ((uop & 0x0200F0) == 0x0200E0)) {
                    // special case: ORX DD,FwFx,FyFz == MVX FwFx,FyFz
                    len = dasm_type1a(buf, "MVX", illegal, uop);
                } else {
                    len = dasm_type1(buf, "OR", illegal, uop);
                }
                break;
            case 0x01:  // XOR
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "XOR", illegal, uop);
                break;
            case 0x02:  // AND
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "AND", illegal, uop);
                break;
            case 0x03:  // subtract w/ carry
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "SC", illegal, uop);
                break;
            case 0x04:  // decimal add w/ carry
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "DAC", illegal, uop);
                break;
            case 0x05:  // decimal subtract w/ carry
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "DSC", illegal, uop);
                break;
            case 0x06:  // binary add w/ carry
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_type1(buf, "AC", illegal, uop);
                break;
            case 0x07:  // multiply
                illegal = ((uop & 0x010000) != 0x000000);
                len = dasm_typeM(buf, "M", illegal, uop);
                break;

            case 0x08:  // or immediate
                if (DASM_PSEUDO_OPS & ((uop & 0xFFFFFF) == 0x200F0F)) {
                    // special case: ORI 0,, == NOOP
                    len = sprintf(buf, "NOP");
                } else if (DASM_PSEUDO_OPS & ((uop & 0x00000F) == 0x00000F)) {
                    // special case: ORI n,,Fy == MVI n,Fy
                    len = dasm_type2a(buf, "MVI", illegal, uop);
                } else if (DASM_PSEUDO_OPS & ((uop & 0x03C0F0) == 0x000000)) {
                    // special case: ORI 00,Fx,Fy == MV Fx,Fy
                    len = dasm_type2b(buf, "MV", illegal, uop);
                } else {
                    len = dasm_type2(buf, "ORI", illegal, uop);
                    if ((uop & 0x000F0F) == 0x000D0D) {
                        // ORI n,SH,SH -- decode bitfields
                        const uint8 bitfield = static_cast<uint8>(IMM8(uop));
                        len += pad_spaces(buf, len, COMMENT_COL);
                        len += sprintf(&buf[len], "; setting: ");
                        len += dasm_SH_bitfield(&buf[len], bitfield);
                    }
                }
                break;
            case 0x09:  // xor immediate
                len = dasm_type2(buf, "XORI", illegal, uop);
                break;
            case 0x0A:  // and immediate
                len = dasm_type2(buf, "ANDI", illegal, uop);
                if ((uop & 0x000F0F) == 0x000D0D) {
                    // ANDI n,SH,SH -- decode bitfields
                    const uint8 bitfield = static_cast<uint8>(~IMM8(uop));
                    len += pad_spaces(buf, len, COMMENT_COL);
                    len += sprintf(&buf[len], "; clearing: ");
                    len += dasm_SH_bitfield(&buf[len], bitfield);
                }
                break;
            case 0x0B:  // binary add immediate
                len = dasm_type2(buf, "AI", illegal, uop);
                break;
            case 0x0C:  // decimal add immediate w/ carry
                len = dasm_type2(buf, "DACI", illegal, uop);
                break;
            case 0x0D:  // decimal subtract immediate w/ carry
                len = dasm_type2(buf, "DSCI", illegal, uop);
                break;
            case 0x0E:  // decimal add immediate w/ carry
                len = dasm_type2(buf, "ACI", illegal, uop);
                break;
            case 0x0F:  // binary multiply immediate
                len = dasm_typeMI(buf, "MI", illegal, uop);
                break;

        // register branch instructions:

            case 0x10: case 0x11:       // branch if R[AAAA] < R[BBBB]
                len = dasm_type3(buf, "BLR", ic, uop);
                break;
            case 0x12: case 0x13:       // branch if R[AAAA] <= R[BBBB]
                len = dasm_type3(buf, "BLER", ic, uop);
                break;
            case 0x14:                  // branch if R[AAAA] == R[BBBB]
                len = dasm_type3(buf, "BER", ic, uop);
                break;
            case 0x16:                  // branch if R[AAAA] != R[BBBB]
                len = dasm_type3(buf, "BNR", ic, uop);
                break;

        // branch instructions:

            case 0x15:  // subroutine branch
                newic = FULLBR(uop);
                strcpy(buf,"SB"); len = 2;
                len += pad_spaces(buf, len, PARAM_COL);
                len += dasm_addr(&buf[len], ic, newic);
                break;
            case 0x17:  // unconditional branch
                newic = FULLBR(uop);
                strcpy(buf,"B"); len = 1;
                len += pad_spaces(buf, len, PARAM_COL);
                len += dasm_addr(&buf[len], ic, newic);
                break;

        // mask branch instructions:

            case 0x18: case 0x19:       // branch if true
                len = dasm_type4(buf, "BT", ic, uop);
                break;
            case 0x1A: case 0x1B:       // branch if false
                len = dasm_type4(buf, "BF", ic, uop);
                break;
            case 0x1C: case 0x1D:       // branch if = to mask
                len = dasm_type4(buf, "BEQ", ic, uop);
                break;
            case 0x1E: case 0x1F:       // branch if != to mask
                len = dasm_type4(buf, "BNE", ic, uop);
                break;

            default: // impossible
                assert(false);
                break;
        }
    }

    if (!parity) {
        len += pad_spaces(buf, len, COMMENT_COL);
        strcpy(&buf[len], "; (bad parity)");
    }
    return illegal;
}


bool
dasm_one_vp(char *buff, uint16 ic, uint32 ucode)
{
    assert(buff != nullptr);
    char dasmtext[100];

    const bool illegal = dasm_op_vp(&dasmtext[0], ic, ucode);
    sprintf(buff, "%04X: %06X : %s%s\n",
            ic, ucode & 0x00FFFFFF, &dasmtext[0],
            illegal ? " (ILLEGAL)" : "");

    return illegal;
}

// vim: ts=8:et:sw=4:smarttab
