// The MXD Terminal Mux card contains an 8080, some EPROM and some RAM,
// along with four RS-232 ports.  The function is emulated at the chip level,
// meaning and embedded i8080 microprocessor emulates the actual ROM image
// from a real MXD card.
//
// These documents were especially useful in reverse engineering the card:
// https://wang2200.org/docs/system/2200MVP_MaintenanceManual.729-0584-A.1-84.pdf
//     section F, page 336..., has schematics for the MXD board (7290-1, 7291-1)
// https://wang2200.org/docs/internal/2236MXE_Documentation.8-83.pdf
// Hand disassembly of MXD ROM image (not yet online ... FIXME)

#include "Ui.h"
#include "IoCardTermMux.h"
#include "IoCardKeyboard.h"   // for key encodings
#include "Cpu2200.h"
#include "Scheduler.h"
#include "System2200.h"
#include "i8080.h"

#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif

const bool dbg = false; // temporary debugging hack

// the i8080 runs at 1.78 MHz
const int ns_per_tick = 561;

// character transmission time, in nanoseconds
const int64 serial_char_delay =
            TIMER_US(  11.0              /* bits per character */
                     * 1.0E6 / 19200.0   /* microseconds per bit */
                    );

// mxd eprom image
#include "IoCardTermMux_eprom.h"

// input port defines
static const int IN_UART_TXRDY  = 0x00; // parallel poll of which UARTs have room in their tx fifo
static const int IN_2200_STATUS = 0x01; // various status bits
        //   0x01 = OBS strobe seen
        //   0x02 = CBS strobe seen
        //   0x04 = PRIME (reset) strobe seen (cleared by OUT 0)
        //   0x08 = high means we are selected and the CPU is waiting for input
        //   0x10 = board selected at time of ABS
        //   0x20 = AB1 when ABS strobed
        //   0x40 = AB2 when ABS strobed
        //   0x80 = AB3 when ABS strobed
static const int IN_OBUS_N      = 0x02; // read !OB bus, and clear obs/cbs strobe status
static const int IN_OBSCBS_ADDR = 0x03; // [7:5] = [AB3:AB1] at time of cbs/obs strobe (active low?)
static const int IN_UART_RXRDY  = 0x04; // parallel poll of which UARTs have received a byte
static const int IN_UART_DATA   = 0x06;
static const int IN_UART_STATUS = 0x0E;
        //   0x80 = DSR       (data set ready)
        //   0x40 = BRKDET    (break detect)
        //   0x20 = FE        (framing error)
        //   0x10 = OE        (overrun error)
        //   0x08 = PE        (parity error)
        //   0x04 = TxEMPTY   (the tx fifo is empty and the serializer is done)
        //   0x02 = RxRDY     (a byte has been received)
        //   0x01 = TxRDY     (the tx fifo buffer has room for a character)

// output port defines
static const int OUT_CLR_PRIME = 0x00;  // clears reset latch
static const int OUT_IB_N      = 0x01;  // drive !IB1-!IB8, pulse IBS
static const int OUT_IB9_N     = 0x11;  // same as OUT_IB_N, plus drive IB9
static const int OUT_PRIME     = 0x02;  // fires the !PRIME strobe
static const int OUT_HALT_STEP = 0x03;  // one shot strobe
static const int OUT_UART_SEL  = 0x05;  // uart chip select
static const int OUT_UART_DATA = 0x06;  // write to selected uart data reg
static const int OUT_RBI       = 0x07;  // 0=ready/1=busy; bit n=addr (n+1); bit 7=n/c
                                        // 01=1(kb),02=2(status),04=3(n/c),08=4(prt),10=5(vpcrt),20=6(cmd),40=7(crt)
static const int OUT_UART_CMD  = 0x0E;  // write to selected uart command reg

// instance constructor
IoCardTermMux::IoCardTermMux(std::shared_ptr<Scheduler> scheduler,
                             std::shared_ptr<Cpu2200> cpu,
                             int baseaddr, int cardslot) :
    m_scheduler(scheduler),
    m_cpu(cpu),
