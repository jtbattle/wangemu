// This code emulates the keyboard controller.

#include "Cpu2200.h"
#include "IoCardKeyboard.h"
#include "Scheduler.h"
#include "Ui.h"
#include "system2200.h"

#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif

// instance constructor
IoCardKeyboard::IoCardKeyboard(std::shared_ptr<Scheduler> scheduler,
                               std::shared_ptr<Cpu2200>   cpu,
                               int base_addr, int card_slot) :
    m_scheduler(scheduler),
    m_cpu(cpu),
    m_base_addr(base_addr),
    m_slot(card_slot)
{
    if (m_slot >= 0) {
        reset(true);
        system2200::registerKb(
            m_base_addr, 0,
            std::bind(&IoCardKeyboard::receiveKeystroke, this, std::placeholders::_1)
        );
    }
}


// instance destructor
IoCardKeyboard::~IoCardKeyboard()
{
    if (m_slot >= 0) {
        reset(true);  // turns off handshakes in progress
        system2200::unregisterKb(m_base_addr, 0);
    }
}


std::string
IoCardKeyboard::getDescription() const
{
    return "Keyboard Controller";
}


std::string
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
    v.push_back(m_base_addr);
    return v;
}


void
IoCardKeyboard::reset(bool /*hard_reset*/) noexcept
{
    m_tmr_script = nullptr;

    // reset card state
    m_selected  = false;
    m_key_ready = false;   // no pending keys
    m_cpb       = true;    // CPU busy, presumably
}


void
IoCardKeyboard::select()
{
    if (NOISY) {
        UI_info("keyboard ABS");
    }

    m_selected = true;
    checkKeyReady(); // doesn't seem to matter if it is here or not
}


void
IoCardKeyboard::deselect()
{
    if (NOISY) {
        UI_info("keyboard -ABS");
    }

    m_selected = false;
    m_cpb      = true;
}


void
IoCardKeyboard::strobeOBS(int val)
{
    if (NOISY) {
        UI_warn("unexpected keyboard OBS: Output of byte 0x%02x", val);
    }
}


void
IoCardKeyboard::strobeCBS(int /*val*/) noexcept
{
#if 0
    int val8 = val & 0xFF;
    // unexpected -- the real hardware ignores this byte
    if (NOISY) {
        UI_warn("unexpected keyboard CBS: Output of byte 0x%02x", val8);
    }
#endif
}


// change of CPU Busy state
void
IoCardKeyboard::setCpuBusy(bool busy)
{
    if (NOISY) {
        UI_info("keyboard CPB%c", busy ? '+' : '-');
    }

    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    m_cpb = busy;
    checkKeyReady();
}


// ================== keyboard specific public functions =================

void
IoCardKeyboard::receiveKeystroke(int keycode)
{
    assert((keycode >= 0));

    if (keycode == KEYCODE_RESET) {
        // warm reset
        system2200::reset(false);
    } else if (keycode == KEYCODE_HALT) {
        // halt/step
        m_key_ready = false;
        m_cpu->halt();
    } else {
        m_key_code  = keycode;
        m_key_ready = true;
    }

    checkKeyReady();
}

// =================== private functions ===================

void
IoCardKeyboard::tcbScript()
{
    if (m_selected) {
        assert(!m_cpb);
        if (m_key_ready) {
            m_cpu->ioCardCbIbs(m_key_code);
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
IoCardKeyboard::checkKeyReady()
{
    if (!m_key_ready) {
        system2200::pollScriptInput(m_base_addr, 0);
    }
    if (m_selected) {
        if (m_key_ready && !m_cpb) {
            // we can't return IBS right away -- apparently there
            // must be some delay otherwise the handshake breaks
            if (m_tmr_script == nullptr) {
                m_tmr_script = m_scheduler->createTimer(
                        TIMER_US(50),     // 30 is OK, 20 is too little
                        [&](){ tcbScript(); });
            }
        }
        m_cpu->setDevRdy(m_key_ready);
    }
}

// vim: ts=8:et:sw=4:smarttab
