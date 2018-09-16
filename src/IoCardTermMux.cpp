// The MXD Terminal Mux card contains an 8080, some EPROM and some RAM,
// along with four RS-232 ports.  Rather than emulating this card at
// the chip level, it is a functional emulation, based on the description
// of the card in 2236MXE_Documentation.8-83.pdf
//
// Also, look at my partial, guess-filled disassembly of the MUXD eproms here:
// C:/Users/Jim/Documents/wang2200/boards/LVP\ box/7291_dasm/asm/mxd.asm

#include "Ui.h"
#include "IoCardTermMux.h"
#include "System2200.h"
#include "Cpu2200.h"

#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif

const bool dbg = false; // temporary debugging hack

// instance constructor
IoCardTermMux::IoCardTermMux(std::shared_ptr<Cpu2200> cpu,
                             int baseaddr, int cardslot) :
    m_cpu(cpu),
    m_baseaddr(baseaddr),
    m_slot(cardslot),
    m_selected(false),
    m_cpb(true),
    m_card_busy(false),
    m_port(0),
    m_term(m_terms[0])
{
    if (m_slot < 0) {
        // this is just a probe to query properties, so don't make a window
        return;
    }

    int io_addr;
    bool ok = System2200().getSlotInfo(cardslot, 0, &io_addr);
    assert(ok); ok=ok;

    for(auto &t : m_terms) {
        t.wndhnd = nullptr;
    }

    // FIXME: just one for now
    m_terms[0].wndhnd = UI_initCrt(UI_SCREEN_2236DE, io_addr, 1,
        bind(&IoCardTermMux::receiveKeystroke, this, std::placeholders::_1));

    reset(true);
}

// instance destructor
IoCardTermMux::~IoCardTermMux()
{
    if (m_slot >= 0) {
        // not just a temp object, so clean up
        reset(true);        // turns off handshakes in progress
        for(auto &t : m_terms) {
            if (t.wndhnd != nullptr) {
                UI_destroyCrt(t.wndhnd);
                t.wndhnd = nullptr;
            }
        }
    }
}

const std::string
IoCardTermMux::getDescription() const
{
    return "Terminal Mux";
}

const std::string
IoCardTermMux::getName() const
{
    return "2236 MXD";
}

// return a list of the various base addresses a card can map to
// the default comes first.
std::vector<int>
IoCardTermMux::getBaseAddresses() const
{
    std::vector<int> v { 0x000, 0x040, 0x080, 0x0c0 };
    return v;
}

// return the list of addresses that this specific card responds to
std::vector<int>
IoCardTermMux::getAddresses() const
{
    std::vector<int> v;
    for(int i=1; i<8; i++) {
        v.push_back( m_baseaddr + i );
    }
    return v;
}

void
IoCardTermMux::reset(bool hard_reset)
{
    // reset card state
    m_selected   = false;
    m_cpb        = true;   // CPU busy
    m_card_busy  = false;
    m_vp_mode    = true;
    m_port       = 0;      // 0=default selected terminal (logical term #1)
    m_term       = m_terms[m_port];

// FIXME: I don't want to duplicate all the logic from IoCardKeyboard.cpp.
//        Restructure things so that logic can be shared.
    for(auto &t : m_terms) {
        t.io1_key_ready = false;   // no pending keys
        t.crt_buf.clear();
        t.printer_buf.clear();
        t.line_req_buf.clear();
        t.keyboard_buf.clear();
        t.command_buf.clear();
    }

    hard_reset = hard_reset;    // silence lint
}

