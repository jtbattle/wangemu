// this class is responsible for modeling a 2236 or similar terminal.

#ifndef _INCLUDE_TERMINAL_H_
#define _INCLUDE_TERMINAL_H_

#include "Scheduler.h"
#include "TerminalState.h"
#include "w2200.h"

#include <queue>

class CrtFrame;
class Scheduler;
class Timer;
class IoCardTermMux;
enum ui_screen_t;

class Terminal
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Terminal);

    Terminal(std::shared_ptr<Scheduler> scheduler,
             IoCardTermMux *muxd,
             int io_addr, int term_num, ui_screen_t screen_type,
             bool vp_cpu);
    ~Terminal();

    // hardware reset
    void reset(bool hard_reset);

    // send a character to the display controller
    void processChar(uint8 byte);

    // character transmission time, in nanoseconds
    static const int64 serial_char_delay =
            TIMER_US(  11.0              /* bits per character */
                     * 1.0E6 / 19200.0   /* microseconds per bit */
                    );

private:
    // size of the FIFO holding keystrokes which are yet to be sent to the
    // host CPU. this is unlikely to ever be met except if the serial line
    // rate is given an option to be set to a low value.
    static const unsigned int KB_BUFF_MAX = 32;

    // size of the FIFO holding received characters which have yet to be
    // parsed and interpreted. in the real terminal, a Z80 CPU has to do
    // a certain amount of work for each character, and especially in the
    // case of decompressing runs, two received characters can turn into
    // 80 characters to get on the CRT with all attendant cursor tracking
    // and scrolling accounting.
    //
    // in the emulator, those activities are handled instantly, but there
    // is one case where delay is modeled: when the <FB><Cn> pair is received,
    // which is used to implement the effects of "SELECT Pn" delays.  because
    // the serial line is still active, the TX buffer can fill, which triggers
    // the terminal to perform flow control back to the host.  these flow
    // control characters have to be inserted at the head of the outbound
    // serial stream.
    //
    // 2536DWTerminalAndTerminalControllerProtocol.pdf describes the FIFO
    // sies and flow control thresholds.
    static const unsigned int CRT_BUFF_MAX = 196;  //  96 + 100 overrun
    static const unsigned int PRT_BUFF_MAX = 232;  // 132 + 100 overrun

    // this describes the state of the crt and prt flow control
    enum class flow_state_t {
        START,     // traffic is flowing; STOP has never been sent
        STOP_PEND, // waiting for an opportunity to send STOP command
        STOPPED,   // traffic should halt; STOP has been sent recently
        GO_PEND,   // waiting for an opportunity to send GO command
        GOING,     // traffic is flowing, but STOP was sent in the past
    };

    // ---- functions ----

    // reset crt/prt part of state
    void resetCrt();
    void resetPrt();

    // send an init sequence from emulated terminal shortly after reset
    void sendInitSeq();

    // process a key received from the associated Crt,
    // or when system2200 stuffs characters during script processing
    void receiveKeystroke(int keycode);

    // process the next entry in kb event queue and schedule a timer to
    // check for the next one
    void checkKbBuffer();

    // callback after a character has finished transmission
    void termToMxdCallback(int key);

    // callback after SELECT Pn timer expires
    void selectPCallback();

    // clear the display and home the cursor
    void clearScreen() noexcept;

    // scroll the contents of the screen up one row,
    // and fill the new row with blanks.
    void scrollScreen() noexcept;

    // receive queueing
    void crtCharFifo(uint8 byte);
    void prtCharFifo(uint8 byte);
    // drain pending rx characters
    void checkCrtFifo();
    void processCrtChar1(uint8 byte);
    void processCrtChar2(uint8 byte);
    void processCrtChar3(uint8 byte);

    void adjustCursorY(int delta) noexcept;  // advance the cursor in y
    void adjustCursorX(int delta) noexcept;  // move cursor left or right

    // ---- state ----
    std::shared_ptr<Scheduler> m_scheduler; // shared event scheduler
    IoCardTermMux *m_muxd;          // nullptr if dumb term
    const bool     m_vp_cpu;        // scripting throttle needs this info

    // display state and geometry
    CrtFrame     *m_wndhnd;         // opaque handle to UI window
    int           m_io_addr;        // associated I/O address
    int           m_term_num;       // associated terminal number
    crt_state_t   m_disp;           // contents of display memory
    std::shared_ptr<Timer> m_init_tmr;  // send init sequence from terminal
    bool          m_script_active;  // a script is feeding us keystrokes

    // current character attributes
    int           m_attrs;          // current char attributes
    bool          m_attr_on;        // use attr bits until next 0F
    bool          m_attr_temp;      // use attr bits until next 0D or 0F
    bool          m_attr_under;     // draw underlined char if m_attr_on
    bool          m_box_bottom;     // true if we've seen at least one 0B

    // byte stream command interpretation
    bool          m_escape_seen;    // escape (FB) received immediately prior
    bool          m_crt_sink;       // true=route to crt, false=to prt
    int           m_raw_cnt;        // raw input stream buffered until we have
    uint8         m_raw_buf[5];     // ... a complete runlength sequence
    int           m_input_cnt;      // buffered input stream while decoding
    uint8         m_input_buf[10];  // ... character sequence escapes

    // the terminal keyboard buffer is modeled in the card
    // instead of cluttering up the Ui code
    std::queue<uint8>      m_kb_buff;           // pending input
    std::deque<uint8>      m_kb_recent;         // recent history
    std::shared_ptr<Timer> m_tx_tmr;            // model uart rate & delay

    // crt receive buffer and flow control state
    std::queue<uint8>      m_crt_buff;
    flow_state_t           m_crt_flow_state;
    std::shared_ptr<Timer> m_crt_tmr;           // background crt-go timer
    std::shared_ptr<Timer> m_selectp_tmr;       // SELECT Pn timer

    // prt receive buffer and flow control state
    std::queue<uint8>      m_prt_buff;
    flow_state_t           m_prt_flow_state;
    std::shared_ptr<Timer> m_prt_tmr;           // background prt-go timer

    // ---- inline functions ----

    // set horizontal position
    inline void setCursorX(int x) noexcept { m_disp.curs_x = x; }
    // set vertical position
    inline void setCursorY(int y) noexcept { m_disp.curs_y = y; }

    // write 1 character to the video memory at location (x,y).
    // it is up to the caller to set the screen dirty flag.
    inline void screenWriteChar(int x, int y, uint8 ch) noexcept
        { m_disp.display[m_disp.chars_w*y + x] = ch; }
    inline void screenWriteAttr(int x, int y, uint8 attr) noexcept
        { m_disp.attr[m_disp.chars_w*y + x] = attr; }

    // set or clear a character cell line attribute bit
    inline void setBoxAttr(bool box_draw, uint8 attr, int y_adj=0) noexcept {
        if (box_draw) {
            m_disp.attr[80*(m_disp.curs_y+y_adj) + m_disp.curs_x] |=  attr;
        } else { // erase
            m_disp.attr[80*(m_disp.curs_y+y_adj) + m_disp.curs_x] &= ~attr;
        }
    }
};

#endif // _INCLUDE_TERMINAL_H_

// vim: ts=8:et:sw=4:smarttab
