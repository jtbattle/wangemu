// This file implements the Terminal class.
// It models either a dumb display controller or a smart terminal.
// Dumb controller:
//    poking character bytes in the right location
//    moving the cursor
//    clearing the screen
// Smart terminal:
//    same as dumb controller, plus
//    maintaining character attributes
//    doing box drawing
//    doing character stream parsing and decompression
//    remapping keyboard encoding to match 2236
//    modeling uart delay and rate limiting

#include "Ui.h"
#include "IoCardKeyboard.h"
#include "IoCardTermMux.h"
#include "Scheduler.h"
#include "System2200.h"
#include "Terminal.h"

// FIXME: this is here and in IoCardTermMux.cpp
//        put it somewhere in common
// character transmission time, in nanoseconds
const int64 serial_char_delay =
            TIMER_US(  11.0              /* bits per character */
                     * 1.0E6 / 19200.0   /* microseconds per bit */
                    );

// ----------------------------------------------------------------------------
// Terminal
// ----------------------------------------------------------------------------

static char id_string[] = "*2236DE R2016 19200BPS 8+O (USA)";

Terminal::Terminal(std::shared_ptr<Scheduler> scheduler,
                   IoCardTermMux *muxd,
                   int io_addr, int term_num, ui_screen_t screen_type) :
    m_scheduler(scheduler),
    m_muxd(muxd),
    m_io_addr(io_addr),
    m_term_num(term_num),
    m_init_tmr(nullptr),
    m_tx_tmr(nullptr)
{
    m_disp.screen_type = screen_type;
    m_disp.chars_w  = (screen_type == UI_SCREEN_64x16)  ? 64 : 80;
    m_disp.chars_h  = (screen_type == UI_SCREEN_64x16)  ? 16 : 24;
    m_disp.chars_h2 = (screen_type == UI_SCREEN_2236DE) ? 25 : m_disp.chars_h;

    reset(true);

    m_wndhnd = UI_displayInit(UI_SCREEN_2236DE, m_io_addr, m_term_num, &m_disp);
    assert(m_wndhnd);

    bool smart_term = (screen_type == UI_SCREEN_2236DE);
    if (smart_term) {
        // in dumb systems, the IoCardKeyboard will establish the callback
        System2200::registerKb(
            m_io_addr, m_term_num,
            std::bind(&Terminal::receiveKeystroke, this, std::placeholders::_1)
        );

        // A real 2336 sends the sequence E4 F8 about a second after
        // it powers up (the second is to run self tests).  E4 is the
        // BASIC atom code for INIT, but functionally I don't think it
        // does anything.  The F8 is the "crt go" flow control byte.
        // However, in a real system the CRT might be turned on before
        // the 2200 CPU is, and so the MXD wouldn't see the init sequence.
        // the 2336 sends an init sequence on reset.  wait a fraction of
        // a second to give the 8080 time to wake up before sending it.
        m_init_tmr = m_scheduler->TimerCreate(
                       TIMER_MS(500),
                       std::bind(&Terminal::SendInitSeq, this)
                     );
    }
}


// free resources on destruction
Terminal::~Terminal()
{
    bool smart_term = (m_disp.screen_type == UI_SCREEN_2236DE);
    if (smart_term) {
        System2200::unregisterKb(m_io_addr, m_term_num);
    }

    UI_displayDestroy(m_wndhnd);
    m_wndhnd = nullptr;
}


// ----------------------------------------------------------------------------
// public member functions
// ----------------------------------------------------------------------------

