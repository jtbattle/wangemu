#ifndef _INCLUDE_UI_CRT_CHARSET_H_
#define _INCLUDE_UI_CRT_CHARSET_H_

#include "w2200.h"

// these arrays are defined in UiCrt_Charset.cpp
// and contain the bitmap images of the 2226 character generator ROM.

extern uint8 chargen[];         // font character generator
extern uint8 xlat_char[128];    // mapping to closest LATIN-1 equivalent

#endif // _INCLUDE_UI_CRT_CHARSET_H_

// vim: ts=8:et:sw=4:smarttab
