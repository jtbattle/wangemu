#ifndef _INCLUDE_UCODE_2200B_H_
#define _INCLUDE_UCODE_2200B_H_

#include "w2200.h"      // get data types

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200B  16384  // main ucode
#define UCODE_WORDS_2200BX   256  // patch routines
#define  KROM_WORDS_2200B   2048

extern uint32 ucode_2200B[UCODE_WORDS_2200B];
extern uint32 ucode_2200BX[UCODE_WORDS_2200BX];
extern uint8 kROM_2200B[KROM_WORDS_2200B];

#endif  // _INCLUDE_UCODE_2200B_H_