void
IoCardTermMux::select()
{
    m_io_offset = (m_cpu->getAB() & 7);

    if (NOISY) {
        UI_Info("TermMux ABS %02x+%1x", m_baseaddr, m_io_offset);
    }
if (dbg) dbglog("TermMux ABS %02x+%1x\n", m_baseaddr, m_io_offset);

    // if the card is ever addressed at 01 or 05, the controller drops back
    // into vp mode.  likewise, if the card is addressed at 02, 06, or 07,
    // the controller is in mvp mode. (2236MXE_Documentation.8-83.pdf, p4)
    // Addressing 04 (printer port) doesn't change vp/mvp mode.
    bool vp_mode_next = (m_io_offset == 1) || (m_io_offset == 5)
                     || (m_io_offset == 4 && m_vp_mode);
    if (!m_vp_mode && !vp_mode_next) {
        // leaving mvp mode for vp mode; reset various state
        reset(false);
    }
    m_vp_mode = vp_mode_next;

    m_selected = true;

// FIXME: the busy state depends on which IO address we have!
// in the actual controller card, there is a 7b latch, one bit corresponding
// to ready/busy of each I/O address.  hardware muxes out the bit corresponding
// to the current address.  perhaps emulation should do the same.
    switch (m_io_offset) {
        case 1:  // vp mode keyboard
            check_keyready();
            m_card_busy = !m_term.io1_key_ready;
            break;
        case 2:  // status of all terminals (input only)
            m_ibs_seq = 0;
            m_card_busy = false;
            break;
        case 3:  // mxd: unused; mxe: receive remote screen dump from terminal
            m_card_busy = false;  // FIXME
            break;
        case 4: // send chars to current terminal's remote printer
            m_card_busy = (m_term.printer_buf.size() >= PrinterBufferSize-3);
            break;
        case 5:  // vp mode display
            m_card_busy = false;  // FIXME
            break;
        case 6:  // send command sequence; some commands return status here
            m_card_busy = false;  // FIXME
            break;
        case 7:  // send chars to current terminal's CRT
            m_card_busy = (m_term.crt_buf.size() >= CrtBufferSize-3);
            break;
    }
    m_cpu->setDevRdy(!m_card_busy);
}

void
IoCardTermMux::deselect()
{
    if (NOISY) {
        UI_Info("TermMux -ABS %02x+%1x", m_baseaddr, m_io_offset);
    }
if (dbg) dbglog("TermMux -ABS %02x+%1x\n", m_baseaddr, m_io_offset);

    m_selected = false;
    m_cpb      = true;
}

void
IoCardTermMux::OBS(int val)
{
    val &= 0xFF;
    if (NOISY) {
        UI_Info("TermMux OBS: byte 0x%02x", val);
    }

    // NOTE: the hardware latches m_io_offset into another latch now.
    // I believe the reason is that say the board is addressed at offset 6.
    // Then it does an OBS(0Xwhatever) in some fire and forget command.
    // It may take a while to process that OBS, but in the mean time,
    // the host computer may readdress the board at, say, offset 2.
    //
    // I'm not sure if the emulation needs that yet.

    switch (m_io_offset) {
        case 1: OBS_01(val); break;
        case 2: OBS_02(val); break;
        case 3: OBS_03(val); break;
        case 4: OBS_04(val); break;
        case 5: OBS_05(val); break;
        case 6: OBS_06(val); break;
        case 7: OBS_07(val); break;
    }

    m_cpu->setDevRdy(!m_card_busy);
}

void
IoCardTermMux::CBS(int val)
{
    val &= 0xFF;
    if (NOISY) {
        UI_Info("TermMux CBS: 0x%02x", val);
    }

    // some commands expect an ibs return sequence;
    // this state tracks how far in the response sequence
    m_ibs_seq = 0;

    // NOTE: the hardware latches m_io_offset into another latch now.
    // see the explanation in ::OBS()

    switch (m_io_offset) {
        case 1: CBS_01(val); break;
        case 2: CBS_02(val); break;
        case 3: CBS_03(val); break;
        case 4: CBS_04(val); break;
        case 5: CBS_05(val); break;
        case 6: CBS_06(val); break;
        case 7: CBS_07(val); break;
    }
}

// weird hack Wang used to signal the attached display is 64x16 (false)
// or 80x24 (true).  All smart terminals are 80x24, but in boot mode/vp mode,
// the term mux looks like a dumb terminal at 05, so it drives this to let
// the ucode know it is 80x24.
int
IoCardTermMux::getIB5() const
{
    return (m_io_offset == 5);
}

// change of CPU Busy state
void
IoCardTermMux::CPB(bool busy)
{
    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (NOISY) {
        UI_Info("TermMux CPB%c", busy ? '+' : '-');
    }

    m_cpb = busy;
    m_cpu->setDevRdy(!m_card_busy);

    if (!busy) {
        // the CPU is waiting for an IBS (input byte strobe)
        switch (m_io_offset) {
            case 1: IBS_01(); break;
            case 2: IBS_02(); break;
            case 6: IBS_06(); break;
            default:          break;
        }
    }
}

// FIXME: warmed over from IoCardKeyboard
void
IoCardTermMux::check_keyready()
{
//  script_poll();
    if (m_selected) {
//      if (m_term.key_ready && !m_cpb) {
//          // we can't return IBS right away -- apparently there
//          // must be some delay otherwise the handshake breaks
//          if (!m_tmr_script) {
//              m_tmr_script = m_scheduler->TimerCreate(
//                      TIMER_US(50),     // 30 is OK, 20 is too little
//                      [&](){ tcbScript(); } );
//          }
//      }
        m_cpu->setDevRdy(m_term.io1_key_ready);
    }
}

