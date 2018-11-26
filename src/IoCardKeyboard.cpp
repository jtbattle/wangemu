// This code emulates the keyboard controller.

#include "Ui.h"
#include "IoCardKeyboard.h"
#include "Cpu2200.h"
#include "Scheduler.h"
#include "System2200.h"

#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif

// instance constructor
IoCardKeyboard::IoCardKeyboard(std::shared_ptr<Scheduler> scheduler,
                               std::shared_ptr<Cpu2200>   cpu,
                               int baseaddr, int cardslot) :
    m_scheduler(scheduler),
    m_tmr_script(nullptr),
    m_cpu(cpu),
    m_baseaddr(baseaddr),
    m_slot(cardslot),
    m_selected(false),
    m_cpb(true),
    m_key_ready(false),
    m_key_code(0)
{
    if (m_slot >= 0) {
        reset();
        System2200().kb_register(
            m_baseaddr, 0,
            bind(&IoCardKeyboard::receiveKeystroke, this, std::placeholders::_1)
        );
    }
}

// instance destructor
IoCardKeyboard::~IoCardKeyboard()
{
    if (m_slot >= 0) {
        reset();        // turns off handshakes in progress
        System2200().kb_unregister(m_baseaddr, 0);
    }
}

const std::string
IoCardKeyboard::getDescription() const
{
    return "Keyboard Controller";
}

const std::string
IoCardKeyboard::getName() const
{
    return "6367";
}

// return a list of the various base addresses a card can map to.
// the default comes first.
std::vector<int>
IoCardKeyboard::getBaseAddresses() const
{
    std::vector<int> v { 0x001, 0x002, 0x003, 0x004 };
    return v;
}

// return the list of addresses that this specific card responds to
std::vector<int>
IoCardKeyboard::getAddresses() const
{
    std::vector<int> v;
    v.push_back( m_baseaddr );
    return v;
}

void
IoCardKeyboard::reset(bool hard_reset)
{
    m_tmr_script = nullptr;

    // reset card state
    m_selected  = false;
    m_key_ready = false;   // no pending keys
    m_cpb       = true;    // CPU busy, presumably

    hard_reset = hard_reset;    // silence lint
}

void
IoCardKeyboard::select()
{
    if (NOISY) {
        UI_Info("keyboard ABS");
    }

    m_selected = true;
    check_keyready(); // doesn't seem to matter if it is here or not
}

void
IoCardKeyboard::deselect()
{
    if (NOISY) {
        UI_Info("keyboard -ABS");
    }

    m_selected = false;
    m_cpb      = true;
}

void
IoCardKeyboard::OBS(int val)
{
    if (NOISY) {
        UI_Warn("unexpected keyboard OBS: Output of byte 0x%02x", val);
    }
}

void
IoCardKeyboard::CBS(int val)
{
    val &= 0xFF;

#if 0
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_Warn("unexpected keyboard CBS: Output of byte 0x%02x", val);
    }
#endif
}

// change of CPU Busy state
void
IoCardKeyboard::CPB(bool busy)
{
    if (NOISY) {
        UI_Info("keyboard CPB%c", busy?'+':'-');
    }

    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    m_cpb = busy;
    check_keyready();
}


// ================== keyboard specific public functions =================

void
IoCardKeyboard::receiveKeystroke(int keycode)
{
    assert( (keycode >= 0) );

    if (keycode & KEYCODE_HALT) {
        m_key_ready = false;
        m_cpu->halt();
    } else {
        m_key_code  = keycode;
        m_key_ready = true;
    }

    check_keyready();
}

// =================== private functions ===================

// FIXME: if nothing else, the name is bad because IoCardKeyboard
// no longer knows about scripts -- System2200 takes care of it.
void
IoCardKeyboard::tcbScript()
{
    if (m_selected) {
        assert(!m_cpb);
        if (m_key_ready) {
            m_cpu->IoCardCbIbs(m_key_code);
            m_key_ready = false;
        }
        m_cpu->setDevRdy(m_key_ready);
    }

    m_tmr_script = nullptr;
}


// this function should be safe to call any time; it internally makes
// sure not to change any state when it isn't safe to do so.  if we are
// in script_mode, it will first fill the pending keystroke slot with
// the next character from the script.  next, if the keyboard is selected
// and a keystroke is pending, it will schedule a callback in a few uS.
// that callback is responsible for completing the handshake to deliver
// the keystroke.  the reason we wait instead of doing it immediately is
// that empirically such a delay is required otherwise the ucode may
// drop characters.
void
IoCardKeyboard::check_keyready()
{
    if (!m_key_ready) {
        System2200().kb_keyReady(m_baseaddr, 0);
    }
// FIXME: keyReady doesn't change m_selected, so the above call can't affect
// it this cycle.  maybe the thing to do is after m_key_ready is set to
// false (no matter the reason) a timer to tcbScript is invoked.  or something.
// think it through.
    if (m_selected) {
        if (m_key_ready && !m_cpb) {
            // we can't return IBS right away -- apparently there
            // must be some delay otherwise the handshake breaks
            if (m_tmr_script == nullptr) {
                m_tmr_script = m_scheduler->TimerCreate(
                        TIMER_US(50),     // 30 is OK, 20 is too little
                        [&](){ tcbScript(); } );
            }
        }
        m_cpu->setDevRdy(m_key_ready);
    }
}

// vim: ts=8:et:sw=4:smarttab
