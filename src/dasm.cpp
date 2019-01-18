// Wang 2200-T microcode disassembler.

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
#define PARAM_COL  8

// which column the comments begin
#define COMMENT_COL 26

// ======================================================================
// the rest
// ======================================================================

// fill out the buffer to the specified number of spaces.
static int
padSpaces(char *buf, int idx, int limit) noexcept
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
dasmHex(char *buf, int value, int digits) noexcept
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
dasmAddr(char *buf, int cur_pc, int new_pc) noexcept
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
        len = dasmHex(buf, new_pc, 4);
    }

    return len;
}


// disassemble the A input bus field.
// return the number of bytes printed out.
// adj is an output field, and is set to true if the PC value is modified.
static int
dasmAField(char *buf, uint32 uop, bool &adj) noexcept
{
    assert(buf != nullptr);
    const int field = ((uop >> 4) & 0xF);

    if (field < 8) {
        return sprintf(buf, "F%d", field);
    }

    const char *str = nullptr;
    int pc = 0;
    switch (field) {
        case  8: str = "CH";  pc = 0; break;
        case  9: str = "CH-"; pc = 1; break;
        case 10: str = "CH+"; pc = 1; break;
        case 11: str = "-";   pc = 1; break;
        case 12: str = "CL";  pc = 0; break;
        case 13: str = "CL-"; pc = 1; break;
        case 14: str = "CL+"; pc = 1; break;
        case 15: str = "+";   pc = 1; break;
        default: str = "";    pc = 0;
            assert(false);
            break;
    }

    // indicate if the PC would get adjusted
    adj |= (pc != 0);

    return sprintf(buf, str);
}


// disassemble the I input bus field
// return the number of bytes printed out
static int
dasmIField(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const int field = ((uop >> 4) & 0xF);
    const int len = dasmHex(buf, field, 1);
    return len;
}


// disassemble the B input bus field
// return the number of bytes printed out
static int
dasmBField(char *buf, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const bool xbit = ((uop >> 14) & 0x1) != 0x0;
    const int field = ((uop >> 10) & 0xF);

    if (field < 8) {
        return sprintf(buf, "F%d", field);
    }

    const char *str = nullptr;
    if (xbit) {
        switch (field) {
            case  8: str = "ST3"; break;
            case  9: str = "ST4"; break;
            case 10: str = "PC2"; break;
            case 11: str = "PC3"; break;
            case 12: str = "PC4"; break;
            case 13: str = "CH";  break;
            case 14: str = "CL";  break;
            case 15: str = "0";   break;
            default: str = nullptr; assert(false); break;
        }
    } else {
        switch (field) {
            case  8: str = "KH";  break;
            case  9: str = "KL";  break;
            case 10: str = "ST1"; break;
            case 11: str = "ST2"; break;
            case 12: str = "PC1"; break;
            case 13: str = "CH";  break;
            case 14: str = "CL";  break;
            case 15: str = "0";   break;
            default: str = nullptr; assert(false); break;
        }
    }

    return sprintf(buf, str);
}


// disassemble the C output bus field
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasmCField(char *buf, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    const bool xbit = ((uop >> 14) & 0x1) != 0x0;
    const int field = ((uop >>  0) & 0xF);

    if (field < 8) {
        illegal = false;
        return sprintf(buf, "F%d", field);
    }

    const char *str = nullptr;
    if (xbit) {
        switch (field) {
            case  8: str = "ST3";   break;
            case  9: str = "ST4";   break;
            case 10: str = "PC2";   break;
            case 11: str = "PC3";   break;
            case 12: str = "PC4";   break;
            case 13: str = nullptr; break;
            case 14: str = nullptr; break;
            case 15: str = "";      break;        // dummy destination
            default: str = nullptr; assert(false); break;
        }
    } else {
        switch (field) {
            case  8: str = "KH";    break;
            case  9: str = "KL";    break;
            case 10: str = "ST1";   break;
            case 11: str = "ST2";   break;
            case 12: str = "PC1";   break;
            case 13: str = nullptr; break;
            case 14: str = nullptr; break;
            case 15: str = "";      break;        // dummy destination
            default: str = nullptr; assert(false); break;
        }
    }

    if (str == nullptr) {
        illegal = true;
        return sprintf(buf, "???");
    }
    illegal = false;
    if (str[0] != '\0') {
        return sprintf(buf, "%s", str);
    }
    return 0; // no dest
}