//  m_i8080(nullptr),
    m_baseaddr(baseaddr),
    m_slot(cardslot),
    m_selected(false),
    m_cpb(true),
    m_io_offset(0),
    m_prime_seen(true),
    m_cbs_seen(false),
    m_obs_seen(false),
    m_obscbs_offset(0),
    m_obscbs_data(0x00),
    m_rbi(0xff),                // not ready
    m_uart_sel(0),
    m_interrupt_pending(false),
    m_init_tmr(nullptr)
{
    if (m_slot < 0) {
        // this is just a probe to query properties, so don't make a window
        return;
    }

    for(auto &t : m_terms) {
        t.wndhnd = nullptr;

        t.remote_baud_tmr = nullptr;

        t.rx_ready = false;
        t.rx_byte  = 0x00;

        t.tx_ready = false;
        t.tx_byte  = 0x00;
        t.tx_tmr   = nullptr;
    }

    int io_addr;
    bool ok = System2200::getSlotInfo(cardslot, 0, &io_addr);
    assert(ok); ok=ok;

    m_i8080 = i8080_new(IoCardTermMux::i8080_rd_func,
                        IoCardTermMux::i8080_wr_func,
                        IoCardTermMux::i8080_in_func,
                        IoCardTermMux::i8080_out_func,
                        static_cast<void*>(this));
    assert(m_i8080);
    i8080_reset(static_cast<i8080*>(m_i8080));

    // register the i8080 for clock callback
    clkCallback cb = std::bind(&IoCardTermMux::execOneOp, this);
    System2200::registerClockedDevice(cb);

    // FIXME: just one for now
    m_terms[0].wndhnd = UI_initCrt(UI_SCREEN_2236DE, io_addr, 1);
    m_terms[0].tx_ready = true;

    {
        int term_num = 0; /* FIXME: term 1 is hardwired here */
        System2200::kb_register(
            // FIXME: need 0x01 because elsewhere m_assoc_kb_addr defaults to 0x01;
            //        a later System2200::kb_foo(m_assoc_kb_addr,...) call fails
            //        fix the m_assoc_kb_addr logic instead of this hack
            m_baseaddr + 0x01,
            term_num+1,
            std::bind(&IoCardTermMux::receiveKeystroke,
                      this, term_num, std::placeholders::_1)
        );
    }

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
                   std::bind(&IoCardTermMux::SendInitSeq, this)
                 );
}

// the 2336 sends an init sequence of E4 F8 on power up.
// the 2336 sends an init sequence of 12 F8 E4 on reset.
void
IoCardTermMux::SendInitSeq()
{
    m_init_tmr = nullptr;
    for(int term_num=0; term_num<MAX_TERMINALS; term_num++) {
        if (m_terms[term_num].wndhnd) {
//          m_terms[term_num].remote_kb_buff.push(static_cast<uint8>(0xE4));
            m_terms[term_num].remote_kb_buff.push(static_cast<uint8>(0xF8));
            CheckKbBuffer(term_num);
        }
    }
}

// perform on instruction and return the number of ns of elapsed time.
int
IoCardTermMux::execOneOp()
{
    if (m_interrupt_pending) {
        // vector to 0x0038 (rst 7)
        i8080_interrupt(static_cast<i8080*>(m_i8080), 0xFF);
    }

    int ticks = i8080_exec_one_op(static_cast<i8080*>(m_i8080));
    if (ticks > 30) {
        // it is in an error state
        return 4 * ns_per_tick;
    }
    return ticks * ns_per_tick;
}