// reset the crt
// hard_reset=true means
//     power on reset
// hard_reset=false means
//     user pressed the SHIFT-RESET sequence or
//     the terminal received a programmatic reset sequence
void
Terminal::reset(bool hard_reset)
{
    bool smart_term = (m_disp.screen_type == UI_SCREEN_2236DE);

    // dumb CRT controllers don't independently get reset;
    // the host CPU tells it to clear.  smart terminals
    // will independently clear the screen even if the serial
    // line is disconnected.
    if (hard_reset || smart_term) {
        // dumb/smart terminal state:
        m_disp.curs_x     = 0;
        m_disp.curs_y     = 0;
        m_disp.curs_attr  = cursor_attr_t::CURSOR_ON;
        m_disp.dirty      = true;  // must regenerate display
        scr_clear();

        // smart terminal state:
        m_crt_sink   = true;
        m_raw_cnt    = 0;
        m_input_cnt  = 0;
        for(auto &byte : m_raw_buf)   { byte = 0x00; }  // not strictly necessary
        for(auto &byte : m_input_buf) { byte = 0x00; }  // not strictly necessary

        m_tx_tmr = nullptr;
        m_kb_buff = {};

        m_attrs      = char_attr_t::CHAR_ATTR_BRIGHT; // implicitly primary char set
        m_attr_on    = false;
        m_attr_temp  = false;
        m_attr_under = false;
        m_box_bottom = false;
    }

    // smart terminals echo the ID string to the CRT at power on
    if (smart_term && hard_reset) {
        char *idptr = &id_string[1];  // skip the leading asterisk
        while (*idptr) {
            processCrtChar3(*idptr);
            idptr++;
        }
        processCrtChar3(0x0D);
        processCrtChar3(0x0A);
    }
}


// ----------------------------------------------------------------------------
// terminal to mxd communication channel
// ----------------------------------------------------------------------------

// The 2336 sends an init sequence of E4 F8 on power up.
// The emulator doesn't send E4 because it sometimes shows up as a
// spurious "INIT" keyword. It isn't clear what the E4 is for.
void
Terminal::SendInitSeq()
{
    m_init_tmr = nullptr;
//  m_kb_buff.push(static_cast<uint8>(0xE4));
    m_kb_buff.push(static_cast<uint8>(0xF8));
    checkKbBuffer();
}


// the received keyval is appropriate for the first generation keyboards,
// but the smart terminals:
//   (a) couldn't send an IB9 (9th bit) per key, and
//   (b) certain keys were coded differently, including as a pair of bytes
// as smart terminals were connected via 19200 baud serial lines, we
// model the transport delay by firing a timer for that long which disallows
// sending any more keys for that long.  if any key arrives while the timer is
// active, store it in a fifo and send the next one when the timer expires.
void
Terminal::receiveKeystroke(int keycode)
{
    if (m_kb_buff.size() >= KB_BUFF_MAX) {
        UI_Warn("the terminal keyboard buffer dropped a character");
        return;
    }

    // remap certain keycodes from first-generation encoding to what is
    // send over the serial line
    if (keycode == IoCardKeyboard::KEYCODE_RESET) {
        reset(false);  // clear screen, home cursor, and empty fifos
        m_kb_buff.push(static_cast<uint8>(0x12));
    } else if (keycode == IoCardKeyboard::KEYCODE_HALT) {
        // halt/step
        m_kb_buff.push(static_cast<uint8>(0x13));
    } else if (keycode == (IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT)) {
        // edit
        m_kb_buff.push(static_cast<uint8>(0xBD));
    } else if (keycode & IoCardKeyboard::KEYCODE_SF) {
        // special function
        m_kb_buff.push(static_cast<uint8>(0xFD));
        m_kb_buff.push(static_cast<uint8>(keycode & 0xff));
    } else if (keycode == 0xE6) {
        // the pc TAB key maps to "STMT" in 2200T mode,
        // but it maps to "FN" (function) in 2336 mode
        m_kb_buff.push(static_cast<uint8>(0xFD));
        m_kb_buff.push(static_cast<uint8>(0x7E));
    } else if (keycode == 0xE5) {
        // erase
        m_kb_buff.push(static_cast<uint8>(0xE5));
    } else if (0x80 <= keycode && keycode < 0xE5) {
        // it is an atom; add prefix
        m_kb_buff.push(static_cast<uint8>(0xFD));
        m_kb_buff.push(static_cast<uint8>(keycode & 0xff));
    } else {
        // the mapping is unchanged
        assert(keycode == (keycode & 0xff));
        m_kb_buff.push(static_cast<uint8>(keycode));
    }

    checkKbBuffer();
}


// process the next entry in kb event queue and schedule a timer to check
// for the next one
void
Terminal::checkKbBuffer()
{
    if (m_kb_buff.empty() || m_tx_tmr) {
        // nothing to do or serial channel is in use
        return;
    }

    uint8 key = m_kb_buff.front();
    m_kb_buff.pop();

    m_tx_tmr = m_scheduler->TimerCreate(
                   serial_char_delay,
                   std::bind(&Terminal::termToMxdCallback, this, key)
               );
}


