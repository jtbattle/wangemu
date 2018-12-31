#ifndef _INCLUDE_UCODE_2200_H_
#define _INCLUDE_UCODE_2200_H_

#include "w2200.h"      // get data types

//------------------------------------------------------------------------------
// 2200B microcode
//------------------------------------------------------------------------------

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200B  16384  // main ucode
#define UCODE_WORDS_2200BX   256  // patch routines
#define  KROM_WORDS_2200B   2048

extern uint32 ucode_2200B[UCODE_WORDS_2200B];
extern uint32 ucode_2200BX[UCODE_WORDS_2200BX];
extern uint8  kROM_2200B[KROM_WORDS_2200B];

//------------------------------------------------------------------------------
// 2200T microcode
//------------------------------------------------------------------------------

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200T  20480
#define  KROM_WORDS_2200T   2048

extern uint32 ucode_2200T[UCODE_WORDS_2200T];
extern uint8  kROM_2200T[KROM_WORDS_2200T];

//------------------------------------------------------------------------------
// 2200VP microcode
//------------------------------------------------------------------------------

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200VP  1024
extern uint32 ucode_2200vp[UCODE_WORDS_2200VP];  // boot ucode

#endif // _INCLUDE_UCODE_2200_H_

// vim: ts=8:et:sw=4:smarttab