// instance destructor
IoCardTermMux::~IoCardTermMux()
{
    if (m_slot >= 0) {
        // not just a temp object, so clean up
        System2200::kb_unregister(m_baseaddr + 0x01, /* see the hack comment in kb_register() */
                                   1 /* FIXME: term 1 is hardwired here */);

        i8080_destroy(static_cast<i8080*>(m_i8080));
        m_i8080 = nullptr;

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

// the MXD card has its own power-on-reset circuit. all !PRMS (prime reset)
// does is set a latch that the 8080 can sample.  the latch is cleared
// via "OUT 0".
// interestingly, the reset pin on the i8251 uart (pin 21) is tied low
// i.e., it doesn't have a hard reset.
void
IoCardTermMux::reset(bool hard_reset)
{
    m_prime_seen = true;
    hard_reset = hard_reset;    // silence lint
}

void
IoCardTermMux::select()
{
    m_io_offset = (m_cpu->getAB() & 7);

    if (NOISY) {
        UI_Info("TermMux ABS %02x", m_baseaddr+m_io_offset);
    }

    // offset 0 is not handled
    if (m_io_offset == 0) {
        return;
    }
    m_selected = true;

    update_rbi();
}

void
IoCardTermMux::deselect()
{
    if (NOISY) {
        UI_Info("TermMux -ABS %02x", m_baseaddr+m_io_offset);
    }
    m_cpu->setDevRdy(false);

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

    // any previous obs or cbs should have been serviced before we see another
    assert(!m_obs_seen && !m_cbs_seen);

    // the hardware latches m_io_offset into another latch on the falling
    // edge of !CBS or !OBS.  I believe the reason is that say the board is
    // addressed at offset 6.  Then it does an OBS(0Xwhatever) in some fire and
    // forget command.  It may take a while to process that OBS, but in the
    // meantime, the host computer may re-address the board at, say, offset 2.
    m_obs_seen = true;
    m_obscbs_offset = m_io_offset;
    m_obscbs_data = val;

    update_rbi();
}

void
IoCardTermMux::CBS(int val)
{
    val &= 0xFF;
    if (NOISY) {
        UI_Info("TermMux CBS: 0x%02x", val);
    }

    // any previous obs or cbs should have been serviced before we see another
    assert(!m_obs_seen && !m_cbs_seen);

    m_cbs_seen = true;
    m_obscbs_offset = m_io_offset;  // secondary address offset latch
    m_obscbs_data = val;

    update_rbi();
}

// weird hack Wang used to signal the attached display is 64x16 (false)
// or 80x24 (true).  All smart terminals are 80x24, but in boot mode/vp mode,
// the term mux looks like a dumb terminal at 05, so it drives this to let
// the ucode know it is 80x24.
int
IoCardTermMux::getIB() const
{
    // FIXME: do we need to give more status than this?
    // In the real hardware, IB is driven by the most recent
    // OUT_IB_N data any time the board is selected.  In addition,
    // any time the address offset is 5 or 7, a gate forcibly drives
    // !IB5 low (logically, the byte is or'd with 0x10). Looking at
    // the MVP microcode, it only ever looks at bit 5.  However, the
    // "CIO SRS" command is exposed via $GIO 760r (Status Request Strobe).
    return (m_io_offset == 5) ? 0x10 : 0x00;
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
}

// update the board's !ready/busy status (if selected)
void
IoCardTermMux::update_rbi()
{
    // don't drive !rbi if the board isn't selected
    if (m_io_offset == 0 || !m_selected) {
        return;
    }

    bool busy = ((m_obs_seen || m_cbs_seen) && (m_io_offset >= 4))
             || !!((m_rbi >> (m_io_offset-1)) & 1);

    m_cpu->setDevRdy(!busy);
}

void
IoCardTermMux::update_interrupt()
{
    m_interrupt_pending = m_terms[0].rx_ready
                       || m_terms[1].rx_ready
                       || m_terms[2].rx_ready
                       || m_terms[3].rx_ready;
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
IoCardTermMux::receiveKeystroke(int term_num, int keycode)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];

    if (term.remote_kb_buff.size() >= KB_BUFF_MAX) {
        UI_Warn("the terminal keyboard buffer dropped a character");
        return;
    }

    // remap certain keycodes from first-generation encoding to what is
    // send over the serial line
    if (keycode == IoCardKeyboard::KEYCODE_RESET) {
        // reset: although the terminal's tx fifo is modeled here and not
        // in UiCrt, the terminal clears its tx fifo on reset, so we should
        // do the same here.
        term.remote_kb_buff = {};
        term.remote_kb_buff.push(static_cast<uint8>(0x12));
    } else if (keycode == IoCardKeyboard::KEYCODE_HALT) {
        // halt/step
        term.remote_kb_buff.push(static_cast<uint8>(0x13));
    } else if (keycode == (IoCardKeyboard::KEYCODE_SF | IoCardKeyboard::KEYCODE_EDIT)) {
        // edit
        term.remote_kb_buff.push(static_cast<uint8>(0xBD));
    } else if (keycode & IoCardKeyboard::KEYCODE_SF) {
        // special function
        term.remote_kb_buff.push(static_cast<uint8>(0xFD));
        term.remote_kb_buff.push(static_cast<uint8>(keycode & 0xff));
    } else if (keycode == 0xE6) {
        // the pc TAB key maps to "STMT" in 2200T mode,
        // but it maps to "FN" (function) in 2336 mode
        term.remote_kb_buff.push(static_cast<uint8>(0xFD));
        term.remote_kb_buff.push(static_cast<uint8>(0x7E));
    } else if (keycode == 0xE5) {
        // erase
        term.remote_kb_buff.push(static_cast<uint8>(0xE5));
    } else if (0x80 <= keycode && keycode < 0xE5) {
        // it is an atom; add prefix
        term.remote_kb_buff.push(static_cast<uint8>(0xFD));
        term.remote_kb_buff.push(static_cast<uint8>(keycode & 0xff));
    } else {
        // the mapping is unchanged
        assert(keycode == (keycode & 0xff));
        term.remote_kb_buff.push(static_cast<uint8>(keycode));
    }

    CheckKbBuffer(term_num);
}

// process the next entry in kb event queue and schedule a timer to check
// for the next one
void
IoCardTermMux::CheckKbBuffer(int term_num)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];

    if (term.remote_kb_buff.empty() ||  // nothing to do
        term.remote_baud_tmr            // modeling transport delay
       ) {
        return;
    }

    uint8 key = term.remote_kb_buff.front();
    term.remote_kb_buff.pop();

    if (term.rx_ready) {
        UI_Warn("terminal received char too fast");
        // TODO: set uart overrun status bit?
        // then fall through because the old char is overwritten
    }

    term.rx_ready = true;
    term.rx_byte  = key;
    update_interrupt();

    term.remote_baud_tmr = m_scheduler->TimerCreate(
                                serial_char_delay,
                                std::bind(&IoCardTermMux::UartTxTimerCallback, this, term_num)
                           );
}

