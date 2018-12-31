// The MXD Terminal Mux card contains an 8080, some EPROM and some RAM,
// along with four RS-232 ports.  The function is emulated at the chip level,
// meaning and embedded i8080 microprocessor emulates the actual ROM image
// from a real MXD card.
//
// These documents were especially useful in reverse engineering the card:
// - https://wang2200.org/docs/system/2200MVP_MaintenanceManual.729-0584-A.1-84.pdf
//     section F, page 336..., has schematics for the MXD board (7290-1, 7291-1)
// - https://wang2200.org/docs/internal/2236MXE_Documentation.8-83.pdf
// - Hand disassembly of MXD ROM image:
//     https://wang2200.org/2200tech/wang_2236mxd.lst

#include "Cpu2200.h"
#include "host.h"             // for dbglog()
#include "i8080.h"
#include "IoCardKeyboard.h"   // for key encodings
#include "IoCardTermMux.h"
#include "Scheduler.h"
#include "system2200.h"
#include "Terminal.h"
#include "TermMuxCfgState.h"
#include "Ui.h"

bool do_dbg = false;

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif

// the i8080 runs at 1.78 MHz
const int NS_PER_TICK = 561;

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
                             int base_addr, int card_slot,
                             const CardCfgState *cfg) :
    m_scheduler(scheduler),
    m_cpu(cpu),
    m_i8080(nullptr),
    m_base_addr(base_addr),
    m_slot(card_slot),
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
    m_interrupt_pending(false)
{
    if (m_slot < 0) {
        // this is just a probe to query properties, so don't make a window
        return;
    }

    // TermMux configuration state
    assert(cfg != nullptr);
    const TermMuxCfgState *cp = dynamic_cast<const TermMuxCfgState*>(cfg);
    assert(cp != nullptr);
    m_cfg = *cp;
    // TODO: why have m_num_terms if we have access to m_cfg?
    m_num_terms = m_cfg.getNumTerminals();
    assert(1 <= m_num_terms && m_num_terms <= 4);

    for (auto &t : m_terms) {
        t.terminal = nullptr;

        t.rx_ready = false;
        t.rx_byte  = 0x00;

        t.tx_ready = true;
        t.tx_byte  = 0x00;
        t.tx_tmr   = nullptr;
    }

    int io_addr = 0;
    const bool ok = system2200::getSlotInfo(card_slot, nullptr, &io_addr);
    assert(ok);

    m_i8080 = i8080_new(IoCardTermMux::i8080_rd_func,
                        IoCardTermMux::i8080_wr_func,
                        IoCardTermMux::i8080_in_func,
                        IoCardTermMux::i8080_out_func,
                        this);
    assert(m_i8080);
    i8080_reset(static_cast<i8080*>(m_i8080));

    // register the i8080 for clock callback
    clkCallback cb = std::bind(&IoCardTermMux::execOneOp, this);
    system2200::registerClockedDevice(cb);

    // create all the terminals
    for(int n=0; n<m_num_terms; n++) {
        m_terms[n].terminal =
            std::make_unique<Terminal>(scheduler, this,
                                       io_addr, n, UI_SCREEN_2236DE);
    }
}