// callback after a character has finished transmission
void
Terminal::termToMxdCallback(int key)
{
    m_tx_tmr = nullptr;

// FIXME: here is the bogus stuff.  m_term_num=1 for the first term
//        but the receiver is 0-based, not 1-based
//  m_muxd->receiveKeystroke(m_term_num, key);
    m_muxd->receiveKeystroke(0, key);

    // see if any other chars are pending
    checkKbBuffer();
}


// ----------------------------------------------------------------------------
// crt character processing
// ----------------------------------------------------------------------------

// advance the cursor in y
void
Terminal::adjustCursorY(int delta)
{
    m_disp.curs_y += delta;
    if (m_disp.curs_y >= m_disp.chars_h) {
        m_disp.curs_y = m_disp.chars_h-1;
        if (m_disp.screen_type != UI_SCREEN_2236DE) {
            m_disp.curs_x = 0;   // yes, scrolling has this effect
        }
        scr_scroll();
    } else if (m_disp.curs_y < 0) {
        m_disp.curs_y = m_disp.chars_h-1;     // wrap around
    }
}


// move cursor left or right
void
Terminal::adjustCursorX(int delta)
{
    m_disp.curs_x += delta;
    if (m_disp.curs_x >= m_disp.chars_w) {
        m_disp.curs_x = 0;
    } else if (m_disp.curs_x < 0) {
        m_disp.curs_x = m_disp.chars_w - 1;
    }
}


// clear the display; home the cursor
void
Terminal::scr_clear()
{
    char fillval = ' ';

#if 1
    for(auto &byte : m_disp.display) { byte = fillval; }
#else
    for(int y=0; y<m_disp.chars_h; y++) {
    for(int x=0; x<m_disp.chars_w; x++) {
        scr_write_char(x, y, fillval);
    }}
#endif

#if 1
    for(auto &byte : m_disp.attr) { byte = 0x00; }
#else
    if (m_disp.screen_type == UI_SCREEN_2236DE) {
        uint8 attr = 0;
        for(int y=0; y<25; y++) {
        for(int x=0; x<80; x++) {
            scr_write_attr(x, y, attr);
        }}
    }
#endif

    setCursorX(0);
    setCursorY(0);
}


// scroll the contents of the screen up one row, and fill the new
// row with blanks.
void
Terminal::scr_scroll()
{
    uint8 *d  = m_disp.display;                     // first char of row 0
    uint8 *s  = d + m_disp.chars_w;                 // first char of row 1
    uint8 *d2 = d + m_disp.chars_w*(m_disp.chars_h2-1);  // first char of last row

    // scroll up the data
    (void)memcpy(d, s, (m_disp.chars_h2-1)*m_disp.chars_w);
    // blank the last line
    (void)memset(d2, ' ', m_disp.chars_w);

    // cake care of the attribute plane
    if (m_disp.screen_type == UI_SCREEN_2236DE) {
        d  = m_disp.attr;                             // first char of row 0
        s  = d + m_disp.chars_w;                      // first char of row 1
        d2 = d + m_disp.chars_w*(m_disp.chars_h2-1);  // first char of last row
        uint8 attr_fill = 0;

        // scroll up the data
        (void)memcpy(d, s, (m_disp.chars_h2-1)*m_disp.chars_w);
        // blank the last line
        (void)memset(d2, attr_fill, m_disp.chars_w);
    }
}


// send a byte to the display controller.
// for dumb terminals, there are a handful control codes which have
// side effects, but most characters are simply poked into the current
// cursor location and the cursor advances.
//
// But the 2236DE recognizes certain bytes as escapes which trigger
// a command sequence or more complex behavior.  At the top level there
// is the unpacking of compressed character sequence (run length encoded).

