// disassemble 8080 instructions

#include "i8080.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// the op_str strings below are interpreted with the following escapes:
//    # = print immediate 8b value
//    ## = print immediate 16b value
//
// the value_str strings below are interpreted with the following escapes:
//        r{a,b,c,d,e,h,l} = push register a/b/c/d/e/h/l on stack
//        r{B,D,H,S} = push register pair on stack
//        #  = push imm16 onto stack
//        pb = pop entry off stack and print 8b
//        pw = pop entry off stack and print 16b
//        @b = pop entry off of stack, read byte from memory, push on stack
//        @w = pop entry off of stack, read word from memory, push on stack
typedef struct {
    int         len;        // number of bytes in this instruction
    const char *op_str;     // main opcode field
    const char *value_str;  // describe values in use
} dasminfo_t;

static dasminfo_t mnemonics[256] = {
    /* 00 */ { 1,  "NOP",         "" },
    /* 01 */ { 3,  "LXI  B,##H",  "" },
    /* 02 */ { 1,  "STAX B",      "[rBpw] <- rapb"  },
    /* 03 */ { 1,  "INX  B",      "BC=rBpw" },
    /* 04 */ { 1,  "INR  B",      "B=rbpb" },
    /* 05 */ { 1,  "DCR  B",      "B=rbpb" },
    /* 06 */ { 2,  "MVI  B,#H",   "" },
    /* 07 */ { 1,  "RLC",         "A=rapb" },

    /* 08 */ { 1,  "DB   08H",    "" },
    /* 09 */ { 1,  "DAD  B",      "HL=rHpw, BC=rBpw" },
    /* 0a */ { 1,  "LDAX B",      "BC=rBpw, mem=rB@pb" },
    /* 0b */ { 1,  "DCX  B",      "BC=rBpw" },
    /* 0c */ { 1,  "INR  C",      "C=rcpb" },
    /* 0d */ { 1,  "DCR  C",      "C=rcpb" },
    /* 0e */ { 2,  "MVI  C,#H",   "" },
    /* 0f */ { 1,  "RRC",         "A=rapb" },

    /* 10 */ { 1,  "DB   10H",    "" },
    /* 11 */ { 3,  "LXI  D,##H",  "" },
    /* 12 */ { 1,  "STAX D",      "[rDpw] <- rapb" },
    /* 13 */ { 1,  "INX  D",      "DE=rDpw" },
    /* 14 */ { 1,  "INR  D",      "D=rdpb" },
    /* 15 */ { 1,  "DCR  D",      "D=rdpb" },
    /* 16 */ { 2,  "MVI  D,#H",   "" },
    /* 17 */ { 1,  "RAL",         "A=rapb" },

    /* 18 */ { 1,  "DB   18H",    "" },
    /* 19 */ { 1,  "DAD  D",      "HL=rHpw, DE=rDpw" },
    /* 1a */ { 1,  "LDAX D",      "DE=rDpw, mem=rD@bpb" },
    /* 1b */ { 1,  "DCX  D",      "DE=rDpw" },
    /* 1c */ { 1,  "INR  E",      "E=repb" },
    /* 1d */ { 1,  "DCR  E",      "E=repb" },
    /* 1e */ { 2,  "MVI  E,#H",   "" },
    /* 1f */ { 1,  "RAR",         "A=rapb" },

    /* 20 */ { 1,  "DB   20H",    "" },
    /* 21 */ { 3,  "LXI  H,##H",  "" },
    /* 22 */ { 3,  "SHLD ##H",    "HL=rHpw" },
    /* 23 */ { 1,  "INX  H",      "HL=rHpw" },
    /* 24 */ { 1,  "INR  H",      "H=rhpb" },
    /* 25 */ { 1,  "DCR  H",      "H=rhpb" },
    /* 26 */ { 2,  "MVI  H,#H",   "" },
    /* 27 */ { 1,  "DAA",         "A=rapb" },

    /* 28 */ { 1,  "DB   28H",    "" },
    /* 29 */ { 1,  "DAD  H",      "HL=rHpw" },
    /* 2a */ { 3,  "LHLD ##H",    "[#pw]=#@wpw" },
    /* 2b */ { 1,  "DCX  H",      "HL=rHpw" },
    /* 2c */ { 1,  "INR  L",      "L=rlpb" },
    /* 2d */ { 1,  "DCR  L",      "L=rlpb" },
    /* 2e */ { 2,  "MVI  L,#H",   "" },
    /* 2f */ { 1,  "CMA",         "A=rapb" },

    /* 30 */ { 1,  "DB   30H",    "" },
    /* 31 */ { 3,  "LXI  SP,##H", "" },
    /* 32 */ { 3,  "STA  ##H",    "A=rapb" },
    /* 33 */ { 1,  "INX  SP",     "SP=rSpw" },
    /* 34 */ { 1,  "INR  M",      "[rHpw]=rH@bpb" },
    /* 35 */ { 1,  "DCR  M",      "[rHpw]=rH@bpb" },
    /* 36 */ { 2,  "MVI  M,#H",   "HL=rHpw" },
    /* 37 */ { 1,  "STC",         "" },

    /* 38 */ { 1,  "DB   38H",    "" },
    /* 39 */ { 1,  "DAD  SP",     "HL=rHpw, DE=rSpw" },
    /* 3a */ { 3,  "LDA  ##H",    "[#pw]=#@bpb" },
    /* 3b */ { 1,  "DCX  SP",     "SP=rSpw" },
    /* 3c */ { 1,  "INR  A",      "A=rapb" },
    /* 3d */ { 1,  "DCR  A",      "A=rapb" },
    /* 3e */ { 2,  "MVI  A,#H",   "" },
    /* 3f */ { 1,  "CMC",         "" },

    /* 40 */ { 1,  "MOV  B,B",    "B=rbpb" },
    /* 41 */ { 1,  "MOV  B,C",    "C=rcpb" },
    /* 42 */ { 1,  "MOV  B,D",    "D=rdpb" },
    /* 43 */ { 1,  "MOV  B,E",    "E=repb" },
    /* 44 */ { 1,  "MOV  B,H",    "H=rhpb" },
    /* 45 */ { 1,  "MOV  B,L",    "L=rlpb" },
    /* 46 */ { 1,  "MOV  B,M",    "[rHpw]=rH@bpb" },
    /* 47 */ { 1,  "MOV  B,A",    "A=rapb" },

    /* 48 */ { 1,  "MOV  C,B",    "B=rbpb" },
    /* 49 */ { 1,  "MOV  C,C",    "C=rcpb" },
    /* 4a */ { 1,  "MOV  C,D",    "D=rdpb" },
    /* 4b */ { 1,  "MOV  C,E",    "E=repb" },
    /* 4c */ { 1,  "MOV  C,H",    "H=rhpb" },
    /* 4d */ { 1,  "MOV  C,L",    "L=rlpb" },
    /* 4e */ { 1,  "MOV  C,M",    "[rHpw]=rH@bpb" },
    /* 4f */ { 1,  "MOV  C,A",    "A=rapb" },

    /* 50 */ { 1,  "MOV  D,B",    "B=rbpb" },
    /* 51 */ { 1,  "MOV  D,C",    "C=rcpb" },
    /* 52 */ { 1,  "MOV  D,D",    "D=rdpb" },
    /* 53 */ { 1,  "MOV  D,E",    "E=repb" },
    /* 54 */ { 1,  "MOV  D,H",    "H=rhpb" },
    /* 55 */ { 1,  "MOV  D,L",    "L=rlpb" },
    /* 56 */ { 1,  "MOV  D,M",    "[rHpw]=rH@bpb" },
    /* 57 */ { 1,  "MOV  D,A",    "A=rapb" },

    /* 58 */ { 1,  "MOV  E,B",    "B=rbpb" },
    /* 59 */ { 1,  "MOV  E,C",    "C=rcpb" },
    /* 5a */ { 1,  "MOV  E,D",    "D=rdpb" },
    /* 5b */ { 1,  "MOV  E,E",    "E=repb" },
    /* 5c */ { 1,  "MOV  E,H",    "H=rhpb" },
    /* 5d */ { 1,  "MOV  E,L",    "L=rlpb" },
    /* 5e */ { 1,  "MOV  E,M",    "[rHpw]=rH@bpb" },
    /* 5f */ { 1,  "MOV  E,A",    "A=rapb" },

    /* 60 */ { 1,  "MOV  H,B",    "B=rbpb" },
    /* 61 */ { 1,  "MOV  H,C",    "C=rcpb" },
    /* 62 */ { 1,  "MOV  H,D",    "D=rdpb" },
    /* 63 */ { 1,  "MOV  H,E",    "E=repb" },
    /* 64 */ { 1,  "MOV  H,H",    "H=rhpb" },
    /* 65 */ { 1,  "MOV  H,L",    "L=rlpb" },
    /* 66 */ { 1,  "MOV  H,M",    "[rHpw]=rH@bpb" },
    /* 67 */ { 1,  "MOV  H,A",    "A=rapb" },

    /* 68 */ { 1,  "MOV  L,B",    "B=rbpb" },
    /* 69 */ { 1,  "MOV  L,C",    "C=rcpb" },
    /* 6a */ { 1,  "MOV  L,D",    "D=rdpb" },
    /* 6b */ { 1,  "MOV  L,E",    "E=repb" },
    /* 6c */ { 1,  "MOV  L,H",    "H=rhpb" },
    /* 6d */ { 1,  "MOV  L,L",    "L=rlpb" },
    /* 6e */ { 1,  "MOV  L,M",    "[rHpw]=rH@bpb" },
    /* 6f */ { 1,  "MOV  L,A",    "A=rapb" },

    /* 70 */ { 1,  "MOV  M,B",    "[HL=rHpw], B=rbpb" },
    /* 71 */ { 1,  "MOV  M,C",    "[HL=rHpw], C=rcpb" },
    /* 72 */ { 1,  "MOV  M,D",    "[HL=rHpw], D=rdpb" },
    /* 73 */ { 1,  "MOV  M,E",    "[HL=rHpw], E=repb" },
    /* 74 */ { 1,  "MOV  M,H",    "[HL=rHpw], H=rhpb" },
    /* 75 */ { 1,  "MOV  M,L",    "[HL=rHpw], L=rlpb" },
    /* 76 */ { 1,  "HLT",         "" },
    /* 77 */ { 1,  "MOV  M,A",    "[HL=rHpw], A=rapb" },

    /* 78 */ { 1,  "MOV  A,B",    "B=rbpb" },
    /* 79 */ { 1,  "MOV  A,C",    "B=rcpb" },
    /* 7a */ { 1,  "MOV  A,D",    "B=rdpb" },
    /* 7b */ { 1,  "MOV  A,E",    "B=repb" },
    /* 7c */ { 1,  "MOV  A,H",    "B=rhpb" },
    /* 7d */ { 1,  "MOV  A,L",    "B=rlpb" },
    /* 7e */ { 1,  "MOV  A,M",    "[HL=rHpw]=rH@bpb" },
    /* 7f */ { 1,  "MOV  A,A",    "A=rapb" },

    /* 80 */ { 1,  "ADD  B",      "A=rapb, B=rbpb" },
    /* 81 */ { 1,  "ADD  C",      "A=rapb, C=rcpb" },
    /* 82 */ { 1,  "ADD  D",      "A=rapb, D=rdpb" },
    /* 83 */ { 1,  "ADD  E",      "A=rapb, E=repb" },
    /* 84 */ { 1,  "ADD  H",      "A=rapb, H=rhpb" },
    /* 85 */ { 1,  "ADD  L",      "A=rapb, L=rlpb" },
    /* 86 */ { 1,  "ADD  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* 87 */ { 1,  "ADD  A",      "A=rapb" },

    /* 88 */ { 1,  "ADC  B",      "A=rapb, B=rbpb" },
    /* 89 */ { 1,  "ADC  C",      "A=rapb, C=rcpb" },
    /* 8a */ { 1,  "ADC  D",      "A=rapb, D=rdpb" },
    /* 8b */ { 1,  "ADC  E",      "A=rapb, E=repb" },
    /* 8c */ { 1,  "ADC  H",      "A=rapb, H=rhpb" },
    /* 8d */ { 1,  "ADC  L",      "A=rapb, L=rlpb" },
    /* 8e */ { 1,  "ADC  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* 8f */ { 1,  "ADC  A",      "A=rapb" },

    /* 90 */ { 1,  "SUB  B",      "A=rapb, B=rbpb" },
    /* 91 */ { 1,  "SUB  C",      "A=rapb, C=rcpb" },
    /* 92 */ { 1,  "SUB  D",      "A=rapb, D=rdpb" },
    /* 93 */ { 1,  "SUB  E",      "A=rapb, E=repb" },
    /* 94 */ { 1,  "SUB  H",      "A=rapb, H=rhpb" },
    /* 95 */ { 1,  "SUB  L",      "A=rapb, L=rlpb" },
    /* 96 */ { 1,  "SUB  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* 97 */ { 1,  "SUB  A",      "A=rapb" },

    /* 98 */ { 1,  "SBB  B",      "A=rapb, B=rbpb" },
    /* 99 */ { 1,  "SBB  C",      "A=rapb, C=rcpb" },
    /* 9a */ { 1,  "SBB  D",      "A=rapb, D=rdpb" },
    /* 9b */ { 1,  "SBB  E",      "A=rapb, E=repb" },
    /* 9c */ { 1,  "SBB  H",      "A=rapb, H=rhpb" },
    /* 9d */ { 1,  "SBB  L",      "A=rapb, L=rlpb" },
    /* 9e */ { 1,  "SBB  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* 9f */ { 1,  "SBB  A",      "A=rapb" },

    /* a0 */ { 1,  "ANA  B",      "A=rapb, B=rbpb" },
    /* a1 */ { 1,  "ANA  C",      "A=rapb, C=rcpb" },
    /* a2 */ { 1,  "ANA  D",      "A=rapb, D=rdpb" },
    /* a3 */ { 1,  "ANA  E",      "A=rapb, E=repb" },
    /* a4 */ { 1,  "ANA  H",      "A=rapb, H=rhpb" },
    /* a5 */ { 1,  "ANA  L",      "A=rapb, L=rlpb" },
    /* a6 */ { 1,  "ANA  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* a7 */ { 1,  "ANA  A",      "A=rapb" },

    /* a8 */ { 1,  "XRA  B",      "A=rapb, B=rbpb" },
    /* a9 */ { 1,  "XRA  C",      "A=rapb, C=rcpb" },
    /* aa */ { 1,  "XRA  D",      "A=rapb, D=rdpb" },
    /* ab */ { 1,  "XRA  E",      "A=rapb, E=repb" },
    /* ac */ { 1,  "XRA  H",      "A=rapb, H=rhpb" },
    /* ad */ { 1,  "XRA  L",      "A=rapb, L=rlpb" },
    /* ae */ { 1,  "XRA  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* af */ { 1,  "XRA  A",      "A=rapb" },

    /* b0 */ { 1,  "ORA  B",      "A=rapb, B=rbpb" },
    /* b1 */ { 1,  "ORA  C",      "A=rapb, C=rcpb" },
    /* b2 */ { 1,  "ORA  D",      "A=rapb, D=rdpb" },
    /* b3 */ { 1,  "ORA  E",      "A=rapb, E=repb" },
    /* b4 */ { 1,  "ORA  H",      "A=rapb, H=rhpb" },
    /* b5 */ { 1,  "ORA  L",      "A=rapb, L=rlpb" },
    /* b6 */ { 1,  "ORA  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* b7 */ { 1,  "ORA  A",      "A=rapb" },

    /* b8 */ { 1,  "CMP  B",      "A=rapb, B=rbpb" },
    /* b9 */ { 1,  "CMP  C",      "A=rapb, C=rcpb" },
    /* ba */ { 1,  "CMP  D",      "A=rapb, D=rdpb" },
    /* bb */ { 1,  "CMP  E",      "A=rapb, E=repb" },
    /* bc */ { 1,  "CMP  H",      "A=rapb, H=rhpb" },
    /* bd */ { 1,  "CMP  L",      "A=rapb, L=rlpb" },
    /* be */ { 1,  "CMP  M",      "A=rapb, [rHpw]=rH@bpb" },
    /* bf */ { 1,  "CMP  A",      "A=rapb" },

    /* c0 */ { 1,  "RNZ",         "" },
    /* c1 */ { 1,  "POP  B",      "" },
    /* c2 */ { 3,  "JNZ  ##H",    "" },
    /* c3 */ { 3,  "JMP  ##H",    "" },
    /* c4 */ { 3,  "CNZ  ##H",    "" },
    /* c5 */ { 1,  "PUSH B",      "BC=rBpw" },
    /* c6 */ { 2,  "ADI  #H",     "A=rapb" },
    /* c7 */ { 1,  "RST  0",      "" },

    /* c8 */ { 1,  "RZ",          "" },
    /* c9 */ { 1,  "RET",         "" },
    /* ca */ { 3,  "JZ   ##H",    "" },
    /* cb */ { 1,  "DB   0CBH",   "" },
    /* cc */ { 3,  "CZ   ##H",    "" },
    /* cd */ { 3,  "CALL ##H",    "" },
    /* ce */ { 2,  "ACI  #H",     "A=rapb" },
    /* cf */ { 1,  "RST  1",      "" },

    /* d0 */ { 1,  "RNC",         "" },
    /* d1 */ { 1,  "POP  D",      "" },
    /* d2 */ { 3,  "JNC  ##H",    "" },
    /* d3 */ { 2,  "OUT  #H",     "" },
    /* d4 */ { 3,  "CNC  ##H",    "" },
    /* d5 */ { 1,  "PUSH D",      "DE=rDpw" },
    /* d6 */ { 2,  "SUI  #H",     "A=rapb" },
    /* d7 */ { 1,  "RST  2",      "" },

    /* d8 */ { 1,  "RC",          "" },
    /* d9 */ { 1,  "DB   0D9H",   "" },
    /* da */ { 3,  "JC   ##H",    "" },
    /* db */ { 2,  "IN   #H",     "" },
    /* dc */ { 3,  "CC   ##H",    "" },
    /* dd */ { 1,  "DB   0DDH",   "" },
    /* de */ { 2,  "SBI  #H",     "A=rapb" },
    /* df */ { 1,  "RST  3",      "" },

    /* e0 */ { 1,  "RPO",         "" },
    /* e1 */ { 1,  "POP  H",      "" },
    /* e2 */ { 3,  "JPO  ##H",    "" },
    /* e3 */ { 1,  "XTHL",        "HL=rHpw" },
    /* e4 */ { 3,  "CPO  ##H",    "" },
    /* e5 */ { 1,  "PUSH H",      "HL=rHpw" },
    /* e6 */ { 2,  "ANI  #H",     "A=rapb" },
    /* e7 */ { 1,  "RST  4",      "" },

    /* e8 */ { 1,  "RPE",         "" },
    /* e9 */ { 1,  "PCHL",        "HL=rHpw" },
    /* ea */ { 3,  "JPE  ##H",    "" },
    /* eb */ { 1,  "XCHG",        "" },
    /* ec */ { 3,  "CPE  ##H",    "" },
    /* ed */ { 1,  "DB   0EDH",   "" },
    /* ee */ { 2,  "XRI  #H",     "A=rapb" },
    /* ef */ { 1,  "RST  5",      "" },

    /* f0 */ { 1,  "RP",          "" },
    /* f1 */ { 1,  "POP  PSW",    "" },
    /* f2 */ { 3,  "JP   ##H",    "" },
    /* f3 */ { 1,  "DI",          "" },
    /* f4 */ { 3,  "CP   ##H",    "" },
    /* f5 */ { 1,  "PUSH PSW",    "" },
    /* f6 */ { 2,  "ORI  #H",     "A=rapb" },
    /* f7 */ { 1,  "RST  6",      "" },

    /* f8 */ { 1,  "RM",          "" },
    /* f9 */ { 1,  "SPHL",        "" },
    /* fa */ { 3,  "JM   ##H",    "" },
    /* fb */ { 1,  "EI",          "" },
    /* fc */ { 3,  "CM   ##H",    "" },
    /* fd */ { 1,  "DB   0FDH",   "" },
    /* fe */ { 2,  "CPI  #H",     "A=rapb" },
    /* ff */ { 1,  "RST  7"       "" },
};

// disassemble the instruction at the given address into a pre-allocated
// character array (to avoid lots of dynamic memory activity).
// it returns the number of bytes in the instruction.
int
i8080_disassemble(i8080 *cpu, char *buff, int addr, int annotate)
{
    int byte0 = RD_BYTE(addr);
    int byte1 = RD_BYTE(addr+1);
    int byte2 = RD_BYTE(addr+2);

    int op    = byte0;
    int imm8  = byte1;
    int imm16 = (byte2 << 8) + byte1;

    int stack[2];
    int sp = 0; // stack pointer

    int         len = mnemonics[op].len;
    const char *src = mnemonics[op].op_str;
    char       *dst = buff;

    char hex[10], ch;

    // scan opcode string, expanding escapes while copying
    while ((ch = *src++)) {
        switch (ch) {

        case '#':
            if (*src == '#') {
                // insert immediate 16b value
                sprintf(hex, "%04X", imm16);
                strcpy(dst,hex);
                dst += 4;
                src++;
            } else {
                // insert immediate 8b value
                sprintf(hex, "%02X", imm8);
                strcpy(dst,hex);
                dst += 2;
            }
            break;

        default:
            // literal copy
            *dst++ = ch;
            break;

        } // switch
    } // while

    // space over to a consistent column before adding value fields
    while (dst-buff < 16) {
        *dst++ = ' ';
    }
    *dst++ = ';';
    *dst++ = ' ';

    // add informational fields
    // TODO: maybe just print all registers?
    src = mnemonics[op].value_str;
    while (annotate && (ch = *src++)) {
        assert(sp >= 0 && sp <= 2);
        switch (ch) {

        case 'r':
            // push register or register pair on stack
            switch (*src++) {
                case 'b': stack[sp++] = B; break;
                case 'c': stack[sp++] = C; break;
                case 'd': stack[sp++] = D; break;
                case 'e': stack[sp++] = E; break;
                case 'h': stack[sp++] = H; break;
                case 'l': stack[sp++] = L; break;
                case 'a': stack[sp++] = A; break;
                case 'B': stack[sp++] = BC; break;
                case 'D': stack[sp++] = DE; break;
                case 'H': stack[sp++] = HL; break;
                case 'S': stack[sp++] = SP; break;
                default: assert(0);
            }
            break;

        case '#':
            stack[sp++] = imm16;
            break;

        case '@':
            {
                // indirection
                assert(sp > 0);
                int tos = stack[--sp];
                int val = 0;
                switch (*src++) {
                    case 'b': val = RD_BYTE(tos); break;
                    case 'w': val = RD_WORD(tos); break;
                    default: assert(0);
                }
                stack[sp++] = val;
            }
            break;

        case 'p':
            {
                // print
                assert(sp > 0);
                int tos = stack[--sp];
                switch (*src++) {
                    case 'b':
                        sprintf(hex, "%02X", tos);
                        strcpy(dst, hex);
                        dst += 2;
                        break;
                    case 'w':
                        sprintf(hex, "%04X", tos);
                        strcpy(dst, hex);
                        dst += 4;
                        break;
                    default:
                        assert(0);
                }
            }
            break;

        default:
            // literal copy
            *dst++ = ch;
            break;

        } // switch
    } // while
    assert(sp == 0);

    // terminate string
    *dst = '\0';

    return len;
}