// callback after a character has finished transmission
void
IoCardTermMux::UartTxTimerCallback(int term_num)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];
    term.remote_baud_tmr = nullptr;

    // see if anything is pending
    CheckKbBuffer(term_num);
}

// ============================================================================
// i8080 CPU modeling
// ============================================================================

int
IoCardTermMux::i8080_rd_func(int addr, void *user_data)
{
    if (addr < 0x1000) {
        // read 4K eprom
        return mxd_eprom[addr & 0x0FFF];
    }

    if ((0x2000 <= addr) && (addr < 0x3000)) {
        // read 4KB ram
        IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
        return tthis->m_ram[addr & 0x0FFF];
    }

    assert(false);
    return 0x00;
}

void
IoCardTermMux::i8080_wr_func(int addr, int byte, void *user_data)
{
    assert(byte == (byte & 0xff));

    if ((0x2000 <= addr) && (addr < 0x3000)) {
        // write 4KB ram
        IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
        tthis->m_ram[addr & 0x0FFF] = static_cast<uint8>(byte);
        return;
    }
    assert(false);
}

int
IoCardTermMux::i8080_in_func(int addr, void *user_data)
{
    IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
    int term_num = tthis->m_uart_sel;
    m_term_t &term = tthis->m_terms[term_num];

    int rv = 0x00;
    switch (addr) {

    case IN_UART_TXRDY:
        // the hardware inverts the status
        rv = (tthis->m_terms[3].tx_ready ? 0x00 : 0x08)
           | (tthis->m_terms[2].tx_ready ? 0x00 : 0x04)
           | (tthis->m_terms[1].tx_ready ? 0x00 : 0x02)
           | (tthis->m_terms[0].tx_ready ? 0x00 : 0x01);
        break;

    case IN_2200_STATUS:
        {
        bool cpu_waiting = tthis->m_selected && !tthis->m_cpb;  // CPU waiting for input
        rv = (tthis->m_obs_seen   ? 0x01 : 0x00)  // [0]
           | (tthis->m_cbs_seen   ? 0x02 : 0x00)  // [1]
           | (tthis->m_prime_seen ? 0x04 : 0x00)  // [2]
           | (cpu_waiting         ? 0x08 : 0x00)  // [3]
           | (tthis->m_selected   ? 0x10 : 0x00)  // [4]
           | (tthis->m_io_offset << 5);           // [7:5]
        }
        break;

    // the 8080 sees the inverted bus polarity
    case IN_OBUS_N:
        tthis->m_obs_seen = false;
        tthis->m_cbs_seen = false;
        tthis->update_rbi();
        rv = (~tthis->m_obscbs_data) & 0xff;
        break;

    case IN_OBSCBS_ADDR:
        rv = (tthis->m_obscbs_offset << 5);  // bits [7:5]
        break;

    case IN_UART_RXRDY:
        rv = (tthis->m_terms[3].rx_ready ? 0x08 : 0x00)
           | (tthis->m_terms[2].rx_ready ? 0x04 : 0x00)
           | (tthis->m_terms[1].rx_ready ? 0x02 : 0x00)
           | (tthis->m_terms[0].rx_ready ? 0x01 : 0x00);
        break;

    case IN_UART_DATA:
        rv = term.rx_byte;
        // reading the data has the side effect of clearing the rxrdy status
        term.rx_ready = false;
        tthis->update_interrupt();
        break;

    case IN_UART_STATUS:
        {
        bool tx_empty = term.tx_ready && !term.tx_tmr;
        bool dsr = (term_num < 1); // FIXME: tie it to the number of terminal instances
        rv = (term.tx_ready ? 0x01 : 0x00)  // [0] = tx fifo empty
           | (term.rx_ready ? 0x02 : 0x00)  // [1] = rx fifo has a byte
           | (tx_empty      ? 0x04 : 0x00)  // [2] = tx serializer and fifo empty
           | (false         ? 0x08 : 0x00)  // [3] = parity error
           | (false         ? 0x10 : 0x00)  // [4] = overrun error
           | (false         ? 0x20 : 0x00)  // [5] = framing error
           | (false         ? 0x40 : 0x00)  // [6] = break detect
           | (dsr           ? 0x80 : 0x00); // [7] = data set ready
        }
        break;

    default:
        assert(false);
    }

    return rv;
}