// first level: FB-escape sequences (mostly char run decompression)
// some of the escape sequence information was gleaned from this document:
//     2536DWTerminalAndTerminalControllerProtocol.pdf
void
Terminal::processChar(uint8 byte)
{
    auto handler = (m_crt_sink) ? std::mem_fn(&Terminal::processCrtChar2)
                                : std::mem_fn(&Terminal::processPrtChar2);

    //dbglog("Terminal::processChar(0x%02x), m_raw_cnt=%d\n", byte, m_raw_cnt);
    if (m_disp.screen_type != UI_SCREEN_2236DE) {
        processCrtChar3(byte);
        return;
    }
    //wxLogMessage("processChar(0x%02x)", byte);

    if ((m_raw_cnt == 0) && (byte == 0xFB)) {
        // start a FB ... sequence
        m_raw_buf[0] = 0xFB;
        m_raw_cnt = 1;
        return;
    }
    if (m_raw_cnt == 0) {
        // pass it through
        handler(this, byte);
        return;
    }

    // keep accumulating command bytes
    assert((0 <= m_raw_cnt) && (m_raw_cnt < sizeof(m_raw_buf)));
    m_raw_buf[m_raw_cnt++] = byte;

    // ------------ immediate commands ------------
    // sequences FBF0, FBF1, FBF2, FBF6 are "immediate", meaning they
    // are not put in the input FIFO and take effect as soon as they are
    // received.  According to the documentation,
    //
    //     Immediate commands may even be transmitted in the middle of a
    //     non-immediate command sequence:
    //         <escape><count> (normally the repeat char arrives next)
    //         <escape><switch to printer><printer data...><escape><switch to crt>
    //         <repeat character>
    //
    // Because of this, we have to look at the last two bytes.

    if (m_raw_buf[m_raw_cnt-2] == 0xFB) {

        // immediate command: route to crt (FB F0)
        if (m_raw_buf[m_raw_cnt-1] == 0xF0) {
            m_crt_sink = true;
            m_raw_cnt -= 2;
            return;
        }

        // immediate command: route to prt (FB F1)
        if (m_raw_buf[m_raw_cnt-1] == 0xF1) {
            m_crt_sink = false;
            m_raw_cnt -= 2;
            return;
        }

        // immediate command: restart terminal (FB F2)
        // a real 2336 sends E4 (??) then F8 (crt go flow control)
        // then F8 every three seconds while not throttled.
        if (m_raw_buf[m_raw_cnt-1] == 0xF2) {
            reset(false);
#if 0
            // if I include this, the emulator thinks it got the INIT atom (E4)
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xE4);
#endif
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xF8);
            // FIXME: then F8 every 3 seconds if not throttled
            return;
        }

        // immediate command: reset crt (FB F6)
        // a real 2336 sends E9 (crt stop flow control),
        // then F8 (crt go flow control), then another F8, then E4 (??),
        // then F8 every three seconds while not throttled.
        //
        // Fasstcom Terminal Spec v1.0.pdf, pdf page 22 says
        // ... clear terminal data ONLY from receive buffer ...
        // TODO: if true, I'm not doing that, as reset() wipes everything.
        if (m_raw_buf[m_raw_cnt-1] == 0xF6) {
            reset(false);
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xF9);
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xF8);
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xF8);
#if 0
            // if I include this, the emulator thinks it got the INIT atom (E4)
            System2200::kb_keystroke(m_io_addr, m_term_num, 0xE4);
