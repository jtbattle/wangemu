// The MXD Terminal Mux card contains an 8080, some EPROM and some RAM,
// along with four RS-232 ports.  Rather than emulating this card at
// the chip level, it is a functional emulation, based on the description
// of the card in 2236MXE_Documentation.8-83.pdf

#include "Ui.h"
#include "IoCardTermMux.h"
#include "Cpu2200.h"


#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif


// instance constructor
IoCardTermMux::IoCardTermMux(Cpu2200 &cpu, int baseaddr, int cardslot) :
    m_cpu(cpu),
    m_baseaddr(baseaddr),
    m_slot(cardslot),
    m_selected(false),
    m_cpb(true),
    m_card_busy(false)
{
    if (m_slot < 0) {
        // this is just a probe to query properties, so don't make a window
        return;
    }

    int io_addr;
    bool ok = System2200().getSlotInfo(cardslot, 0, &io_addr);
    assert(ok); ok=ok;

    for(int i=0; i<4; i++) {
        m_term[i].wndhnd = nullptr;
    }

    // FIXME: just one for now
    m_term[0].wndhnd = UI_initCrt(UI_SCREEN_80x24, io_addr);

    m_wndhnd[0] = UI_initCrt(UI_SCREEN_80x24, io_addr);

    reset(true);
}

// instance destructor
IoCardTermMux::~IoCardTermMux()
{
    if (m_slot >= 0) {
        // not just a temp object, so clean up
        reset(true);        // turns off handshakes in progress
        UI_destroyCrt(m_term[0].wndhnd); // FIXME: up to 4
    }
}

const string
IoCardTermMux::getDescription() const
{
    return "Terminal Mux";
}

const string
IoCardTermMux::getName() const
{
    return "2236 MXD";
}

// return a list of the various base addresses a card can map to
// the default comes first.
vector<int>
IoCardTermMux:getBaseAddresses() const
{
    vector<int> v { 0x000, 0x040, 0x080, 0x0c0 };
    return v;
}

// return the list of addresses that this specific card responds to
vector<int>
IoCardTermMux::getAddresses() const
{
    vector<int> v;
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

Really, I don't want to duplicate all the logic in IoCardKeyboard.cpp.
I need to restructure things so that logic can be shared.
    for(int i=0; i<4; i++) {
        m_term[i].key_ready = false;   // no pending keys
        m_term[i].crt_buf.clear();
        m_term[i].printer_buf.clear();
        m_term[i].line_req_buf.clear();
        m_term[i].keyboard_buf.clear();
        m_term[i].command_buf.clear();
    }

    m_vp_mode = true;
    m_current_term = 1;

    hard_reset = hard_reset;    // silence lint
}

void
IoCardTermMux::select()
{
    m_addr = m_cpu->getAB();
    uint8 offset = (m_addr & 7);

    bool vp_mode_next = (offset == 1) || (offset == 5);
    if (!vp_mode && !vp_mode_next) {
        // leaving mvp mode for vp mode; reset
        reset(false);
    }
    m_vp_mode = vp_mode_next;

    m_selected = true;

    m_cpu.setDevRdy(!m_card_busy);
    // ...
}

void
IoCardTermMux::deselect()
{
    if (NOISY)
        UI_Info("TermMux -ABS");

    m_selected = false;
    m_cpb      = true;
    // ...
}

void
IoCardTermMux::OBS(int val)
{
    val &= 0xFF;
    if (NOISY)
        UI_Info("TermMux OBS: Output of byte 0x%02x", val);

    switch (m_addr & 7) {
        case 1: OBS_01(val); break;
        case 2: OBS_02(val); break;
        case 3: OBS_03(val); break;
        case 4: OBS_04(val); break;
        case 5: OBS_05(val); break;
        case 6: OBS_06(val); break;
        case 7: OBS_07(val); break;
    }

    // ...
    m_cpu.setDevRdy(!m_card_busy);
}

void
IoCardTermMux::CBS(int val)
{
    val &= 0xFF;
    if (NOISY)
        UI_Info("TermMux CBS: Output of byte 0x%02x", val);

    switch (m_addr & 7) {
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
// or 80x24 (true).  All smart terms are 80x24, though.
// TODO: perhaps this hack wasn't used in MVP mode as it already assumed
// 80x24.
int
IoCardTermMux::GetIB5() const
{
    return 1;
}

// change of CPU Busy state
void
IoCardTermMux::CPB(bool busy)
{
    assert(val == 0 || val == 1);

    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (NOISY)
        UI_Info("TermMux CPB%c", val?'+':'-');

    m_cpb = busy;
    // ...
    m_cpu.setDevRdy(!m_card_busy);
}

// ============================================================================
// there is a separate handler routine for each port
// ============================================================================

// CBS to port 6 begins a control command sequence
void
IoCardTermMux::CBS_06(int val)
{
    if (!m_command_buffer.empty()) {
        // possibly flush out the existing command string
        uint8 cmd = m_command_buffer.front();
        // re: CMD_END_OF_LINE_REQ
        // "The 2200 will sometimes skip this command when it knows it will read in
        //  the data from the controller on the next command.  This is a violation of
        //  protocol but the controllers live with it.  Should the 2200 skip this command
        //  and go directly to CBS(0x0C) the controller should update this creen before
        //  allowing another Line Request to be started.
        if (cmd == CMD_
    }

    switch (val) {
        CMD_SELECT:
            break;
        CMD_NULL:
            break;
        CMD_POWERON:
            break;
        CMD_INIT_CURRENT_TERM:
            break;
        CMD_DELETE_LINE_REQ:
            break;
        CMD_KEYBOARD_READY_CHECK:
            break;
        CMD_KEYIN_POLL_REQ:
            break;
        CMD_KEYIN_LINE_REQ:
            break;
        CMD_LINE_REQ:
            break;
        CMD_PREFILL_LINE_REQ:
            break;
        CMD_REFILL_LIN_REQ:
            break;
        CMD_END_OF_LINE_REQ:
            break;
        CMD_QUERY_LINE_REQ:
            break;
        CMD_ACCEPT_LIN_REQ_DATA:
            break;
        CMD_REQ_CRT_BUFFER:
            break;
        CMD_REQ_PRINT_BUFFER:
            break;
        CMD_ERROR_LINE_REQ:
            break;
        CMD_TERMINATE_LINE_REQ:
            break;
        default:
            // unexpected -- the real hardware ignores this byte
            if (NOISY)
                UI_Warn("unexpected TermMux CBS: Output of byte 0x%02x", val);
            break;
    }
}

// vim: ts=8:et:sw=4:smarttab