void
IoCardTermMux::i8080_out_func(int addr, int byte, void *user_data)
{
    assert(byte == (byte & 0xff));
    IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);

    switch (addr) {

    case OUT_CLR_PRIME:
        tthis->m_prime_seen = false;
        break;

    case OUT_IB_N:
        byte = (~byte & 0xff);
        tthis->m_cpu->IoCardCbIbs(byte);
        break;

    case OUT_IB9_N:
        byte = (~byte & 0xff);
        tthis->m_cpu->IoCardCbIbs(0x100 | byte);
        break;

    case OUT_PRIME:
        // issue (warm) reset
        System2200::reset(false);
        break;

    case OUT_HALT_STEP:
        tthis->m_cpu->halt();
        break;

    case OUT_UART_SEL:
        assert(byte == 0x00 || byte == 0x01 || byte == 0x02 || byte == 0x04 || byte == 0x08);
        tthis->m_uart_sel = (byte == 0x01) ? 0
                          : (byte == 0x02) ? 1
                          : (byte == 0x04) ? 2
                          : (byte == 0x08) ? 3
                                           : 0;
        break;

    case OUT_UART_DATA:
// FIXME: this should clear the txrdy status for (11/baudrate) seconds.
// but it is more complicated than that because there is a serialization
// buffer and a one byte fifo I believe.
        if (tthis->m_uart_sel == 0) {
            // FIXME: only one term is supported
            UI_displayChar(tthis->m_terms[0].wndhnd, (uint8)byte);
        }
        break;

    case OUT_UART_CMD:
        // this code emulates only those bits of 8251 functionality which the
        // MXD actually uses, and everything else is assumed to be configured
        // as the MXD configures the UARTs. nobody reading this should use this
        // code as a reference for 8251 behavior!
        // FIXME: it might be good to model overrun, in which case this code
        //        must reset the overrun bit when the clear command is given
        break;

    case OUT_RBI:
        tthis->m_rbi = byte;
        tthis->update_rbi();
        break;
    }
}

// vim: ts=8:et:sw=4:smarttab
