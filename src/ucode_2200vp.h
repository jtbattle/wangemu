#ifndef _INCLUDE_UCODE_2200VP_H_
#define _INCLUDE_UCODE_2200VP_H_

#include "w2200.h"      // get data types

// statically compile in an image of the ROM.
// this makes the program self-contained.
#define UCODE_WORDS_2200VP  1024
extern uint32 ucode_2200vp[UCODE_WORDS_2200VP];  // boot ucode

#endif  // _INCLUDE_UCODE_2200VP_H_