void
IoCardTermMux::receiveKeystroke(int keycode)
{
    // FIXME: warmed over from IoCardKeyboard
    assert( (keycode >= 0) );

    // halt acts independently of addressing in vp mode
    // in mvp mode, the halt status is reported via port 02
    if ((keycode & KEYCODE_HALT) && m_vp_mode) {
//      if (tthis->m_script_handle) {
//          // cancel any script in progress
//          tthis->m_tmr_script = nullptr;
//          tthis->m_script_handle = nullptr;
//          tthis->m_01_key_ready  = false;
//      }
        m_cpu->halt();
        return;
    }

    // ignore subsequent keys if one is already pending
    if (m_vp_mode && m_term.io1_key_ready) {
        return;
    }

//  if (tthis->m_script_handle) {
//      return;         // ignore any other keyboard if script is in progress
//  }

    if (m_io_offset == 1) {
        // let CPU know we have a key
        m_card_busy = false;
        m_cpu->setDevRdy(!m_card_busy);
    }
    if (m_cpb) {
        // store it until CPU is ready for it
        m_term.io1_key_code  = keycode;
        m_term.io1_key_ready = true;
    } else {
        // cpu is waiting for input, so send it now
        m_cpu->IoCardCbIbs(keycode);
        m_term.io1_key_ready = false;
    }
}

// ============================================================================
// there is a separate OBS handler routine for each port
// ============================================================================

void
IoCardTermMux::OBS_01(int val)
{
    if (NOISY) {
        UI_Warn("unexpected TermMux OBS 01: 0x%02x", val);
    }
}

void
IoCardTermMux::OBS_02(int val)
{
    if (NOISY) {
        UI_Warn("unexpected TermMux OBS 02: 0x%02x; this is a status port", val);
    }
    return;
}

void
IoCardTermMux::OBS_03(int val)
{
    if (NOISY) {
        UI_Warn("unexpected TermMux OBS 03: 0x%02x (MXE only)", val);
    }
}

// send bytes to printer attached to the currently selected terminal
void
IoCardTermMux::OBS_04(int val)
{
    if (m_term.printer_buf.size() < PrinterBufferSize) {
        m_term.printer_buf.push_back(static_cast<uint8>(val));
    } else if (NOISY) {
        UI_Warn("TermMux OBS 04: 0x%02x, but the print buffer is full", val);
    }
}

// addressing this register causes vp_mode to be set.
// any bytes sent to this address get sent to the CRT display.
void
IoCardTermMux::OBS_05(int val)
{
    if (NOISY) {
        UI_Info("TermMux vp_mode OBS 05: byte 0x%02x", val);
    }

    // FIXME: these should probably go into keyboard_buf (if not full)
    // and then some timer-based mechansim would be used to drain them
    // at baud rate
    UI_displayChar(m_term.wndhnd, (uint8)val);
    m_cpu->setDevRdy(!m_card_busy);
}