// disassemble the M field.
// this is used by the mini ops, and may be a read or write.
// if it is a read, the data goes into the C data read register.
// if it is a write, the data comes from whatever is on the A bus.
// return the number of bytes printed out
static int
dasmMField(char *buf, uint32 uop) noexcept
{
    const int m_field = ((uop >> 8) & 0x3);

    switch (m_field) {
         case 0: return 0;
         case 1: return sprintf(buf, ",R");
         case 2: return sprintf(buf, ",W1");
         case 3: return sprintf(buf, ",W2");
        default: assert(false); return 0;
    }
}


static void
dasmSt1Bitfield(char *buf, int *len, uint4 bits) noexcept
{
    assert(buf != nullptr);
    assert(len != nullptr);

    bool many = false;
    int p = *len;

    for (int i=0; i < 4; i++) {
        if ((bits & (1 << i)) != 0) {
            if (many) {
                strcpy(&buf[p], ", ");
                p += 2;
            }
            many = true;
            const char *str = nullptr;
            switch (i) {
                 case 0: str = "carry";      break;
                 case 1: str = "CPB";        break;
                 case 2: str = "KFN";        break;
                default: str = "!RAM/ROM";   break;
            }
            strcpy(&buf[p], str);
            p += strlen(str);
        }
    }

    *len = p;
}


static void
dasmSt3Bitfield(char *buf, int *len, uint4 bits) noexcept
{
    assert(buf != nullptr);
    assert(len != nullptr);

    bool many = false;
    int p = *len;

    for (int i=0; i < 4; i++) {
        if ((bits & (1 << i)) != 0) {
            if (many) {
                strcpy(&buf[p], ", ");
                p += 2;
            }
            many = true;
            const char *str = nullptr;
            switch (i) {
                 case 0: str = "ready/!busy"; break;
                 case 1: str = "IB5";         break;
                 case 2: str = "HALT";        break;
                default: str = "!vert/horiz"; break;
            }
            strcpy(&buf[p], str);
            p += strlen(str);
        }
    }

    *len = p;
}


// disassemble ALU op reg to reg
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasmType1(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);
    bool bad = false;

    const int b_field = (uop >> 10) & 0xF;
    const bool mov = DASM_PSEUDO_OPS
                  && (strcmp(mnemonic, "OR") != 0)
                  && (b_field == 0xF); // OR a,0,c  --> MV a,c
    const char *mnem = (mov) ? "MV" : mnemonic;

    int len = sprintf(buf, "%s", mnem);
    len += dasmMField(&buf[len], uop);
    len += padSpaces(buf, len, PARAM_COL);

    bool dummy = false;
    len += dasmAField(&buf[len], uop, dummy);
    if (!mov) {
        len += sprintf(&buf[len], ",");
        len += dasmBField(&buf[len], uop);
    }
    len += sprintf(&buf[len], ",");
    len += dasmCField(&buf[len], bad, uop);

    illegal = bad;
    return len;
}


// disassemble ALU op with immediate
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasmType2(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);
    bool bad = false;

    const int i_field = (uop >>  4) & 0xF;
    const int b_field = (uop >> 10) & 0xF;
    const int c_field = (uop >>  0) & 0xF;
    const bool x_field = ((uop >> 14) & 0x1) != 0x0;
    const bool b_st1 = (!x_field && (b_field == 0xA)); // B is ST1
    const bool b_st3 = ( x_field && (b_field == 0x8)); // B is ST3
    const bool c_st1 = (!x_field && (c_field == 0xA)); // C is ST1
    const bool c_st3 = ( x_field && (c_field == 0x8)); // C is ST3
    const bool ori  = (strcmp(mnemonic, "ORI") != 0);
    const bool andi = (strcmp(mnemonic, "ANDI") != 0);
    const bool mvi = DASM_PSEUDO_OPS
                  && ori && (b_field == 0xF);  // ORI imm,0,c  --> MVI imm,c
    const bool mov = DASM_PSEUDO_OPS && !mvi
                  && ori && (i_field == 0x0);  // ORI 0,b,c    --> MV b,c
    const char *mnem = (mvi) ? "MVI"
                     : (mov) ? "MV"
                             : mnemonic;

    int len = sprintf(buf, "%s", mnem);
    len += dasmMField(&buf[len], uop);
    len += padSpaces(buf, len, PARAM_COL);

    if (!mov) {
        len += dasmIField(&buf[len], uop);
    }
    if (!mov && !mvi) {
        len += sprintf(&buf[len], ",");
    }
    if (!mvi) {
        len += dasmBField(&buf[len], uop);
    }
    len += sprintf(&buf[len], ",");
    len += dasmCField(&buf[len], bad, uop);

    if (ori && ((b_st1 && c_st1) || (b_st3 && c_st3))) {
        // ORI #,ST1,ST1 = setting ST1 flag bit(s)
        // ORI #,ST3,ST3 = setting ST3 flag bit(s)
        const uint4 bitfield = static_cast<uint4>(i_field);
        len += padSpaces(buf, len, COMMENT_COL);
        len += sprintf(&buf[len], "; setting: ");
        ((b_st1) ? dasmSt1Bitfield : dasmSt3Bitfield) (buf, &len, bitfield);
    }
    if (andi && ((b_st1 && c_st1) || (b_st3 && c_st3))) {
        // ANDI #,ST1,ST1 = clearing ST1 flag bit(s)
        // ANDI #,ST3,ST3 = clearing ST3 flag bit(s)
        const uint4 bitfield = static_cast<uint4>(~i_field & 0xF);
        len += padSpaces(buf, len, COMMENT_COL);
        len += sprintf(&buf[len], "; clearing: ");
        ((b_st1) ? dasmSt1Bitfield : dasmSt3Bitfield) (buf, &len, bitfield);
    }
    if (mvi && (c_st1 || c_st3)) {
        const uint4 bitfield = static_cast<uint4>(i_field);
        len += padSpaces(buf, len, COMMENT_COL);
        len += sprintf(&buf[len], "; setting: ");
        ((c_st1) ? dasmSt1Bitfield : dasmSt3Bitfield) (buf, &len, bitfield);
    }

    illegal = bad;
    return len;
}


