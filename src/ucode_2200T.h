#ifndef _INCLUDE_UCODE_2200T_H_
#define _INCLUDE_UCODE_2200T_H_

#include "w2200.h"      // get data types

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200T  20480
#define  KROM_WORDS_2200T   2048

extern uint32 ucode_2200T[UCODE_WORDS_2200T];
extern uint8 kROM_2200T[KROM_WORDS_2200T];

#endif  // _INCLUDE_UCODE_2200T_H_

// vim: ts=8:et:sw=4:smarttab