void
IoCardTermMux::OBS_06(int val)
{
    auto cmd_len = m_term.command_buf.size();

    if (cmd_len == 0) {
        // there was no initial CBS_06 command byte
        if (val != 0x00 && NOISY) {
            // complain if we get command argument bytes either without an initial
            // command byte, or because it is an extra byte following a successfully
            // decoded command.
            // 2236MXE_Documentation.8-83.pdf, p8, says that sometimes an extra 00
            // byte is sent after the normal command, perhaps for timing reasons.
            UI_Warn("unexpected TermMux OBS 06: 0x%02x without preceding command byte", val);
            return;
        }
        if (val == 0x00) {
            // some commands end with an extra 0x00
            return;
        }
    }

    // append byte to current command
    m_term.command_buf.push_back(static_cast<uint8>(val));

    // commands don't take any extra arguments, so should be processed immediately
    uint8 cmd_byte = m_term.command_buf[0];
    switch (static_cast<mux_cmd_t>(cmd_byte)) {

        case mux_cmd_t::CMD_SELECT_TERMINAL:  // CBS(FF) OBS(xx)
            if (cmd_len == 2) {
                val &= 0x7;     // this is what the mxd ucode does
                if ((val < 0 || val > 3) && NOISY) {
                    UI_Warn("unexpected TermMux command sequence FF %02X", val);
                } else {
if (dbg) dbglog("TermMux OBS 06: selected terminal %d\n", val+1);
                    m_port = val;
                    m_term = m_terms[m_port];
                }
                m_term.command_buf.clear();
                return;
            }
            break;

        case mux_cmd_t::CMD_LINE_REQ: // CBS(07) OBS(XXXXYYZZ)
            // A command code of 07h will cause the controller to setup to
            // receive a field of up to XXXX characters (a hexadecimal
            // representation of the count, not to exceed 480 (01E0h)) starting
            // from the current CRT cursor position for the currently selected
            // terminal.  All field entries will be forced to stay within the
            // field limits set.  A line request is active until either a
            // carriage return or a special function key is entered, or until
            // a delete line request command is issued (RESET, HALT, etc).
            // YY specifies three parameters as follows: The 80-bit specifies
            // underline.  The 04-bit specifies EDIT mode.  The 01-bit
            // specifies that characters previously entered in the keyboard
            // buffer should be flushed.  (In other words, keystrokes received
            // prior to a line request being set, can be either received as
            // part of the line or deleted).  If deleted they are never echoed
            // back to the CRT nor entered into the line request buffer.
            // ZZ specifies current column of CRT cursor (the 2200 should have
            // already positioned the cursor at this position).
            if (m_term.command_buf.size() == 5) {
                m_term.io6_field_size = 256*m_term.command_buf[1]
                                      +     m_term.command_buf[2];
                m_term.io6_underline = !!(m_term.command_buf[3] & 0x80);
                m_term.io6_edit      = !!(m_term.command_buf[3] & 0x04);
                if (!!(m_term.command_buf[3] & 0x01)) {
                    // empty out any pending keystrokes
                    m_term.keyboard_buf.clear();
                }
                // FIXME: ZZ byte isn't used yet
                if (m_term.io6_field_size > 480) {
                    UI_Warn("TermMux REQUEST-LINE command has bad size %d (>480)",
                            m_term.io6_field_size);
                    // I'm not sure if it is better to limit or abandon
                    m_term.io6_field_size = 480;
                }
                m_term.line_req_buf.clear();
if (dbg) dbglog("TermMux OBS 06: CMD_LINE_REQ: field_size=%d, underline=%d, edit=%d\n",
        m_term.io6_field_size, m_term.io6_underline, m_term.io6_edit);
            }
            break;

        case mux_cmd_t::CMD_PREFILL_LINE_REQ:  // CBS(08) OBS(YYYY...)
            // This optional command code of CBS(08) can be sent after a line
            // request command CBS(07) to prefill the desired line with the
            // supplied characters YYYY...  starting with the leftmost position.
            // The characters are treated as keystrokes.  The cursor is
            // terminated by the next CBS, which will normally be an
            // END-OF-LINE-REQUEST CBS(0A).
            if (m_term.line_req_buf.size() < m_term.io6_field_size) {
                m_term.line_req_buf.push_back(static_cast<uint8>(val));
if (dbg) dbglog("TermMux OBS 06: CMD_PREFILL_LINE_REQ got %02x\n", val);
            }
            break;

        case mux_cmd_t::CMD_REFILL_LINE_REQ:  // CBS(09) OBS(XXXX...)
            // The refill command is identical to a prefill except that it does
            // not cause repositioning of the cursor to the beginning.  Thus
            // the characters are treated as keystrokes.  It is normally used
            // for RECALL and DEFFN' quotes.  It is generally followed by an
            // END-OF-LINE-REQUEST CBS(0A) or a TERMINATE-LINE-REQUEST CBS(10).
            if (m_term.line_req_buf.size() < m_term.io6_field_size) {
                m_term.line_req_buf.push_back(static_cast<uint8>(val));
if (dbg) dbglog("TermMux OBS 06: CMD_REFILL_LINE_REQ got %02x\n", val);
            }
            break;

        default:
            // unexpected -- the real hardware ignores this byte
            if (NOISY) {
                UI_Warn("unexpected TermMux CBS: 0x%02x", val);
            }
            break;
    }
}

// sending display data to the current selected terminal's CRT
void
IoCardTermMux::OBS_07(int val)
{
    if (m_term.crt_buf.size() < CrtBufferSize) {
        m_term.crt_buf.push_back(static_cast<uint8>(val));
    } else if (NOISY) {
        UI_Warn("TermMux OBS 07: 0x%02x, but the CRT buffer is full", val);
    }

    // The device should be ready any time there is at least three bytes of
    // space available [in the output buffer].
    // (2236MXE_Documentation.8-83.pdf, p16)
    m_card_busy = (m_term.crt_buf.size() >= CrtBufferSize-3);
    m_cpu->setDevRdy(!m_card_busy);
}