// disassemble conditional branch, reg to reg compare
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasmType3(char *buf, const char *mnemonic, bool &illegal, uint32 ic, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const uint32 new_ic   = (ic & 0xFF00) | ((uop >> 4) & 0xF0) | (uop & 0x0F);
    const uint32 fake_uop = (uop & 0x0F000) >> 2;      // B field is in different location, and no X field

    int len = sprintf(buf, "%s", mnemonic);
    len += padSpaces(buf, len, PARAM_COL);

    // service manual doesn't say it is illegal to +/- the PC
    // with this type of branch, but I don't think it makes sense.
    len += dasmAField(&buf[len], uop, illegal);
    len += sprintf(&buf[len], ",");
    len += dasmBField(&buf[len], fake_uop);
    len += sprintf(&buf[len], ",");
    len += dasmAddr(&buf[len], ic, new_ic);

    return len;
}


// disassemble conditional branch, reg vs immediate
// return the number of bytes printed out
static int
dasmType4(char *buf, const char *mnemonic, uint32 ic, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const uint32 new_ic   = (ic & 0xFF00) | ((uop >> 4) & 0xF0) | (uop & 0x0F);
    const uint32 fake_uop = (uop & 0x0F000) >> 2;      // B field is in different location, and no X field

    int len = sprintf(buf, "%s", mnemonic);
    len += padSpaces(buf, len, PARAM_COL);

    len += dasmIField(&buf[len], uop);
    len += sprintf(&buf[len], ",");
    len += dasmBField(&buf[len], fake_uop);
    len += sprintf(&buf[len], ",");
    len += dasmAddr(&buf[len], ic, new_ic);

    if ((uop & 0xEF000) == 0xCA000) {   // BT or BF with B=ST1
        const uint4 bitfield = static_cast<uint4>((uop >> 4) & 0x0F);
        len += padSpaces(buf, len, COMMENT_COL);
        len += sprintf(&buf[len], "; testing: ");
        dasmSt1Bitfield(buf, &len, bitfield);
    }

    return len;
}


// disassemble TPI/TMP/TIP instructions
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasmType5(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int m_field = ((uop >> 8) & 0x3);

    int len = sprintf(buf, "%s", mnemonic);
    len += dasmMField(&buf[len], uop);
    len += padSpaces(buf, len, PARAM_COL);
    if (m_field >= 2) {
        // there is a write, and A supplies the data
        len += padSpaces(buf, len, PARAM_COL);
        len += dasmAField(&buf[len], uop, illegal);
    }
    return len;
}