// instance destructor
IoCardTermMux::~IoCardTermMux()
{
    if (m_slot >= 0) {
        // not just a temp object, so clean up
        i8080_destroy(static_cast<i8080*>(m_i8080));
        m_i8080 = nullptr;
        for (auto &t : m_terms) {
            t.terminal = nullptr;
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
#if 0
    // FIXME: running with more than one MXD causes MVP OS to hang, for reasons
    // I have yet to debug.  having more than one MXD is unwieldy anyway.
    std::vector<int> v { 0x00, 0x40, 0x80, 0xc0 };
#else
    std::vector<int> v { 0x00, };
#endif
    return v;
}


// return the list of addresses that this specific card responds to
std::vector<int>
IoCardTermMux::getAddresses() const
{
    std::vector<int> v;
    for (int i=1; i<8; i++) {
        v.push_back(m_base_addr + i);
    }
    return v;
}

// -----------------------------------------------------
// configuration management
// -----------------------------------------------------

// subclass returns its own type of configuration object
std::shared_ptr<CardCfgState>
IoCardTermMux::getCfgState()
{
    return std::make_unique<TermMuxCfgState>();
}


// modify the existing configuration state
void
IoCardTermMux::setConfiguration(const CardCfgState &cfg) noexcept
{
    const TermMuxCfgState &ccfg(dynamic_cast<const TermMuxCfgState&>(cfg));

    // FIXME: do sanity checking to make sure things don't change at a bad time?
    //        perhaps queue this change until the next WAKEUP phase?
    m_cfg = ccfg;
};


// -----------------------------------------------------
// operational
// -----------------------------------------------------

// the MXD card has its own power-on-reset circuit. all !PRMS (prime reset)
// does is set a latch that the 8080 can sample.  the latch is cleared
// via "OUT 0".
// interestingly, the reset pin on the i8251 uart (pin 21) is tied low
// i.e., it doesn't have a hard reset.
void
IoCardTermMux::reset(bool /*hard_reset*/) noexcept
{
    m_prime_seen = true;
}


void
IoCardTermMux::select()
{
    m_io_offset = (m_cpu->getAB() & 7);

    if (do_dbg) {
        dbglog("TermMux/%02x +ABS %02x\n", m_base_addr, m_base_addr+m_io_offset);
    }

    // offset 0 is not handled
    if (m_io_offset == 0) {
        return;
    }
    m_selected = true;

    updateRbi();
}


void
IoCardTermMux::deselect()
{
    if (do_dbg) {
        dbglog("TermMux/%02x -ABS %02x\n", m_base_addr, m_base_addr+m_io_offset);
    }
    m_cpu->setDevRdy(false);

    m_selected = false;
    m_cpb      = true;
}


void
IoCardTermMux::strobeOBS(int val)
{
    val &= 0xFF;
    if (do_dbg) {
        dbglog("TermMux/%02x OBS: byte 0x%02x\n", m_base_addr, val);
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

    updateRbi();
}


void
IoCardTermMux::strobeCBS(int val)
{
    val &= 0xFF;
    if (do_dbg) {
        dbglog("TermMux/%02x CBS: byte 0x%02x\n", m_base_addr, val);
    }

    // any previous obs or cbs should have been serviced before we see another
    assert(!m_obs_seen && !m_cbs_seen);

    m_cbs_seen = true;
    m_obscbs_offset = m_io_offset;  // secondary address offset latch
    m_obscbs_data = val;

    updateRbi();
}


// weird hack Wang used to signal the attached display is 64x16 (false)
// or 80x24 (true).  All smart terminals are 80x24, but in boot mode/vp mode,
// the term mux looks like a dumb terminal at 05, so it drives this to let
// the ucode know it is 80x24.
int
IoCardTermMux::getIB() const noexcept
{
    // TODO: do we need to give more status than this?
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
IoCardTermMux::setCpuBusy(bool busy)
{
    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (do_dbg) {
        dbglog("TermMux/%02x CPB%c\n", m_base_addr, busy ? '+' : '-');
    }
    m_cpb = busy;
}


// perform on instruction and return the number of ns of elapsed time.
int
IoCardTermMux::execOneOp() noexcept
{
    if (m_interrupt_pending) {
        // vector to 0x0038 (rst 7)
        i8080_interrupt(static_cast<i8080*>(m_i8080), 0xFF);
    }

    const int ticks = i8080_exec_one_op(static_cast<i8080*>(m_i8080));
    if (ticks > 30) {
        // it is in an error state
        return 4 * NS_PER_TICK;
    }
    return ticks * NS_PER_TICK;
}


// update the board's !ready/busy status (if selected)
void
IoCardTermMux::updateRbi() noexcept
{
    // don't drive !rbi if the board isn't selected
    if (m_io_offset == 0 || !m_selected) {
        return;
    }

    const bool busy = ((m_obs_seen || m_cbs_seen) && (m_io_offset >= 4))
                   || !!((m_rbi >> (m_io_offset-1)) & 1);

    m_cpu->setDevRdy(!busy);
}


void
IoCardTermMux::updateInterrupt() noexcept
{
    m_interrupt_pending = m_terms[0].rx_ready
                       || m_terms[1].rx_ready
                       || m_terms[2].rx_ready
                       || m_terms[3].rx_ready;
}


// a character has come in from the serial port
void
IoCardTermMux::receiveKeystroke(int term_num, int keycode)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];

    if (term.rx_ready) {
#ifdef _DEBUG
        UI_warn("terminal received char too fast");
#endif
        // TODO: set uart overrun status bit?
        // then fall through because the old char is overwritten
    }
    term.rx_ready = true;
    term.rx_byte  = keycode;

    updateInterrupt();
}


void
IoCardTermMux::checkTxBuffer(int term_num)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];

    if (term.tx_ready || term.tx_tmr) {
        // nothing to do or serial channel is in use
        return;
    }

    term.tx_tmr = m_scheduler->createTimer(
                      Terminal::serial_char_delay,
                      std::bind(&IoCardTermMux::mxdToTermCallback, this, term_num, term.tx_byte)
                  );

    // the byte in the tx register is moved to the serializer,
    // making room for the next tx byte
    term.tx_ready = true;
}


