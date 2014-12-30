// Wang 2200-T microcode disassembler.

#include <fstream>

#include "w2200.h"


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

#if 0
static void
quit(int rc)
{
    char dummy[300];
    printf("Hit <CR> to quit\n");
    gets(dummy);
    exit(rc);
}
#endif


// fill out the buffer to the specified number of spaces
static void
pad_spaces(char *buf, int *off, int limit)
{
    int p = *off;
    while (p < limit) {
        buf[p++] = ' ';
    }
    buf[p] = '\0';
    *off = p;
}


// stuff in a hex value at the buffer location offset "off",
// updating off in the process, 'digits' long.  if the numer
// starts with A-F, preceed it with a bonus 0.
static void
hex(char *buf, int *off, int value, int digits)
{
    char fmtstr[16];

    // figure out if we need a leading zero
    int first_dig = (value >> 4*(digits-1)) & 0xF;
    if (first_dig >= 10)
        digits++;

    sprintf(fmtstr, "%%0%dX", digits);
    sprintf(&buf[*off], fmtstr, value);
    *off += digits;
}


// stuff in the target address, in hex.  however, if the address
// is very near the current address, list it as relative.
static void
addr(char *buf, int *off, int cur_pc, int new_pc)
{
    int len = 0;
#if DASM_REL_BRANCH
    const int window = 2;       // how big of a window to use relative addr
    int diff = new_pc - cur_pc;

    if (diff == 0)
        len += sprintf(&buf[*off], "*");
    else if (diff > 0 && diff <= window)
        len += sprintf(&buf[*off], "*+%d", diff);
    else if (diff < 0 && diff >= -window)
        len += sprintf(&buf[*off], "*%d", diff);
    else
#endif
        hex(buf, off, new_pc, 4);

    *off += len;
}


// disassemble the A input bus field
// *adj is set to 1 if the PC value is modified (if adj != NULL)
// return the number of bytes printed out
static int
dasm_a_field(char *buf, uint32 uop, int *adj)
{
    int field = ((uop >> 4) & 0xF);
    char *str;
    int pc;

    if (field < 8)
        return sprintf(buf, "F%d", field);

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
            ASSERT(0);
            break;
    }

    // indicate if the PC would get adjusted
    if (adj != NULL)
        *adj |= pc;

    return sprintf(buf, str);
}


// disassemble the I input bus field
// return the number of bytes printed out
static int
dasm_i_field(char *buf, uint32 uop)
{
    int field = ((uop >> 4) & 0xF);
    int len = 0;

    hex(buf, &len, field, 1);
    return len;
}


// disassemble the B input bus field
// return the number of bytes printed out
static int
dasm_b_field(char *buf, uint32 uop)
{
    int xbit  = ((uop >> 14) & 0x1);
    int field = ((uop >> 10) & 0xF);
    char *str;

    if (field < 8)
        return sprintf(buf, "F%d", field);

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
            default: str = NULL; ASSERT(0); break;
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
            default: str = NULL; ASSERT(0); break;
        }
    }

    return sprintf(buf, str);
}


// disassemble the C output bus field
// return the number of bytes printed out
static int
dasm_c_field(char *buf, int *illegal, uint32 uop)
{
    int xbit  = ((uop >> 14) & 0x1);
    int field = ((uop >>  0) & 0xF);
    char *str;

    if (field < 8) {
        *illegal = 0;
        return sprintf(buf, "F%d", field);
    }

    if (xbit) {
        switch (field) {
            case  8: str = "ST3"; break;
            case  9: str = "ST4"; break;
            case 10: str = "PC2"; break;
            case 11: str = "PC3"; break;
            case 12: str = "PC4"; break;
            case 13: str = NULL;  break;
            case 14: str = NULL;  break;
            case 15: str = "";    break;        // dummy destination
            default: str = NULL; ASSERT(0); break;
        }
    } else {
        switch (field) {
            case  8: str = "KH";  break;
            case  9: str = "KL";  break;
            case 10: str = "ST1"; break;
            case 11: str = "ST2"; break;
            case 12: str = "PC1"; break;
            case 13: str = NULL;  break;
            case 14: str = NULL;  break;
            case 15: str = "";    break;        // dummy destination
            default: str = NULL; ASSERT(0); break;
        }
    }

    if (str == NULL) {
        *illegal = 1;
        return sprintf(buf, "???");
    } else {
        *illegal = 0;
        if (str[0] != '\0')
            return sprintf(buf, "%s", str);
        return 0; // no dest
    }
}