// ============================================================================
// there is a separate CBS handler routine for each port
// ============================================================================

void
IoCardTermMux::CBS_01(int val)
{
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_Warn("unexpected TermMux CBS 01: 0x%02x", val);
    }
}

void
IoCardTermMux::CBS_02(int val)
{
    if (NOISY) {
        UI_Warn("unexpected TermMux CBS 02: 0x%02x; this is a status port", val);
    }
}

void
IoCardTermMux::CBS_03(int val)
{
    if (NOISY) {
        UI_Warn("unexpected TermMux CBS 03: 0x%02x (MXE only)", val);
    }
}

void
IoCardTermMux::CBS_04(int val)
{
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_Warn("unexpected TermMux CBS 04: 0x%02x", val);
    }
}

void
IoCardTermMux::CBS_05(int val)
{
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_Warn("unexpected TermMux CBS 05: 0x%02x", val);
    }
}

// CBS to port 6 begins a control command sequence.
// command descriptions are taken from 2236MXE_Documentation.8-83.pdf,
// page 8 or so.
//
// FIXME: not really a fixme, but at the bottom of page 7, CPB  is described as:
//        "The 2200 sets the CPU Busy signal active on the 2200 bus.
//        This signals its readiness to receive bytes from the controller."
void
IoCardTermMux::CBS_06(int val)
{
    if (!m_term.command_buf.empty()) {
        // re: CMD_END_OF_LINE_REQ
        // "The 2200 will sometimes skip this command when it knows it will
        //  read in the data from the controller on the next command.  This is
        //  a violation of protocol but the controllers live with it.  Should
        //  the 2200 skip this command and go directly to CBS(0C) the
        //  controller should update the screen before allowing another Line
        //  Request to be started."
        auto cmd = static_cast<mux_cmd_t>(m_term.command_buf.front());
        if (cmd == mux_cmd_t::CMD_ACCEPT_LINE_REQ_DATA &&
            !m_term.command_buf.empty()) {
            perform_end_of_line_req();
        }
    }

    // we are starting a new command, so kill the last one
    m_term.command_buf.clear();

    // commands don't take any extra arguments, so should be processed immediately
    switch (static_cast<mux_cmd_t>(val)) {

        case mux_cmd_t::CMD_NULL:  // CBS(00)
            // do nothing
if (dbg) dbglog("TermMux OBS 06: CMD_NULL\n");
            break;

        case mux_cmd_t::CMD_POWERON:  // CBS(01)
            // The MXD reininitalizes itself to VP/Bootstrap mode.  Everything
            // is set to the way it is at power on.  All buffers are cleared,
            // all pointers are reset, all flags cleared. The mode becomes VP
            // mode.
if (dbg) dbglog("TermMux OBS 06: CMD_POWERON\n");
            reset();
            break;

        case mux_cmd_t::CMD_INIT_CURRENT_TERM:  // CBS(02)
            // This command will cause the CRT screen, pending line request CRT
            // buffer, print buffer, and input buffer of the current terminal
            // to be cleared (used at RESET).
// FIXME: the SVP muxd code always looks for an OBS following CBS(02).
// and interestingly, the init sequence is different if the OBS is 00 or not.
            m_term.io1_key_ready = false;
            m_term.crt_buf.clear();
            m_term.printer_buf.clear();
            m_term.line_req_buf.clear();
            m_term.keyboard_buf.clear();
if (dbg) dbglog("TermMux OBS 06: CMD_INIT_CURRENT_TERM\n");
            break;

        case mux_cmd_t::CMD_DELETE_LINE_REQ:  // CBS(03)
            // this command causes a pending line request and input buffers
            // of the current terminal to be cleared
            // (used at HALT with special function keys)
            m_term.io1_key_ready = false;
            m_term.line_req_buf.clear();
            m_term.keyboard_buf.clear();
if (dbg) dbglog("TermMux OBS 06: CMD_DELETE_LINE_REQ\n");
            break;

        case mux_cmd_t::CMD_END_OF_LINE_REQ:  // CBS(0A)
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_END_OF_LINE_REQ\n");
            perform_end_of_line_req();
            break;

        case mux_cmd_t::CMD_QUERY_LINE_REQ:  // CBS(0B)
            // When a CBS(0B) command is received, the controller responds with
            // one of the following IBS values.
            //       00h -- No line request in progress.
            //       01h -- line request still in progress.
            //       0Dh -- line request terminated by CR.
            //       FFh -- Recall key pressed (see note).
            //  ENDI(XX) -- S.F. key pressed.
            //
            // Note on recall:
            //     After the FFh, the controller may send one more more bytes
            //     to the MVP.  Each time the MVP sets CPB ready, th controller
            //     will send one more data byte with IBS.  These are the
            //     characters from the entered text, read from right to left,
            //     beginning with the cursor position.  The beginning of the
            //     buffer is indicated by ENDI.  This sequence ends whenever
            //     the MVP stops setting CPB ready, sends OBS or CBS, or
            //     switches address.  The controller should not clear the
            //     buffer when the 2200 has read all the bytes contained
            //     therein.  Unfortunately, the 2200 takes some shortcuts for
            //     expediency and may reread the buffer later.
            //
            // Following the query, the MVP may do one of the following:
            //     1. Nothing another query later).
            //     2. Delete line request
            //        (usually for HALT, and SF keys without parameters).
            //     3. Refill -- this is more data to be merged into the present
            //        line request, as though the operator typed it (used for
            //        recall and DEFFN' quotes).  Then End Line Request.
            //     4. Terminate Line Request -- used to implement DEFFN' HEX(0D).
            //     5. Error Line Request -- this beeps an error and continues
            //        the line request.
            //     6. Ask for data.
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_QUERY_LINE_REQ\n");
            break;

        case mux_cmd_t::CMD_ACCEPT_LINE_REQ_DATA:  // CBS(0C)
            // When an CBS(0C) is received after a line request has been
            // completed, the controller will send the data.  It should only be
            // issued after a query has shown that the line is complete.
            //
            // The controller sends the data if any, then an ENDI as terminator.
            // If the ENDI is zero, the line request is complete; if 01h, the
            // controller needs more time to finish updating the screen.
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_ACCEPT_LINE_REQ_DATA\n");
            break;

        case mux_cmd_t::CMD_REQ_CRT_BUFFER:  // CBS(0D)
            // This command causes the controller to check the CRT buffer of
            // the current terminal.  If it is empty, the appropriate status
            // bit is set (address 02h = ready) to signal the fact.  If not,
            // then the controller will set the bit when the buffer does go
            // empty.
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_REQ_CRT_BUFFER\n");
            break;

        case mux_cmd_t::CMD_REQ_PRINT_BUFFER:  // CBS(0E)
            // This is just like the previous, except is refers to the current
            // terminal's PRINT buffer not CRT buffer.
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_REQ_PRINT_BUFFER\n");
            break;

        case mux_cmd_t::CMD_ERROR_LINE_REQ:  // CBS(0F)
            // This command causes the line request to resume, just like
            // END-LINE-REQUEST, except it beeps first.  It should not be used
            // in conjunction with PREFILL or REFILL.  It is normally used for
            // undefined function keys.
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_ERROR_LINE_REQ\n");
            break;

        case mux_cmd_t::CMD_TERMINATE_LINE_REQ:  // CBS(10)
            // This command is used (after optional PREFILL or REFILL) to
            // cause all the same actions as the operator pressing EXEC.
            // It is normally used for the BASIC statement DEFFN' HEX(0D).
            // TODO
if (dbg) dbglog("TermMux OBS 06: CMD_TERMINATE_LINE_REQ\n");
            break;

        // the following commands take arguments via subsequent OBS 06 strobes.
        // just start a command string and it will be handled when the command
        // string is complete.
        case mux_cmd_t::CMD_SELECT_TERMINAL:
        case mux_cmd_t::CMD_LINE_REQ:
        case mux_cmd_t::CMD_PREFILL_LINE_REQ:
        case mux_cmd_t::CMD_REFILL_LINE_REQ:
            m_term.command_buf.push_back(static_cast<uint8>(val));
            break;

        // the following commands are followed by an IBS request, so we have to
        // leave the command byte so the IBS routine knows what is supposed to
        // returned.
        case mux_cmd_t::CMD_KEYBOARD_READY_CHECK:
        case mux_cmd_t::CMD_KEYIN_POLL_REQ:
        case mux_cmd_t::CMD_KEYIN_LINE_REQ:
            m_term.command_buf.push_back(static_cast<uint8>(val));
            break;

        // unknown/unsupported by MXD mux
        default:
            if (NOISY) {
                UI_Warn("unexpected TermMux CBS: 0x%02x", val);
            }
            break;
    }
}