// this causes a delay of 1/char_time before posting a byte to the terminal.
// more than the latency, it is intended to rate limit the channel to match
// that of a real serial terminal.
void
IoCardTermMux::mxdToTermCallback(int term_num, int byte)
{
    assert((0 <= term_num) && (term_num < MAX_TERMINALS));
    m_term_t &term = m_terms[term_num];
    term.tx_tmr = nullptr;

    term.terminal->processChar(static_cast<uint8>(byte));
    checkTxBuffer(term_num);
}

// ============================================================================
// i8080 CPU modeling
// ============================================================================

int
IoCardTermMux::i8080_rd_func(int addr, void *user_data) noexcept
{
    if (addr < 0x1000) {
        // read 4K eprom
        return mxd_eprom[addr & 0x0FFF];
    }

    if ((0x2000 <= addr) && (addr < 0x3000)) {
        // read 4KB ram
        const IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
        assert(tthis != nullptr);
        return tthis->m_ram[addr & 0x0FFF];
    }

    assert(false);
    return 0x00;
}


void
IoCardTermMux::i8080_wr_func(int addr, int byte, void *user_data) noexcept
{
    assert(byte == (byte & 0xff));

    if ((0x2000 <= addr) && (addr < 0x3000)) {
        // write 4KB ram
        IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
        assert(tthis != nullptr);
        tthis->m_ram[addr & 0x0FFF] = static_cast<uint8>(byte);
        return;
    }
    assert(false);
}


int
IoCardTermMux::i8080_in_func(int addr, void *user_data) noexcept
{
    IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
    assert(tthis != nullptr);
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
        const bool cpu_waiting = tthis->m_selected && !tthis->m_cpb;  // CPU waiting for input
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
        tthis->updateRbi();
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
        tthis->updateInterrupt();
        break;

    case IN_UART_STATUS:
        {
        const bool tx_empty = term.tx_ready && !term.tx_tmr;
        const bool dsr = (term_num < tthis->m_num_terms);
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
    IoCardTermMux *tthis = static_cast<IoCardTermMux*>(user_data);
    assert(tthis != nullptr);
    assert(byte == (byte & 0xff));

    switch (addr) {

    case OUT_CLR_PRIME:
        tthis->m_prime_seen = false;
        break;

    case OUT_IB_N:
        byte = (~byte & 0xff);
        if (do_dbg) {
            dbglog("TermMux/%02x IB=%02x\n", tthis->m_base_addr, byte);
        }
        tthis->m_cpu->ioCardCbIbs(byte);
        break;

    case OUT_IB9_N:
        byte = (~byte & 0xff);
        if (do_dbg) {
            dbglog("TermMux/%02x IB=%03x\n", tthis->m_base_addr, 0x100 | byte);
        }
        tthis->m_cpu->ioCardCbIbs(0x100 | byte);
        break;

    case OUT_PRIME:
        // issue (warm) reset
        system2200::reset(false);
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
        if (tthis->m_uart_sel < tthis->m_num_terms) {
            int term_num   = tthis->m_uart_sel;
            m_term_t &term = tthis->m_terms[term_num];
#if defined(_DEBUG)
            if (!term.tx_ready) {
                UI_warn("terminal %d mxd overwrote the uart tx buffer", term_num+1);
            }
#endif
            term.tx_ready = false;
            term.tx_byte  = byte;
            tthis->checkTxBuffer(term_num);
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
        tthis->updateRbi();
        break;
    }
}

// vim: ts=8:et:sw=4:smarttab
