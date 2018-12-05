// This code emulates the 64x16/80x24 display controller.
//
// If the realtime mode is enabled, the timing of the original display
// controller is approximated.  Basically the ucode always checks to see
// if the card is busy before it sends any byte.  When a byte is received,
// the card asserts its busy flag until the requested operation is done.
// The timing to complete each operation is as follows
//    printable characters:
//        (see p. 6-8 and p. 6-13 of the 2200 service manual)
//        printable characters must wait until horizontal blanking
//        before they get saved into the display RAM, then busy is cleared.
//    screen clear:
//        (see p. 6-7 and p. 6-11 of the 2200 service manual)
//        the controller waits for vertical blanking, then it writes
//        a space to each character at the rate it would normally be
//        reading the bytes for display.  after all characters have
//        been written to spaces, the busy flag is cleared.
//    scroll event:
//        (see p. 6-11 of the 2200 service manual, called a "roll")
//        the description of the actions is hard to figure out without
//        detailed study of the schematic as several one-shots are
//        involved and I'm too lazy to decipher it.  it sounds like
//        after a short delay, a state machine writes the 64 characters
//        of the exposed row to spaces and then clears the busy flag.
//        instead, what the emulator implements is this:
//        the controller waits for a horizontal sync, then it writes
//        one row of characters to all spaces in the time it would
//        normally use to display one row of dots -- that is one
//        horizontal sweep period -- then busy is dropped.

/* character generation details
   + see p. 5-2 of the service manual (p 170 of 414)
        for 64x16
            - 5x7 dot matrix in a 7x15 character cell
                - scanline 1 is blank
                - scanlines 2-8 are 7 rows of character data
                - scanlines 9 is always blank
                - scanline 10 is optional cursor, which is 7 dots wide
                - scanlines 11-15 are blank
                - no underline
            - field is 256 rows; 1-240 are data, 241-256 are vblank
            - 60 Hz update rate
            - dot clock is 116 ns; char clock is 812 ns (7 dots)
            - 80 character times per scanline (65 uS)
        for 80x24
            - 8 rows of pixel data
            - 1 row of optional underline (>=0x80)
            - 1 row of cursor
            - 1 blank row
*/

#include "Ui.h"
#include "IoCardDisplay.h"
#include "Scheduler.h"          // for Timer...() functions
#include "System2200.h"
#include "Cpu2200.h"

#define NOISY  0        // turn on some debugging messages
#ifdef _MSC_VER
    #pragma warning( disable: 4127 )  // conditional expression is constant
#endif

// number of scanlines per display field
static const int num_scanlines = 256;

// horizontal refresh period = (1M us/s) / (60 frames/s * 256 rows/frame)
#define HSYNC_PERIOD (TIMER_US(65))

// vertical refresh period
#define VSYNC_PERIOD (HSYNC_PERIOD * num_scanlines)


// the logic of the card busy status is as follows.
//
// (1) the m_tmr_hsync timer is always running, no matter whether the user
//     wants realtime operation or not.  having this timer fire every 65 uS
//     hurts performance about 10% on my celeron 667 machine.  to minimize
//     this cost, once per vblank the state of the "realtime or not" boolean
//     is checked.  if realtime isn't requested, then the timer is set to
//     fire at the next vblank.  this cuts down on overhead by 256x.  the
//     reason for insisting it always run is that it cuts down on
//     complications of starting it/stopping it at arbitrary times.
//
// (2) the busy flag is always set on OBS.
//
// (3) as the character received by OBS is analyzed, some control codes
//     are acted upon and clear the busy state.  otherwise, the character's
//     effect is handled immediately and a small state machine is set up to
//     determine when to clear the busy state.  this state is advanced by
//     the m_tmr_hsync timer event.

// instance constructor
IoCardDisplay::IoCardDisplay(std::shared_ptr<Scheduler> scheduler,
                             std::shared_ptr<Cpu2200>   cpu,
                             int baseaddr, int cardslot, int size) :
    m_scheduler(scheduler),
    m_cpu(cpu),
    m_baseaddr(baseaddr),
    m_slot(cardslot),
    m_selected(false),
    m_card_busy(false),
    m_size(size),
    m_wndhnd(nullptr),
    m_tmr_hsync(nullptr),
    m_realtime(true),
    m_hsync_count(0),
    m_busy_state(busy_state::IDLE)
{
    if (m_slot < 0) {
        // this is just a probe to query properties, so don't make a window
        return;
    }

    int io_addr;
    bool ok = System2200::getSlotInfo(cardslot, 0, &io_addr);
    assert(ok); ok=ok;
    m_wndhnd = UI_initCrt(m_size, io_addr, 0);

    reset(true);
}

// instance destructor
IoCardDisplay::~IoCardDisplay()
{
    if (m_slot >= 0) {
        reset(true);    // turns off handshakes in progress
        UI_destroyCrt(m_wndhnd);
    }
}

const std::string
IoCardDisplay::getDescription() const
{
    return (m_size == UI_SCREEN_64x16) ? "64x16 CRT Controller"
                                       : "80x24 CRT Controller";
}