// page 16 of the doc:
// Address 07h is used to transmit characters onto the CRT of the current
// terminal.  It should be ready whenever there is at least three bytes of space
// available.  When it goes busy the CPU will time out (one milliscond) and
// service other partitions until a buffer-empty interrupt occurs.  In addition
// to OBS with data to be displayed, this address also supports CBS's at this
// address.  When the controller receives CBS(xx) at address 07h it should
// place a FBh before itin the buffer.  This will cause the terminal to accept
// this string as a command.  The following are the three possible commands to
// the terminal:
//    FB <count> <character>   blah blah
//    FB <delay>               where delay is CBS(C0) to CBS(C9)
//    FB D0h                   literal character FB
// (it seems like that is not important to the mux, just the terminal emulation)
void
IoCardTermMux::CBS_07(int val)
{
    if (m_term.crt_buf.size() < CrtBufferSize-1) {
        m_term.crt_buf.push_back(static_cast<uint8>(0xFB));
        m_term.crt_buf.push_back(static_cast<uint8>(val));
    } else if (NOISY) {
        UI_Warn("TermMux CBS 07: 0x%02x, but the CRT buffer is full", val);
    }

    // The device should be ready any time there is at least three bytes of
    // space available [in the output buffer].
    // (2236MXE_Documentation.8-83.pdf, p16)
    m_card_busy = (m_term.crt_buf.size() >= CrtBufferSize-3);
    m_cpu->setDevRdy(!m_card_busy);
}