#endif
            // FIXME: then F8 every 3 seconds if not throttled
            return;
        }
    }

    // ------------ other sequences ------------

    // it is a character run sequence: FB nn cc
    // where nn is the repetition count, and cc is the character
    if (m_raw_cnt == 3) {
        //dbglog("Decompress run: cnt=%d, chr=0x%02x\n", m_raw_buf[1], m_raw_buf[2]);
        for(int i=0; i<m_raw_buf[1]; i++) {
            handler(this, m_raw_buf[2]);
        }
        m_raw_cnt = 0;
        return;
    }

    // this must be the case we are considering at this point
    assert(m_raw_cnt == 2);

    // FB nn, where 0x02 < count < 0x60, is a three byte sequence
    if ((0x02 < m_raw_buf[1]) && (m_raw_buf[1] < 0x60)) {
        return;
    }

    // FB nn, where nn >= 0x60 represents (nn-0x60) spaces in a row
    if ((0x60 <= m_raw_buf[1]) && (m_raw_buf[1] <= 0xBF)) {
        //dbglog("Decompress spaces: cnt=%d\n", m_raw_buf[1]-0x60);
        for(int i=0; i<m_raw_buf[1]-0x60; i++) {
            handler(this, static_cast<uint8>(0x20));
        }
        m_raw_cnt = 0;
        return;
    }

    // check for delay sequence: FB Cn
    if ((0xC1 <= m_raw_buf[1]) && (m_raw_buf[1] <= 0xC9)) {
        int delay_ms = 1000 * ((int)m_raw_buf[1] - 0xC0) / 6;
        // FIXME: doing this requires creating a flow control mechanism
        // to block further input, and creating a timer to expire N/6 seconds
        //dbglog("Delay sequence: cnt=%d\n", m_raw_buf[1]);
        delay_ms = delay_ms;  // defeat linter
        m_raw_cnt = 0;
        return;
    }

    // check for literal 0xFB byte: FB D0
    if (m_raw_buf[1] == 0xD0) {
        //dbglog("Literal 0xFB byte\n");
        handler(this, static_cast<uint8>(0xFB));
        m_raw_cnt = 0;
        return;
    }

    // disable cursor blink (FB F8)
    // turning off blink does not re-enable the cursor
    if (m_raw_buf[1] == 0xF8) {
        if (m_disp.curs_attr == cursor_attr_t::CURSOR_BLINK) {
            m_disp.curs_attr = cursor_attr_t::CURSOR_ON;
        }
        m_raw_cnt = 0;
        return;
    }

    // enable cursor blink (FB FC)
    // if the cursor was off to begin with, it remains off
    // Fasstcom Terminal Spec v1.0.pdf, pdf page 22 says FBF4
    // is a synonym for FBFC, so I've implemented it.
    if ((m_raw_buf[1] == 0xF4) || (m_raw_buf[1] == 0xFC)) {
        if (m_disp.curs_attr == cursor_attr_t::CURSOR_ON) {
            m_disp.curs_attr = cursor_attr_t::CURSOR_BLINK;
        }
        m_raw_cnt = 0;
        return;
    }

    // TODO: what should happen with illegal sequences?
    // for now, I'm passing them through
    //dbglog("Unexpected sequence: 0x%02x 0x%02x\n", m_raw_buf[0], m_raw_buf[1]);
    handler(this, m_raw_buf[0]);
    handler(this, m_raw_buf[1]);
    m_raw_cnt = 0;
}


// second level command stream interpretation, for 2236DE type
void
Terminal::processCrtChar2(uint8 byte)
{
    assert(m_disp.screen_type == UI_SCREEN_2236DE);
    //wxLogMessage("processCrtChar2(0x%02x), m_input_cnt=%d", byte, m_input_cnt);

    assert( m_input_cnt < sizeof(m_input_buf) );

    if (m_input_cnt == 0) {
        switch (byte) {
        case 0x02:  // character attribute, draw/erase box
            m_input_buf[0] = byte;
            m_input_cnt = 1;
            return;
        case 0x0D:  // carriage return
            // this has the side effect of disabling attributes if in temp 0E mode
            m_attr_temp = false;
            processCrtChar3(0x0D);
            return;
        case 0x0E:  // enable attributes
            m_attr_on   = false;  // after 04 xx yy 0E, 0E changes to temp mode?
            m_attr_temp = true;
//          UI_Info("attrs: 0E --> %02x, %d", m_attrs, m_attr_under);
            return;
        case 0x0F:  // disable attributes
            m_attr_on   = false;
            m_attr_temp = false;
//          UI_Info("attrs: 0F");
            return;
        default:
            // pass through
            processCrtChar3(byte);
            return;
        }
    }

    // accumulate this byte on the current command string
    m_input_buf[m_input_cnt++] = byte;

    // look for completed 02 ... command sequences
    assert(m_input_cnt > 0 && m_input_buf[0] == 0x02);

    // cursor blink enable: 02 05 0F
    if (m_input_cnt == 3 && m_input_buf[1] == 0x05
                         && m_input_buf[2] == 0x0F) {
        m_disp.curs_attr = cursor_attr_t::CURSOR_BLINK;
        m_input_cnt = 0;
        return;
    }
    // unrecognized 02 05 xx sequence
    if (m_input_cnt == 3 && m_input_buf[1] == 0x05) {
        // TODO: just ignore?
        m_input_cnt = 0;
        return;
    }

    // normal character set: 02 02 00 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02
                         && m_input_buf[2] == 0x00
                         && m_input_buf[3] == 0x0F) {
        m_attrs &= ~(char_attr_t::CHAR_ATTR_ALT);
        m_input_cnt = 0;
        return;
    }
    // alternate character set: 02 02 02 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02
                         && m_input_buf[2] == 0x02
                         && m_input_buf[3] == 0x0F) {
        m_attrs |= (char_attr_t::CHAR_ATTR_ALT);
        m_input_cnt = 0;
        return;
    }
    // unrecognized 02 02 xx yy sequence
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02) {
        // TODO: just ignore?
        m_input_cnt = 0;
        return;
    }

    // defined attributes, possibly enabling them: 02 04 xx yy {0E|0F}
    if (m_input_cnt == 3 && m_input_buf[1] == 0x04) {
        if (m_input_buf[2] != 0x00 && m_input_buf[2] != 0x02 &&
            m_input_buf[2] != 0x04 && m_input_buf[2] != 0x0B) {
            m_input_cnt = 0;  // TODO: ignore?
        }
        return;
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x04) {
        if (m_input_buf[3] != 0x00 && m_input_buf[3] != 0x02 &&
            m_input_buf[3] != 0x04 && m_input_buf[3] != 0x0B) {
            m_input_cnt = 0;  // TODO: ignore?
        }
        return;
    }
    if (m_input_cnt == 5 && m_input_buf[1] == 0x04) {
        m_input_cnt = 0;
        if (m_input_buf[4] != 0x0E && m_input_buf[4] != 0x0F) {
            return;  // TODO: ignore?
        }
        m_attrs &= ~( char_attr_t::CHAR_ATTR_BRIGHT |
                      char_attr_t::CHAR_ATTR_BLINK  |
                      char_attr_t::CHAR_ATTR_INV    );
        m_attr_under = false;
        if (m_input_buf[2] == 0x02 || m_input_buf[2] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_BRIGHT);
        }
        if (m_input_buf[2] == 0x04 || m_input_buf[2] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_BLINK);
        }
        if (m_input_buf[3] == 0x02 || m_input_buf[3] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_INV);
        }
        if (m_input_buf[3] == 0x04 || m_input_buf[3] == 0x0B) {
            m_attr_under = true;
        }
        m_attr_on   = (m_input_buf[4] == 0x0E);
        m_attr_temp = false;
