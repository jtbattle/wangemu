// Wang BASIC and BASIC-2 encode many keywords as a single byte.  All have
// value HEX(80) or greater.  This is a table of symbolic names for each
// token.  This information is used in two places:
//
//    (1) the keyboard can emulate a Wang keyboard, where certain shift
//        combinations produce a keyword in a single stroke.
//
//    (2) the script file function looks for escapes like \<PRINT> and
//        replaces it with the single PRINT token.

#ifndef _INCLUDE_TOKENS_H_
#define _INCLUDE_TOKENS_H_

enum token_t {

        TOKEN_LIST              = 0x80,
        TOKEN_CLEAR             = 0x81,
        TOKEN_RUN               = 0x82,
        TOKEN_RENUMBER          = 0x83,
        TOKEN_CONTINUE          = 0x84,
        TOKEN_SAVE              = 0x85,
        TOKEN_LIMITS            = 0x86,
        TOKEN_COPY              = 0x87,
        TOKEN_KEYIN             = 0x88,
        TOKEN_DSKIP             = 0x89,
        TOKEN_AND               = 0x8A,
        TOKEN_OR                = 0x8B,
        TOKEN_XOR               = 0x8C,
        TOKEN_TEMP              = 0x8D,
        TOKEN_DISK              = 0x8E,
        TOKEN_TAPE              = 0x8F,

        TOKEN_TRACE             = 0x90,
        TOKEN_LET               = 0x91,
//      TOKEN_DRAM              = 0x92, // this is in the 2200T ROM, but it isn't used
        TOKEN_FIX               = 0x92, // BASIC-2
        TOKEN_DIM               = 0x93,
        TOKEN_ON                = 0x94,
        TOKEN_STOP              = 0x95,
        TOKEN_END               = 0x96,
        TOKEN_DATA              = 0x97,
        TTOKEN_READ             = 0x98, // TOKEN_READ is apparently defined somewhere else
        TOKEN_INPUT             = 0x99,
        TOKEN_GOSUB             = 0x9A,
        TOKEN_RETURN            = 0x9B,
        TOKEN_GOTO              = 0x9C,
        TOKEN_NEXT              = 0x9D,
        TOKEN_FOR               = 0x9E,
        TOKEN_IF                = 0x9F,

        TOKEN_PRINT             = 0xA0,
        TOKEN_LOAD              = 0xA1,
        TOKEN_REM               = 0xA2,
        TOKEN_RESTORE           = 0xA3,
        TOKEN_PLOT              = 0xA4,
        TOKEN_SELECT            = 0xA5,
        TOKEN_COM               = 0xA6,
        TOKEN_PRINTUSING        = 0xA7,
        TOKEN_MAT               = 0xA8,
        TOKEN_REWIND            = 0xA9,
        TOKEN_SKIP              = 0xAA,
        TOKEN_BACKSPACE         = 0xAB,
        TOKEN_SCRATCH           = 0xAC,
        TOKEN_MOVE              = 0xAD,
        TOKEN_CONVERT           = 0xAE,
        TOKEN_SELECT_PLOT       = 0xAF, // "PLOT" in a SELECT PLOT context

        TOKEN_STEP              = 0xB0,
        TOKEN_THEN              = 0xB1,
        TOKEN_TO                = 0xB2,
        TOKEN_BEG               = 0xB3,
        TOKEN_OPEN              = 0xB4,
        TOKEN_CI                = 0xB5,
        TOKEN_R                 = 0xB6,
        TOKEN_D                 = 0xB7,
        TOKEN_CO                = 0xB8,
        TOKEN_LGT               = 0xB9, // BASIC-2
        TOKEN_OFF               = 0xBA,
        TOKEN_DBACKSPACE        = 0xBB,
        TOKEN_VERIFY            = 0xBC,
        TOKEN_DA                = 0xBD,
        TOKEN_BA                = 0xBE,
        TOKEN_DC                = 0xBF,

        TOKEN_FN                = 0xC0,
        TOKEN_ABS               = 0xC1,
        TOKEN_SQR               = 0xC2,
        TOKEN_COS               = 0xC3,
        TOKEN_EXP               = 0xC4,
        TOKEN_INT               = 0xC5,
        TOKEN_LOG               = 0xC6,
        TOKEN_SIN               = 0xC7,
        TOKEN_SGN               = 0xC8,
        TOKEN_RND               = 0xC9,
        TOKEN_TAN               = 0xCA,
        TOKEN_ARC               = 0xCB,
        TOKEN_PI                = 0xCC,
        TOKEN_TAB               = 0xCD,
        TOKEN_DEFFN             = 0xCE,
        TOKEN_ARCTAN            = 0xCF,

        TOKEN_ARCSIN            = 0xD0,
        TOKEN_ARCCOS            = 0xD1,
        TOKEN_HEX               = 0xD2,
        TOKEN_STR               = 0xD3,
        TOKEN_ATN               = 0xD4,
        TOKEN_LEN               = 0xD5,
        TOKEN_RE                = 0xD6,
        TOKEN_SHARP             = 0xD7, // [SELECT] #
        TOKEN_PERCENT           = 0xD8, // %[image]
        TOKEN_P                 = 0xD9,
        TOKEN_BT                = 0xDA,
        TOKEN_G                 = 0xDB,
        TOKEN_VAL               = 0xDC,
        TOKEN_NUM               = 0xDD,
        TOKEN_BIN               = 0xDE,
        TOKEN_POS               = 0xDF,

        TOKEN_LSEQ              = 0xE0, // "LS="
        TOKEN_ALL               = 0xE1,
        TOKEN_PACK              = 0xE2,
        TOKEN_CLOSE             = 0xE3,
        TOKEN_INIT              = 0xE4,
        TOKEN_HEX_ANOTHER       = 0xE5,
        TOKEN_UNPACK            = 0xE6,
        TOKEN_BOOL              = 0xE7,
        TOKEN_ADD               = 0xE8,
        TOKEN_ROTATE            = 0xE9,
        TOKEN_DOLLAR            = 0xEA, // $[statement]
        TOKEN_ERROR             = 0xEB, // BASIC-2 only
        TOKEN_ERR               = 0xEC, // BASIC-2 only
        TOKEN_DAC               = 0xED, // BASIC-2 only
        TOKEN_DSC               = 0xEE, // BASIC-2 only
        TOKEN_SUB               = 0xEF, // BASIC-2 only

        TOKEN_LINPUT            = 0xF0, // BASIC-2 only
        TOKEN_VER               = 0xF1, // BASIC-2 only
        TOKEN_ELSE              = 0xF2, // BASIC-2 only
        TOKEN_SPACE             = 0xF3, // BASIC-2 only
        TOKEN_ROUND             = 0xF4, // BASIC-2 only
        TOKEN_AT                = 0xF5, // BASIC-2 only
        TOKEN_HEXOF             = 0xF6, // BASIC-2 only
        TOKEN_MAX               = 0xF7, // BASIC-2 only
        TOKEN_MIN               = 0xF8, // BASIC-2 only
        TOKEN_MOD               = 0xF9, // BASIC-2 only
        TOKEN_LINENUM           = 0xFF,

};

#endif // _INCLUDE_TOKENS_H_

// vim: ts=8:et:sw=4:smarttab