// A special command must be supplied to signal the end of a line request
// sequence which consists of the setup and prefill if desired.  The last
// command sent, however, must be a CBS(0A), to signal the microcode to invoke
// the line request.  Nothing is sent to the CRT until the CBS(0A) is issued.
//
// This command is also used after successful RECALL or DEFFN' text entry to
// signal the controller to resume proceessing the line request.
void
IoCardTermMux::perform_end_of_line_req()
{
    // FIXME: actually, the first thing to do is send the (p)refill
    // bytes, then worry about pending keys as if they were being typed
    // for the first time.
    // TODO

    // transfer pending keystrokes into the buffer, I guess
    while (!m_term.keyboard_buf.empty() &&
            m_term.line_req_buf.size() < m_term.io6_field_size) {
        m_term.line_req_buf.push_back( m_term.keyboard_buf.front() );
        m_term.keyboard_buf.pop_front();
    }
}

// ============================================================================
// when CPB drops (cpu not busy), it means the CPU is waiting for a byte from
// the addressed card.  The card supplies the byte by driving the IBS strobe
// when it is ready.
// ============================================================================

void
IoCardTermMux::IBS_01()
{
    check_keyready();
    if (m_term.io1_key_ready) {
        m_cpu->IoCardCbIbs(m_term.io1_key_code);
        m_term.io1_key_ready = false;
    }
}

