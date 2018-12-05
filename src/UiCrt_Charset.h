#ifndef _INCLUDE_UI_CRT_CHARSET_H_
#define _INCLUDE_UI_CRT_CHARSET_H_

#include "w2200.h"
#include "wx/wx.h"

// these arrays are defined in UiCrt_Charset.cpp
// and contain the bitmap images of the 2236 character generator ROM.

extern uint8 chargen[];          // font character generator
extern uint8 chargen_2236_alt[]; // 2236 alternate character set

extern uint8 xlat_char[128];     // mapping to closest LATIN-1 equivalent
extern uint8 xlat_char_alt[64];  // mapping to closest LATIN-1 equivalent

extern wxString unicode_xlat_char[128];
extern wxString unicode_xlat_char_alt[64];

#endif // _INCLUDE_UI_CRT_CHARSET_H_

// vim: ts=8:et:sw=4:smarttab