// disassemble transfer/exchange pc/aux instructions.
// return the number of bytes printed out
// "illegal" is an output parameter.
static int
dasmType6(char *buf, const char *mnemonic, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);
    assert(mnemonic != nullptr);

    const int r_field = uop & 0xF;
    const int m_field = ((uop >> 8) & 0x3);

    int len = sprintf(buf, "%s", mnemonic);
    len += dasmMField(&buf[len], uop);
    len += padSpaces(buf, len, PARAM_COL);
    len += dasmHex(&buf[len], r_field, 1);
    if (m_field >= 2) {
        // display source only on writes
        len += sprintf(&buf[len], ",");
        len += dasmAField(&buf[len], uop, illegal);
    }
    return len;
}


// disassemble "mini instructions".
// return the number of bytes printed out.
// "illegal" is an output parameter.
static int
dasmMiniOp(char *buf, bool &illegal, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int opcode2 = (uop >> 10) & 0x1F;
    const int m_field = (uop >>  8) & 0x3;
    int len = 0;

    illegal = false;  // default

    switch (opcode2) {

        case 0x00: // control I/O (CIO)
            len = sprintf(buf, "%s", "CIO");
            len += dasmMField(&buf[len], uop);
            len += padSpaces(buf, len, PARAM_COL);
            len += sprintf(&buf[len], "%02X", static_cast<int>(uop & 0xFF));
            if ((uop & 0xF0) != 0x00) {
                const char *comma = "";
                len += padSpaces(buf, len, COMMENT_COL);
                len += sprintf(&buf[len], "; ");
                if ((uop & 0x80) != 0x00) {
                    len += sprintf(&buf[len], "AB = K");
                    comma = ", ";
                }
                if ((uop & 0x40) != 0x00) {
                    len += sprintf(&buf[len], "%s-ABS", comma);
                    comma = ", ";
                }
                if ((uop & 0x20) != 0x00) {
                    len += sprintf(&buf[len], "%s-OBS", comma);
                    comma = ", ";
                }
                if ((uop & 0x10) != 0x00) {
                    len += sprintf(&buf[len], "%s-CBS", comma);
                }
            }

            switch (uop & 0x7F) {
                case 0x00: // noop
                case 0x10: // generate -CBS
                case 0x20: // generate -OBS
                case 0x40: // generate -ABS
                    break;
                default:
                    illegal = true;
                    break;
            }
            if (m_field >= 2) {
                // there is no source of data to feed a write
                illegal = true;
            }
            break;

        case 0x01: // subroutine return
            len = sprintf(buf, "%s", "SR");
            len += dasmMField(&buf[len], uop);
            if (m_field >= 2) {
                // there is a write, and A supplies the data
                len += padSpaces(buf, len, PARAM_COL);
                len += dasmAField(&buf[len], uop, illegal);
            }
            break;

        case 0x05: // transfer PC to IC
            len = dasmType5(buf, "TPI", illegal, uop);
            break;
        case 0x06: // transfer IC to PC
            len = dasmType5(buf, "TIP", illegal, uop);
            break;

        case 0x07: // transfer memory size to PC
            len = dasmType5(buf, "TMP", illegal, uop);
            break;

        case 0x02: // transfer PC to Aux
            len = dasmType6(buf, "TP", illegal, uop);
            break;
        case 0x03: // transfer Aux to PC
            len = dasmType6(buf, "TA", illegal, uop);
            break;
        case 0x04: // exchange PC and Aux
            len = dasmType6(buf, "XP", illegal, uop);
            break;

        case 0x08: // transfer PC to Aux,+1
            len = dasmType6(buf, "TP+1", illegal, uop);
            break;
        case 0x09: // transfer PC to Aux,-1
            len = dasmType6(buf, "TP-1", illegal, uop);
            break;
        case 0x0A: // transfer PC to Aux,+2
            len = dasmType6(buf, "TP+2", illegal, uop);
            break;
        case 0x0B: // transfer PC to Aux,-2
            len = dasmType6(buf, "TP-2", illegal, uop);
            break;

        case 0x0C: // exchange PC and Aux,+1
            len = dasmType6(buf, "XP+1", illegal, uop);
            break;
        case 0x0D: // exchange PC and Aux,-1
            len = dasmType6(buf, "XP-1", illegal, uop);
            break;
        case 0x0E: // exchange PC and Aux,+2
            len = dasmType6(buf, "XP+2", illegal, uop);
            break;
        case 0x0F: // exchange PC and Aux,-2
            len = dasmType6(buf, "XP-2", illegal, uop);
            break;
        default:
            len = sprintf(buf, "bad miniop: %02X", opcode2);
            illegal = true;
            break;
    }

    return len;
}


