// this class is responsible for modeling a 2236 or similar terminal.

#ifndef _INCLUDE_TERMINAL_H_
#define _INCLUDE_TERMINAL_H_

#include "w2200.h"
#include "TerminalState.h"

class CrtFrame;
enum ui_screen_t;

class Terminal
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Terminal);

    Terminal(int io_addr, int term_num, ui_screen_t screen_type);
    ~Terminal();

    // hardware reset
    void reset(bool hard_reset);

    // send a character to the display controller
    void processChar(uint8 byte);

private:
    // ---- functions ----

    // clear the display and home the cursor
    void scr_clear();

    // scroll the contents of the screen up one row,
    // and fill the new row with blanks.
    void scr_scroll();

    // lower level crt character handling
    void processCrtChar2(uint8 byte);
    // lowest level crt character handling
    void processCrtChar3(uint8 byte);
    // prt character handling (only one level of interpretation)
    void processPrtChar2(uint8 byte);

    void adjustCursorY(int delta);      // advance the cursor in y
    void adjustCursorX(int delta);      // move cursor left or right

    // ---- state ----

    // display state and geometry
    CrtFrame     *m_wndhnd;         // opaque handle to UI window
    int           m_io_addr;        // associated I/O address
    int           m_term_num;       // associated terminal number
    crt_state_t   m_disp;           // contents of display memory

    // current character attributes
    int           m_attrs;          // current char attributes
    bool          m_attr_on;        // use attr bits until next 0F
    bool          m_attr_temp;      // use attr bits until next 0D or 0F
    bool          m_attr_under;     // draw underlined char if m_attr_on
    bool          m_box_bottom;     // true if we've seen at least one 0B

    // byte stream command interpretation
    bool          m_crt_sink;       // true=route to crt, false=to prt
    int           m_raw_cnt;        // raw input stream buffered until we have
    uint8         m_raw_buf[5];     // ... a complete runlength sequence
    int           m_input_cnt;      // buffered input stream while decoding
    uint8         m_input_buf[10];  // ... character sequence escapes

    // inline functions

    // set horizontal position
    inline void setCursorX(int x) { m_disp.curs_x = x; }
    // set vertical position
    inline void setCursorY(int y) { m_disp.curs_y = y; }

    // write 1 character to the video memory at location (x,y).
    // it is up to the caller to set the screen dirty flag.
    inline void scr_write_char(int x, int y, uint8 ch)
        { m_disp.display[m_disp.chars_w*y + x] = ch; }
    inline void scr_write_attr(int x, int y, uint8 attr)
        { m_disp.attr[m_disp.chars_w*y + x] = attr; }

    // set or clear a character cell line attribute bit
    inline void setBoxAttr(bool box_draw, uint8 attr, int y_adj=0) {
        if (box_draw) {
            m_disp.attr[80*(m_disp.curs_y+y_adj) + m_disp.curs_x] |=  attr;
        } else { // erase
            m_disp.attr[80*(m_disp.curs_y+y_adj) + m_disp.curs_x] &= ~attr;
        }
    }
};

#endif _INCLUDE_TERMINAL_H_

// vim: ts=8:et:sw=4:smarttab