// see 2236MXE_Documentation.8-83.pdf, page 17. also check out
//   C:\Users\Jim\Documents\wang2200\disk_images\kryoflux\MVP_3.5_Source_Aug_91\JLMVP32A.txt
// for the ucode on the 2200 CPU side of the handshake.
// we response with 7 bytes indicating various status.
void
IoCardTermMux::IBS_02()
{
    int retval = 0x00;

    switch (m_ibs_seq) {

        case 0: // RESET flags, 1 bit per terminal
            for(int n=0; n<MAX_TERMINALS; n++) {
                retval = (0 << n);  // FIXME: '1' if term is RESET
            }
if (dbg) dbglog("IBS 02, seq=%d, returning %02x\n", m_ibs_seq, retval);
            m_cpu->IoCardCbIbs(retval);
            break;

        case 1: // Halt/step flags, 1 bit per terminal
            for(int n=0; n<MAX_TERMINALS; n++) {
                retval = (0 << n);  // FIXME: '1' if term is halt/step
            }
if (dbg) dbglog("IBS 02, seq=%d, returning %02x\n", m_ibs_seq, retval);
            m_cpu->IoCardCbIbs(retval);
            break;

        case 2: // one second clock tick (MXE/triple controller only)
// FIXME: the muxd rom in the lvp returns the uart DSR flags
retval = 0x01;  // indicate port 1 data set ready
if (dbg) dbglog("IBS 02, seq=%d, returning %02x\n", m_ibs_seq, retval);
            m_cpu->IoCardCbIbs(retval);
            break;

        case 3: // terminal 1 status
        case 4: // terminal 2 status
        case 5: // terminal 3 status
        case 6: // terminal 4 status
        {
            // bit 0: printer buffer became empty (requested)
            // bit 1: CRT buffer became empty (requested)
            // bit 2: keyboard buffer not empty or line request complete
            // bit 3: RSD buffer almost full  (MXE only)
            // bits 4-7: muxd doesn't use these
            int off = (m_ibs_seq - 3);
            if (m_terms[off].printer_buf.empty()) {
                // FIXME: what is this "requested" qualifier?
                retval |= 0x01;
            }
            if (m_terms[off].crt_buf.empty()) {
                // FIXME: what is this "requested" qualifier?
                retval |= 0x02;
            }
            if (!m_terms[off].keyboard_buf.empty()) {
                // FIXME: "line request complete" not implemented yet
                retval |= 0x04;
            }
if (dbg) dbglog("IBS 02, seq=%d, returning %02x\n", m_ibs_seq, retval);
            m_cpu->IoCardCbIbs(retval);
            break;
        }

        case 7: // ENDI terminator byte
            // the documentation doesn't say what value to send, but the muxd
            // ROM sends 0xF0 w/endi
            retval = 0x1F0;  // note bit 8 is ENDI
if (dbg) dbglog("IBS 02, seq=%d, returning %03x\n", m_ibs_seq, retval);
            m_cpu->IoCardCbIbs(retval);
            break;

        default:
            if (NOISY) {
                UI_Info("TermMux IBS %02x call not expected", m_io_offset);
            }
            break;
    }
    m_ibs_seq++;
}

void
IoCardTermMux::IBS_06()
{
    if (m_term.command_buf.empty()) {
        if (NOISY) {
            UI_Warn("unexpected TermMux IBS 06: empty command buffer");
        }
        return;
    }

    uint8 cmd_byte = m_term.command_buf[0];

    int retval = 0x00;
    switch (static_cast<mux_cmd_t>(cmd_byte)) {

    case mux_cmd_t::CMD_KEYBOARD_READY_CHECK: // CBS(04) IBS(xx)
        // Whenever a command code of 04h is received, the controller
        // checks the keyboard buffer of the selected terminal.  An IBS(00)
        // is sent if it is empty, and non zero if there is a character
        // (the non zero byte is of no particular value).
        retval = (m_term.keyboard_buf.empty()) ? 0x00 : 0xff;
        m_cpu->IoCardCbIbs(retval);
        m_term.command_buf.clear();
        break;

    case mux_cmd_t::CMD_KEYIN_POLL_REQ: // CBS(05) IBS(xx) IBS(yy)
        // Whenever a command code of 05h is received, the controller
        // checks the keyboard buffer.  If there is no character an IBS(00)
        // is returned.  Otherwise, a non zero byte is returned followed by
        // an IBS of the byte (with ENDI if it is a Special Function key).
        switch (m_ibs_seq) {

            case 0: // return 00 or non-00 to indicate if there is something ready
                if (m_term.keyboard_buf.empty()) {
                    retval = 0x00;
                    m_ibs_seq = 2;
                    m_term.command_buf.clear();
                } else {
                    retval = 0xff;
                    m_ibs_seq = 1;
                }
                m_cpu->IoCardCbIbs(retval);
                break;

            case 1: // return the keystroke (w/ possible ENDI)
                retval = m_term.keyboard_buf.front();
                m_term.keyboard_buf.pop_front();
                m_cpu->IoCardCbIbs(retval);
                m_ibs_seq++;
                m_term.command_buf.clear();
                break;

            default:
                if (NOISY) {
                    UI_Info("TermMux command 05 IBS %02x call not expected", m_io_offset);
                }
                break;
        }
        break;

    case mux_cmd_t::CMD_KEYIN_LINE_REQ: // CBS(06) IBS(xx)
        // Whenever a command code of 06h is received, the controller checks
        // the keyboard buffer.  If there is a byte in it, this command is
        // treated exactly as the command 05h.  If not, a IBS(00) is sent, and
        // a special Keyin Line Request is set up.  An interrupt (completed
        // line request) will be generated at address 02h when a character is
        // later received.
        // FIXME: unimplemented
        break;

    default:
        UI_Warn("IBS_06: Got unexpected IBS following command byte %02x", cmd_byte);
        break;

    } // switch
}

// vim: ts=8:et:sw=4:smarttab