// disassemble the M field.
// this is used by the mini ops, and may be a read or write.
// if it is a read, the data goes into the C data read register.
// if it is a write, the data comes from whatever is on the A bus.
// return the number of bytes printed out
static int
dasm_m_field(char *buf, uint32 uop)
{
    int m_field = ((uop >> 8) & 0x3);

    switch (m_field) {
         case 0: return 0;
         case 1: return sprintf(buf, ",R");
         case 2: return sprintf(buf, ",W1");
         case 3: return sprintf(buf, ",W2");
        default: ASSERT(0); return 0;
    }
}


static void
dasm_ST1_bitfield(char *buf, int *len, uint4 bits)
{
    int many = 0;
    int i;
    int p = *len;
    char *str;

    for(i=0; i<4; i++) {
        if (bits & (1 << i)) {
            if (many) {
                strcpy(&buf[p], ", ");
                p += 2;
            }
            many = 1;
            switch(i) {
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
dasm_ST3_bitfield(char *buf, int *len, uint4 bits)
{
    int many = 0;
    int i;
    int p = *len;
    char *str;

    for(i=0; i<4; i++) {
        if (bits & (1 << i)) {
            if (many) {
                strcpy(&buf[p], ", ");
                p += 2;
            }
            many = 1;
            switch(i) {
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
static int
dasm_type1(char *buf, char *mnemonic, int *illegal, uint32 uop)
{
    int b_field = (uop >> 10) & 0xF;
    int mov = DASM_PSEUDO_OPS
           && !strcmp(mnemonic,"OR")
           && (b_field == 0xF); // OR a,0,c  --> MV a,c
    const char *mnem = (mov) ? "MV" : mnemonic;
    int len;
    int bad;

    len = sprintf(buf, "%s", mnem);
    len += dasm_m_field(&buf[len], uop);
    pad_spaces(buf, &len, PARAM_COL);

    len += dasm_a_field(&buf[len], uop, NULL);
    if (!mov) {
        len += sprintf(&buf[len], ",");
        len += dasm_b_field(&buf[len], uop);
    }
    len += sprintf(&buf[len], ",");
    len += dasm_c_field(&buf[len], &bad, uop);

    *illegal = bad;
    return len;
}


// disassemble ALU op with immediate
// return the number of bytes printed out
static int
dasm_type2(char *buf, char *mnemonic, int *illegal, uint32 uop)
{
    int i_field = (uop >>  4) & 0xF;
    int b_field = (uop >> 10) & 0xF;
    int c_field = (uop >>  0) & 0xF;
    int x_field = (uop >> 14) & 0x1;
    int b_st1 = (!x_field && (b_field == 0xA)); // B is ST1
    int b_st3 = ( x_field && (b_field == 0x8)); // B is ST3
    int c_st1 = (!x_field && (c_field == 0xA)); // C is ST1
    int c_st3 = ( x_field && (c_field == 0x8)); // C is ST3
    int ori  = !strcmp(mnemonic,"ORI");
    int andi = !strcmp(mnemonic,"ANDI");
    int mvi = DASM_PSEUDO_OPS
           && ori && (b_field == 0xF);  // ORI imm,0,c  --> MVI imm,c
    int mov = DASM_PSEUDO_OPS && !mvi
           && ori && (i_field == 0x0);  // ORI 0,b,c    --> MV b,c
    const char *mnem = (mvi) ? "MVI"
                     : (mov) ? "MV"
                             : mnemonic;
    int len;
    int bad;

    len = sprintf(buf, "%s", mnem);
    len += dasm_m_field(&buf[len], uop);
    pad_spaces(buf, &len, PARAM_COL);

    if (!mov)
        len += dasm_i_field(&buf[len], uop);
    if (!mov && !mvi)
        len += sprintf(&buf[len], ",");
    if (!mvi)
        len += dasm_b_field(&buf[len], uop);
    len += sprintf(&buf[len], ",");
    len += dasm_c_field(&buf[len], &bad, uop);

    if (ori && ((b_st1 && c_st1) || (b_st3 && c_st3))) {
        // ORI #,ST1,ST1 = setting ST1 flag bit(s)
        // ORI #,ST3,ST3 = setting ST3 flag bit(s)
        uint4 bitfield = (uint4)i_field;
        pad_spaces(buf, &len, COMMENT_COL);
        len += sprintf(&buf[len], "; setting: ");
        ((b_st1) ? dasm_ST1_bitfield : dasm_ST3_bitfield) (buf, &len, bitfield);
    }
    if (andi && ((b_st1 && c_st1) || (b_st3 && c_st3))) {
        // ANDI #,ST1,ST1 = clearing ST1 flag bit(s)
        // ANDI #,ST3,ST3 = clearing ST3 flag bit(s)
        uint4 bitfield = (uint4)(~i_field & 0xF);
        pad_spaces(buf, &len, COMMENT_COL);
        len += sprintf(&buf[len], "; clearing: ");
        ((b_st1) ? dasm_ST1_bitfield : dasm_ST3_bitfield) (buf, &len, bitfield);
    }
    if (mvi && (c_st1 || c_st3)) {
        uint4 bitfield = (uint4)i_field;
        pad_spaces(buf, &len, COMMENT_COL);
        len += sprintf(&buf[len], "; setting: ");
        ((c_st1) ? dasm_ST1_bitfield : dasm_ST3_bitfield) (buf, &len, bitfield);
    }

    *illegal = bad;
    return len;
}


// disassemble conditional branch, reg to reg compare
// return the number of bytes printed out
static int
dasm_type3(char *buf, char *mnemonic, int *illegal, uint32 ic, uint32 uop)
{
    uint32 newic   = (ic & 0xFF00) | ((uop >> 4) & 0xF0) | (uop & 0x0F);
    uint32 fakeuop = (uop & 0x0F000) >> 2;      // B field is in different location, and no X field
    int len;

    len = sprintf(buf, "%s", mnemonic);
    pad_spaces(buf, &len, PARAM_COL);

    // service manual doesn't say it is illegal to +/- the PC
    // with this type of branch, but I don't think it makes sense.
    len += dasm_a_field(&buf[len], uop, illegal);
    len += sprintf(&buf[len], ",");
    len += dasm_b_field(&buf[len], fakeuop);
    len += sprintf(&buf[len], ",");
    addr(buf, &len, ic, newic);

    return len;
}


// disassemble conditional branch, reg vs immediate
// return the number of bytes printed out
static int
dasm_type4(char *buf, char *mnemonic, uint32 ic, uint32 uop)
{
    uint32 newic   = (ic & 0xFF00) | ((uop >> 4) & 0xF0) | (uop & 0x0F);
    uint32 fakeuop = (uop & 0x0F000) >> 2;      // B field is in different location, and no X field
    int len;

    len = sprintf(buf, "%s", mnemonic);
    pad_spaces(buf, &len, PARAM_COL);

    len += dasm_i_field(&buf[len], uop);
    len += sprintf(&buf[len], ",");
    len += dasm_b_field(&buf[len], fakeuop);
    len += sprintf(&buf[len], ",");
    addr(buf, &len, ic, newic);

    if ((uop & 0xEF000) == 0xCA000) {   // BT or BF with B=ST1
        uint4 bitfield = (uint4)((uop >> 4) & 0x0F);
        pad_spaces(buf, &len, COMMENT_COL);
        len += sprintf(&buf[len], "; testing: ");
        dasm_ST1_bitfield(buf, &len, bitfield);
    }

    return len;
}

// disassemble TPI/TMP/TIP instructions
// return the number of bytes printed out
static int
dasm_type5(char *buf, char *mnemonic, int *illegal, uint32 uop)
{
    int m_field = ((uop >> 8) & 0x3);
    int len;

    len = sprintf(buf, "%s", mnemonic);
    len += dasm_m_field(&buf[len], uop);
    pad_spaces(buf, &len, PARAM_COL);
    if (m_field >= 2) {
        // there is a write, and A supplies the data
        pad_spaces(buf, &len, PARAM_COL);
        len += dasm_a_field(&buf[len], uop, illegal);
    }
    return len;
}


// disassemble transfer/exchange pc/aux instructions.
// return the number of bytes printed out
static int
dasm_type6(char *buf, char *mnemonic, int *illegal, uint32 uop)
{
    int r_field = uop & 0xF;
    int m_field = ((uop >> 8) & 0x3);
    int len;

    len = sprintf(buf, "%s", mnemonic);
    len += dasm_m_field(&buf[len], uop);
    pad_spaces(buf, &len, PARAM_COL);
    hex(buf, &len, r_field, 1);
    if (m_field >= 2) {
        // display source only on writes
        len += sprintf(&buf[len], ",");
        len += dasm_a_field(&buf[len], uop, illegal);
    }
    return len;
}


// disassemble "mini instructions".
// return the number of bytes printed out.
static int
dasm_miniop(char *buf, int *illegal, uint32 uop)
{
    int opcode2 = (uop >> 10) & 0x1F;
    int m_field = (uop >>  8) & 0x3;
    int len;

    *illegal = 0;       // default

    switch (opcode2) {

        case 0x00: // control I/O (CIO)
            len = sprintf(buf, "%s", "CIO");
            len += dasm_m_field(&buf[len], uop);
            pad_spaces(buf, &len, PARAM_COL);
            len += sprintf(&buf[len], "%02X", (int)(uop & 0xFF));
            if ((uop & 0xF0) != 0x00) {
                char *comma = "";
                pad_spaces(buf, &len, COMMENT_COL);
                len += sprintf(&buf[len], "; ");
                if (uop & 0x80) {
                    len += sprintf(&buf[len], "AB = K");
                    comma = ", ";
                }
                if (uop & 0x40) {
                    len += sprintf(&buf[len], "%s-ABS", comma);
                    comma = ", ";
                }
                if (uop & 0x20) {
                    len += sprintf(&buf[len], "%s-OBS", comma);
                    comma = ", ";
                }
                if (uop & 0x10)
                    len += sprintf(&buf[len], "%s-CBS", comma);
            }

            switch (uop & 0x7F) {
                case 0x00: // noop
                case 0x10: // generate -CBS
                case 0x20: // generate -OBS
                case 0x40: // generate -ABS
                    break;
                default:
                    *illegal = 1;
                    break;
            }
            if (m_field >= 2) {
                // there is no source of data to feed a write
                *illegal = 1;
            }
            break;

        case 0x01: // subroutine return
            len = sprintf(buf, "%s", "SR");
            len += dasm_m_field(&buf[len], uop);
            if (m_field >= 2) {
                // there is a write, and A supplies the data
                pad_spaces(buf, &len, PARAM_COL);
                len += dasm_a_field(&buf[len], uop, illegal);
            }
            break;

        case 0x05: // transfer PC to IC
            len = dasm_type5(buf, "TPI", illegal, uop);
            break;
        case 0x06: // transfer IC to PC
            len = dasm_type5(buf, "TIP", illegal, uop);
            break;

        case 0x07: // transfer memory size to PC
            len = dasm_type5(buf, "TMP", illegal, uop);
            break;

        case 0x02: // transfer PC to Aux
            len = dasm_type6(buf, "TP", illegal, uop);
            break;
        case 0x03: // transfer Aux to PC
            len = dasm_type6(buf, "TA", illegal, uop);
            break;
        case 0x04: // exchange PC and Aux
            len = dasm_type6(buf, "XP", illegal, uop);
            break;

        case 0x08: // transfer PC to Aux,+1
            len = dasm_type6(buf, "TP+1", illegal, uop);
            break;
        case 0x09: // transfer PC to Aux,-1
            len = dasm_type6(buf, "TP-1", illegal, uop);
            break;
        case 0x0A: // transfer PC to Aux,+2
            len = dasm_type6(buf, "TP+2", illegal, uop);
            break;
        case 0x0B: // transfer PC to Aux,-2
            len = dasm_type6(buf, "TP-2", illegal, uop);
            break;

        case 0x0C: // exchange PC and Aux,+1
            len = dasm_type6(buf, "XP+1", illegal, uop);
            break;
        case 0x0D: // exchange PC and Aux,-1
            len = dasm_type6(buf, "XP-1", illegal, uop);
            break;
        case 0x0E: // exchange PC and Aux,+2
            len = dasm_type6(buf, "XP+2", illegal, uop);
            break;
        case 0x0F: // exchange PC and Aux,-2
            len = dasm_type6(buf, "XP-2", illegal, uop);
            break;
        default:
            len = sprintf(buf, "bad miniop: %02X", opcode2);
            *illegal = 1;
            break;
    }

    return len;
}


// disassemble one microinstruction into static buffer
// return 0 if OK, 1 if error
int
dasm_op(char *buf, uint32 ic, uint32 uop)
{
    int opcode1 = (uop >> 15) & 0x1F;
    int b_field = (uop >> 10) & 0xF;
    int m_field = (uop >>  8) & 0x3;
    int illegal;
    int len;
    uint16 newic;

    illegal = 0;        // default

    // primary instruction decode
    switch (opcode1) {

    // register instructions:

        case 0x00:      // OR
            len = dasm_type1(buf, "OR", &illegal, uop);
            break;
        case 0x01:      // XOR
            len = dasm_type1(buf, "XOR", &illegal, uop);
            break;
        case 0x02:      // AND
            len = dasm_type1(buf, "AND", &illegal, uop);
            break;
        case 0x03:      // decimal subtract w/ carry
            len = dasm_type1(buf, "DSC", &illegal, uop);
            break;
        case 0x04:      // binary add
            len = dasm_type1(buf, "A", &illegal, uop);
            break;
        case 0x05:      // binary add w/ carry
            len = dasm_type1(buf, "AC", &illegal, uop);
            break;
        case 0x06:      // decimal add
            len = dasm_type1(buf, "DA", &illegal, uop);
            break;
        case 0x07:      // decimal add w/ carry
            len = dasm_type1(buf, "DAC", &illegal, uop);
            break;

        case 0x08:      // or immediate
            if ((m_field == 0) && (b_field == 0)) {
                // mnemonic
                len = dasm_type2(buf, "ORI", &illegal, uop);
            } else {
                len = dasm_type2(buf, "ORI", &illegal, uop);
            }
            break;
        case 0x09:      // xor immediate
            len = dasm_type2(buf, "XORI", &illegal, uop);
            break;
        case 0x0A:      // and immediate
            len = dasm_type2(buf, "ANDI", &illegal, uop);
            break;
        case 0x0B:      // mini instruction decode
            len = dasm_miniop(buf, &illegal, uop);
            break;
        case 0x0C:      // binary add immediate
            len = dasm_type2(buf, "AI", &illegal, uop);
            break;
        case 0x0D:      // binary add immediate w/ carry
            len = dasm_type2(buf, "ACI", &illegal, uop);
            break;
        case 0x0E:      // decimal add immediate
            len = dasm_type2(buf, "DAI", &illegal, uop);
            break;
        case 0x0F:      // decimal add immediate w/ carry
            len = dasm_type2(buf, "DACI", &illegal, uop);
            break;

    // branch instructions:

        case 0x10: case 0x11:   // branch if R[AAAA] == R[BBBB]
            len = dasm_type3(buf, "BER", &illegal, ic, uop);
            break;
        case 0x12: case 0x13:   // branch if R[AAAA] != R[BBBB]
            len = dasm_type3(buf, "BNR", &illegal, ic, uop);
            break;
        case 0x14: case 0x15:   // subroutine branch
            newic = (uint16)((uop & 0xF00F) | ((uop<<4) & 0x0F00) | ((uop>>4) & 0x00F0));
            len = sprintf(buf, "SB");
            pad_spaces(buf, &len, PARAM_COL);
            addr(buf, &len, ic, newic);
            break;
        case 0x16: case 0x17:   // unconditional branch
            newic = (uint16)((uop & 0xF00F) | ((uop<<4) & 0x0F00) | ((uop>>4) & 0x00F0));
            len = sprintf(buf, "B");
            pad_spaces(buf, &len, PARAM_COL);
            addr(buf, &len, ic, newic);
            break;
        case 0x18: case 0x19:   // branch if true
            len = dasm_type4(buf, "BT", ic, uop);
            break;
        case 0x1A: case 0x1B:   // branch if false
            len = dasm_type4(buf, "BF", ic, uop);
            break;
        case 0x1C: case 0x1D:   // branch if = to mask
            len = dasm_type4(buf, "BEQ", ic, uop);
            break;
        case 0x1E: case 0x1F:   // branch if != to mask
            len = dasm_type4(buf, "BNE", ic, uop);
            break;
    }

    return illegal;
}


int
dasm_one(char *buff, uint32 ic, uint32 ucode)
{
    char dasmtext[100];
    int illegal;

    illegal = dasm_op(dasmtext, ic, ucode);
    sprintf(buff, "%04X: %05X : %s %s\n", ic, ucode, dasmtext,
            illegal ? "(ILLEGAL)" : "");

    return illegal;
}