//      UI_Info("attrs: 02 04 %02x %02x %02x --> %02x, %d", m_input_buf[2], m_input_buf[3], m_input_buf[4], m_attrs, m_attr_under);
        return;
    }

    // return self-ID string: 02 08 09 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x08
                         && m_input_buf[2] == 0x09
                         && m_input_buf[3] == 0x0F) {
        m_input_cnt = 0;
        char *idptr = &id_string[1];  // skip the leading asterisk
        while (*idptr) {
            System2200::kb_keystroke(m_io_addr, m_term_num, *idptr);
            idptr++;
        }
        System2200::kb_keystroke(m_io_addr, m_term_num, 0x0D);
        return;
    }
    // 02 08 xx yy is otherwise undefined
    if (m_input_cnt == 4 && m_input_buf[1] == 0x08) {
        m_input_cnt = 0;
        return;  // TODO: ignore it?
    }

    // draw/erase box mode
    // the implementation is hinky: keep the box mode prefix then erase the
    // most recent box mode "verb" after performing it, since the total box
    // command string can be very long, and there is no need to keep all of it.
    if (m_input_cnt == 3 && m_input_buf[1] == 0x0B) {
        if (m_input_buf[2] != 0x02 && m_input_buf[2] != 0x0B) {
            // must start either 02 0B 02, or 02 0B 0B
            m_input_cnt = 0;
            return;  // TODO: ignore it?
        }
        // this flag is used during 08 sub-commands
        // we draw the bottom only if we've seen a previous 0B command
        m_box_bottom = false;
        return;
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x0B) {
        bool box_draw = (m_input_buf[2] == 0x02);
        m_input_cnt--;  // drop current command byte
        switch (byte) {
            case 0x08: // move left
                // draw top left  side of char below old position
                // draw top right side of char below new position
                if (m_box_bottom) {
                    setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_LEFT, +1);
                }
                adjustCursorX(-1);
                if (m_box_bottom) {
                    setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_RIGHT, +1);
                }
                return;
            case 0x09: // move right
                // draw top right side at old position
                // draw top left  side at new position
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_RIGHT);
                adjustCursorX(+1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_LEFT);
                return;
            case 0x0A: // move down one line and draw vertical line
                adjustCursorY(+1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                return;
            case 0x0B: // draw vertical line at current position
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                m_box_bottom = true;  // subsequent 08 must draw bottom
                return;
            case 0x0C: // draw vertical line at current position
                adjustCursorY(-1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                return;
            case 0x0F: // end of box mode
                m_input_cnt = 0;
                return;
            default:
                // TODO: just ignore error and drop out of box mode?
                m_input_cnt = 0;
                return;
        }
    }

    // reinitialize terminal: 02 0D 0C 03 0F
    // 2336DW_InteractiveTerminalUserManual.700-7636.11-82.pdf says this:
    //   1. Clears the screen, homes the cursor and turns the cursor on
    //   2. Selects normal intensity characters
    //   3. Selects bright as attribute to be activated by HEX(0E)
    //   4. Select the default character set for that version of terminal
    if (m_input_cnt == 3 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] != 0x0C) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] == 0x0C
                         && m_input_buf[3] != 0x03) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
    if (m_input_cnt == 5 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] == 0x0C
                         && m_input_buf[3] == 0x03
                         && m_input_buf[4] == 0x0F) {
        // logging real 2336, it clears the CRT
        // but doesn't spew out any return code
        m_input_cnt = 0;
        reset(false);
        return;
    }

    if (m_input_cnt >= 5) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
}


