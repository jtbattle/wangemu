// this is the state maintained by the Terminal class and is
// needed by the Crt class to create a display image

#ifndef _INCLUDE_TERMINAL_STATE_H_
#define _INCLUDE_TERMINAL_STATE_H_

#include "w2200.h"

enum ui_screen_t : int;  // defined in Ui.h

// state used only in smart terminal mode (eg 2236DE)
enum char_attr_t : uint8 {
    CHAR_ATTR_RIGHT  = 0x01,  // top right horizontal line
    CHAR_ATTR_VERT   = 0x02,  // mid cell vertical line
    CHAR_ATTR_LEFT   = 0x04,  // top left horizontal line
    CHAR_ATTR_ALT    = 0x08,  // alternate character set
    CHAR_ATTR_BRIGHT = 0x10,  // high intensity
    CHAR_ATTR_BLINK  = 0x40,  // blink character
    CHAR_ATTR_INV    = 0x80,  // inverse character
};

enum cursor_attr_t {
    CURSOR_OFF,
    CURSOR_ON,
    CURSOR_BLINK
};

// this is the state which is used by UiCrt to construct the display image
struct crt_state_t
{
    ui_screen_t   screen_type;    // enum ui_screen_t
    int           chars_w;        // display width, in characters
    int           chars_h;        // display height, in characters
    int           chars_h2;       // display height, in characters (incl. status line)

    uint8         display[80*25]; // character codes
    uint8         attr[80*25];    // display attributes

    int           curs_x;         // cursor location
    int           curs_y;         // cursor location
    cursor_attr_t curs_attr;      // cursor state

    bool          dirty;          // something has changed since last refresh
};

#endif // _INCLUDE_TERMINAL_STATE_H_

// vim: ts=8:et:sw=4:smarttab
