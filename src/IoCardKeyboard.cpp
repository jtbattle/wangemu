// This code emulates the keyboard controller.

#include "Ui.h"
#include "IoCardKeyboard.h"
#include "Cpu2200.h"
#include "Scheduler.h"
#include "ScriptFile.h"
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
    m_key_code(0),
    m_script_handle(nullptr)
{
    if (m_slot >= 0) {
        reset();
    }
}

// instance destructor
IoCardKeyboard::~IoCardKeyboard()
{
    if (m_slot >= 0) {
        reset();        // turns off handshakes in progress
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
    m_script_handle = nullptr;

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
    // handle it if card uses it instead of the message here
#else
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

// this is called by UI when a key is entered by the user
void
IoCardKeyboard::receiveKeystroke(int io_addr, int keycode)
{
    assert( (io_addr >= 0) && (io_addr <= 255) );
    assert( (keycode >= 0) );

    IoCardKeyboard *tthis = static_cast<IoCardKeyboard*>
                                (System2200().getInstFromIoAddr(io_addr));
    assert(tthis != nullptr);

    // halt apparently acts independently of addressing
    if (keycode & KEYCODE_HALT) {
        if (tthis->m_script_handle) {
            // cancel any script in progress
            tthis->m_tmr_script    = nullptr;
            tthis->m_script_handle = nullptr;
            tthis->m_key_ready     = false;
        }
        tthis->m_cpu->halt();
        return;
    }

    if (tthis->m_script_handle) {
        return;         // ignore any other keyboard if script is in progress
    }

    tthis->m_key_code  = keycode;
    tthis->m_key_ready = true;

    tthis->check_keyready();
}


// returns 1 if the keyboard at the specified io_addr
// is already processing a script
bool
IoCardKeyboard::script_mode(int io_addr)
{
    assert( (io_addr >= 0) && (io_addr <= 255) );

    IoCardKeyboard *tthis = static_cast<IoCardKeyboard*>
                                (System2200().getInstFromIoAddr(io_addr));
    if (tthis == nullptr) {
        // this can happen during initialization when there are two keyboards
        return false;
    }
    return (!!tthis->m_script_handle);
}


// open up a script for keyboard input.  returns true on success.
bool
IoCardKeyboard::invoke_script(const int io_addr, const std::string &filename)
{
    assert( (io_addr >= 0) && (io_addr <= 255) );

    IoCardKeyboard *tthis = static_cast<IoCardKeyboard*>
                                (System2200().getInstFromIoAddr(io_addr));
    assert(tthis != nullptr);
    assert(!tthis->m_script_handle);  // can't have two scripts to one kb

    int flags = ScriptFile::SCRIPT_META_INC |
                ScriptFile::SCRIPT_META_HEX |
                ScriptFile::SCRIPT_META_KEY ;
    tthis->m_script_handle = std::make_unique<ScriptFile>(
                                filename, flags, 3 /*max nesting*/);

    if (!tthis->m_script_handle->openedOk()) {
        tthis->m_script_handle = nullptr;
        return false;
    }

    // possibly get the first character
    tthis->check_keyready();

    return true;
}


// =================== private functions ===================

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
    script_poll();
    if (m_selected) {
        if (m_key_ready && !m_cpb) {
            // we can't return IBS right away -- apparently there
            // must be some delay otherwise the handshake breaks
            if (!m_tmr_script) {
                m_tmr_script = m_scheduler->TimerCreate(
                        TIMER_US(50),     // 30 is OK, 20 is too little
                        [&](){ tcbScript(); } );
            }
        }
        m_cpu->setDevRdy(m_key_ready);
    }
}


// poll the script buffer to fetch another character if
// one is available and there isn't already a key ready.
void
IoCardKeyboard::script_poll()
{
    if (m_script_handle && !m_key_ready) {
        int ch;
        bool rc = m_script_handle->getNextByte(&ch);
        if (rc) {
            // OK
            m_key_code  = ch;
            m_key_ready = true;
        } else {
            // EOF
            m_script_handle = nullptr;
            m_key_ready     = false;
        }
    }
}

// ====== redirector so callers don't need to drag in whole IoCard.h

bool
core_invokeScript(const int io_addr, const std::string &filename)
{
    return IoCardKeyboard::invoke_script(io_addr, filename);
}

bool
core_isKbInScriptMode(int io_addr)
{
    return IoCardKeyboard::script_mode(io_addr);
}

void
core_sysKeystroke(int io_addr, int keycode)
{
    IoCardKeyboard::receiveKeystroke(io_addr, keycode);
}

// vim: ts=8:et:sw=4:smarttab