// lowest level command stream interpretation
void
Terminal::processCrtChar3(uint8 byte)
{
    // true of old 64x16 display controller, which had only 7b buffer
    // byte &= 0x7F;

    switch (byte) {

        case 0x00: case 0x02: case 0x04:
        case 0x0B: case 0x0E: case 0x0F:
            // ignored
            break;

        case 0x01:      // home screen
            setCursorX(0);
            setCursorY(0);
            break;

        case 0x03:      // clear screen
            scr_clear();
            break;

        case 0x05:      // enable cursor
            m_disp.curs_attr = cursor_attr_t::CURSOR_ON;
            break;

        case 0x06:      // disable cursor
            m_disp.curs_attr = cursor_attr_t::CURSOR_OFF;
            break;

        case 0x07:      // bell
            UI_displayDing(m_wndhnd);
            break;

        case 0x08:      // cursor left
            adjustCursorX(-1);
            break;

        case 0x09:      // horizontal tab
            adjustCursorX(+1);
            break;

        case 0x0A:      // linefeed (go down one column)
            adjustCursorY(+1);
            break;

        case 0x0C:      // reverse index (cursor up)
            adjustCursorY(-1);
            break;

        case 0x0D:      // go to first column
            setCursorX(0);
            break;

        default:        // just a character
            assert(byte >= 0x10);
#if 0
            // this is true for the old 64x16 display generator, not 80x24
            if (0x10 <= byte && byte <= 0x1F) {
                byte = byte + 0x40;     // 0x10 aliases into 0x50, etc.
            }
#endif
            bool use_alt_char = (byte >= 0x80)
                             && (m_attrs & char_attr_t::CHAR_ATTR_ALT);

            bool use_underline = ((byte >= 0x90) && !use_alt_char)
                              || (m_attr_under && (m_attr_on || m_attr_temp));

            byte = (byte & 0x7F)
                 | ((use_underline) ? 0x80 : 0x00);

            scr_write_char(m_disp.curs_x, m_disp.curs_y, byte);

            // update char attributes in screen buffer
            int old = m_disp.attr[m_disp.chars_w*m_disp.curs_y + m_disp.curs_x]
                    & ( char_attr_t::CHAR_ATTR_LEFT  |
                        char_attr_t::CHAR_ATTR_RIGHT |
                        char_attr_t::CHAR_ATTR_VERT  );

            int attr_mask = 0;
            if (!m_attr_on && !m_attr_temp) {
                attr_mask |= char_attr_t::CHAR_ATTR_BLINK
                          |  char_attr_t::CHAR_ATTR_BRIGHT
                          |  char_attr_t::CHAR_ATTR_INV;
            }
            if (!use_alt_char) {
                attr_mask |= char_attr_t::CHAR_ATTR_ALT;
            }

            scr_write_attr(m_disp.curs_x, m_disp.curs_y,
                           static_cast<uint8>(old | (m_attrs & ~attr_mask)));
            adjustCursorX(+1);
            break;
    }

    m_disp.dirty = true;
}


// second level prt command stream interpretation, for 2236DE type
void
Terminal::processPrtChar2(uint8 byte)
{
#if 0
    // FIXME: connect a printer
    UI_Info("term %d printer received 0x%02x", m_term_num, byte);
#else
    m_term_num = m_term_num;
    byte = byte;
#endif
}

// vim: ts=8:et:sw=4:smarttab