// disassemble one microinstruction into static buffer
// return true if error, otherwise false
static bool
dasmOp(char *buf, uint16 ic, uint32 uop) noexcept
{
    assert(buf != nullptr);

    const int opcode1 = (uop >> 15) & 0x1F;
    const int b_field = (uop >> 10) & 0xF;
    const int m_field = (uop >>  8) & 0x3;
    int len = 0;
    uint16 new_ic = 0;

    bool illegal = false;  // default

    // primary instruction decode
    switch (opcode1) {

    // register instructions:

        case 0x00:      // OR
            len = dasmType1(buf, "OR", illegal, uop);
            break;
        case 0x01:      // XOR
            len = dasmType1(buf, "XOR", illegal, uop);
            break;
        case 0x02:      // AND
            len = dasmType1(buf, "AND", illegal, uop);
            break;
        case 0x03:      // decimal subtract w/ carry
            len = dasmType1(buf, "DSC", illegal, uop);
            break;
        case 0x04:      // binary add
            len = dasmType1(buf, "A", illegal, uop);
            break;
        case 0x05:      // binary add w/ carry
            len = dasmType1(buf, "AC", illegal, uop);
            break;
        case 0x06:      // decimal add
            len = dasmType1(buf, "DA", illegal, uop);
            break;
        case 0x07:      // decimal add w/ carry
            len = dasmType1(buf, "DAC", illegal, uop);
            break;

        case 0x08:      // or immediate
            if ((m_field == 0) && (b_field == 0)) {
                // mnemonic
                len = dasmType2(buf, "ORI", illegal, uop);
            } else {
                len = dasmType2(buf, "ORI", illegal, uop);
            }
            break;
        case 0x09:      // xor immediate
            len = dasmType2(buf, "XORI", illegal, uop);
            break;
        case 0x0A:      // and immediate
            len = dasmType2(buf, "ANDI", illegal, uop);
            break;
        case 0x0B:      // mini instruction decode
            len = dasmMiniOp(buf, illegal, uop);
            break;
        case 0x0C:      // binary add immediate
            len = dasmType2(buf, "AI", illegal, uop);
            break;
        case 0x0D:      // binary add immediate w/ carry
            len = dasmType2(buf, "ACI", illegal, uop);
            break;
        case 0x0E:      // decimal add immediate
            len = dasmType2(buf, "DAI", illegal, uop);
            break;
        case 0x0F:      // decimal add immediate w/ carry
            len = dasmType2(buf, "DACI", illegal, uop);
            break;

    // branch instructions:

        case 0x10: case 0x11:   // branch if R[AAAA] == R[BBBB]
            len = dasmType3(buf, "BER", illegal, ic, uop);
            break;
        case 0x12: case 0x13:   // branch if R[AAAA] != R[BBBB]
            len = dasmType3(buf, "BNR", illegal, ic, uop);
            break;
        case 0x14: case 0x15:   // subroutine branch
            new_ic = static_cast<uint16>((uop & 0xF00F) | ((uop << 4) & 0x0F00) | ((uop >> 4) & 0x00F0));
            len = sprintf(buf, "SB");
            len += padSpaces(buf, len, PARAM_COL);
            len += dasmAddr(&buf[len], ic, new_ic);
            break;
        case 0x16: case 0x17:   // unconditional branch
            new_ic = static_cast<uint16>((uop & 0xF00F) | ((uop << 4) & 0x0F00) | ((uop >> 4) & 0x00F0));
            len = sprintf(buf, "B");
            len += padSpaces(buf, len, PARAM_COL);
            len += dasmAddr(&buf[len], ic, new_ic);
            break;
        case 0x18: case 0x19:   // branch if true
            len = dasmType4(buf, "BT", ic, uop);
            break;
        case 0x1A: case 0x1B:   // branch if false
            len = dasmType4(buf, "BF", ic, uop);
            break;
        case 0x1C: case 0x1D:   // branch if = to mask
            len = dasmType4(buf, "BEQ", ic, uop);
            break;
        case 0x1E: case 0x1F:   // branch if != to mask
            len = dasmType4(buf, "BNE", ic, uop);
            break;
    }

    return illegal;
}


bool
dasmOneOp(char *buff, uint16 ic, uint32 ucode) noexcept
{
    assert(buff != nullptr);
    char dasm_text[100];

    const bool illegal = dasmOp(&dasm_text[0], ic, ucode);
    sprintf(buff, "%04X: %05X : %s %s\n", ic, ucode, &dasm_text[0],
            illegal ? "(ILLEGAL)" : "");

    return illegal;
}

// vim: ts=8:et:sw=4:smarttab