const std::string
IoCardDisplay::getName() const
{
    return (m_size == UI_SCREEN_64x16) ? "6312A" : "7011";
}

// return a list of the various base addresses a card can map to
// list of common I/O addresses for this device taken from p. 2-5 of
// the default comes first.
std::vector<int>
IoCardDisplay::getBaseAddresses() const
{
    std::vector<int> v {0x005, 0x006, 0x007 };
    return v;
}

// return the list of addresses that this specific card responds to
std::vector<int>
IoCardDisplay::getAddresses() const
{
    std::vector<int> v;
    v.push_back( m_baseaddr );
    return v;
}

void
IoCardDisplay::reset(bool hard_reset)
{
    // reset card state
    m_busy_state = busy_state::IDLE;
    m_selected   = false;
    m_card_busy  = false;

    // get the horizontal sync timer going
    if (m_tmr_hsync != nullptr) {
        m_tmr_hsync->Kill();
    }
    m_tmr_hsync = nullptr;
    m_hsync_count = 0;
    tcbHsync(0);

    hard_reset = hard_reset;    // silence lint
}

void
IoCardDisplay::select()
{
    if (NOISY) {
        UI_Info("display ABS");
    }

    m_selected = true;
    m_cpu->setDevRdy(!m_card_busy);
}

void
IoCardDisplay::deselect()
{
    if (NOISY) {
        UI_Info("display -ABS");
    }
    m_cpu->setDevRdy(false);

    m_selected = false;
}

void
IoCardDisplay::OBS(int val)
{
    assert(m_busy_state == busy_state::IDLE);

    val &= 0xFF;

    if (NOISY) {
        UI_Info("display OBS: Output of byte 0x%02x", val);
    }

    UI_displayChar(m_wndhnd, (uint8)val);

    if (System2200::isCpuSpeedRegulated()) {
        if (val == 0x03) {
            m_busy_state = busy_state::CLEAR1;
            m_card_busy = true;
#if 0
        } else if (val == 0x0A && cur_row == 15) {
            m_busy_state = busy_state::ROLL1;
            m_card_busy = true;
#endif
        } else if (val >= 0x10) {
            m_busy_state = busy_state::CHAR;
            m_card_busy = true;
        }
    }

    m_cpu->setDevRdy(!m_card_busy);
}

void
IoCardDisplay::CBS(int val)
{
    val &= 0xFF;

#if 0
    // handle it if card uses it instead of the message here
#else
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_Warn("unexpected display CBS: Output of byte 0x%02x", val);
    }
#endif
}

int
IoCardDisplay::getIB() const
{
    // the system uses this to determine screen size so effectively either
    //    SELECT PRINT 005(64), CO 005(64), LIST 005(64)
    // or
    //    SELECT PRINT 005(80), CO 005(80), LIST 005(80)
    // is performed on reset.
    return (m_size == UI_SCREEN_80x24) ? 0x10 : 0x00;
}

// change of CPU Busy state
// because the display is write-only, we don't expect the CPU
// to poll us for input.
void
IoCardDisplay::CPB(bool busy)
{
    busy = busy; // make lint happy

    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (NOISY) {
        UI_Info("display CPB%c", busy?'+':'-');
    }

    m_cpu->setDevRdy(!m_card_busy);
}


// horizontal sync timer callback
void
IoCardDisplay::tcbHsync(int arg)
{
    const bool regulated = System2200::isCpuSpeedRegulated();

    m_hsync_count++;

    int64 new_period;
    if (regulated && (m_hsync_count < num_scanlines)) {
        // end of horizontal sweep; do another
        new_period = HSYNC_PERIOD;
    } else {
        // once per vertical refresh -- just to keep the timer alive
        new_period = VSYNC_PERIOD;
        m_hsync_count = 1;
    }

    // retrigger the timer
#if 0
    m_tmr_hsync = m_scheduler->TimerCreate( new_period,
                                            [&](){ tcbHsync(arg); } );
#else
    m_tmr_hsync = m_scheduler->TimerCreate( new_period,
                            std::bind(&IoCardDisplay::tcbHsync, this, arg) );
#endif

    // advance state machine
    switch (m_busy_state) {

        case busy_state::IDLE:
            break;

        case busy_state::CHAR:
            m_card_busy = false;
            m_busy_state = busy_state::IDLE;
            m_cpu->setDevRdy(true);
            break;

        case busy_state::CLEAR1:
            if (m_hsync_count == 1) {    // vblank
                m_busy_state = busy_state::CLEAR2;
            }
            break;
        case busy_state::CLEAR2:
            if (m_hsync_count == 1) {    // vblank
                m_card_busy = false;
                m_busy_state = busy_state::IDLE;
                m_cpu->setDevRdy(true);
            }
            break;

        case busy_state::ROLL1:
            m_busy_state = busy_state::ROLL2;
            break;
        case busy_state::ROLL2:
            m_card_busy = false;
            m_busy_state = busy_state::IDLE;
            m_cpu->setDevRdy(true);
            break;

        default:
            assert(false);
            break;
    }
}

// vim: ts=8:et:sw=4:smarttab
