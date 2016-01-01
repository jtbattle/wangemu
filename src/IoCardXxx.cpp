// This is a skeleton file for patterning future I/O devices
// When creating a new card type, beside filling in thte skeleton code,
// there is one step that must be performed.  An enum symbol for the new
// card must be added to card_type_e in IoCard.h, and the card factory
// function, IoCard::makeCardImpl(), must be taught to associate the new
// class with this enum.

#include "Ui.h"
#include "IoCardXxx.h"
#include "Cpu2200.h"


#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif


// instance constructor
IoCardXxx::IoCardXxx(Cpu2200 &cpu, int baseaddr, int cardslot) :
    m_cpu(cpu),
    m_baseaddr(baseaddr),
    m_slot(cardslot)
{
    if (m_slot >= 0) {
        // ... other creation functions
        reset();
    } else {
        // this is just a probe to determine card properties
    }
}

// instance destructor
IoCardXxx::~IoCardXxx()
{
    if (m_slot >= 0) {
        // not just a temp object, so clean up
        reset();        // turns off handshakes in progress

        // free allocated resources
        // ...
    }
}

const string
IoCardXxx::getDescription() const
{
    return "Card Description";
}

const string
IoCardXxx::getName() const
{
    return "Card Name (eg, 6541)";
}

// return a list of the various base addresses a card can map to
// the default comes first.
vector<int>
IoCardXxx:getBaseAddresses() const
{
    vector<int> v { 0x710, 0x720, 0x730 };  // e.g.
    return v;
}

// return the list of addresses that this specific card responds to
vector<int>
IoCardXxx::getAddresses() const
{
    vector<int> v;
    v.push_back( m_baseaddr );
    return v;
}

void
IoCardXxx::reset(int hard_reset)
{
    // reset card state
    m_selected   = false;
    m_cpb        = true;   // CPU busy
    m_card_busy  = 0;
    ...
}

void
IoCardXxx::select()
{
    if (NOISY)
        UI_Info("xxx ABS");

    m_selected = true;
    m_cpu.setDevRdy(!m_card_busy);
    // ...
}

void
IoCardXxx::deselect()
{
    if (NOISY)
        UI_Info("xxx -ABS");

    m_selected = false;
    m_cpb      = true;
    // ...
}

void
IoCardXxx::OBS(int val)
{
    val &= 0xFF;

    if (NOISY)
        UI_Info("xxx OBS: Output of byte 0x%02x", val);

    // ...
    m_cpu.setDevRdy(!m_card_busy);
}

void
IoCardXxx::CBS(int val)
{
    val &= 0xFF;

#if 0
    // handle it if card uses it instead of the message here
#else
    // unexpected -- the real hardware ignores this byte
    if (NOISY)
        UI_Warn("unexpected xxx CBS: Output of byte 0x%02x", val);
#endif
}

int
IoCardXxx::GetIB5() const
{
    return 0;   // this card doesn't use this feature (or change if it does)
}

// change of CPU Busy state
void
IoCardXxx::CPB(bool busy)
{
    assert(val == 0 || val == 1);

    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (NOISY)
        UI_Info("xxx CPB%c", val?'+':'-');

    m_cpb = busy;
    // ...
    m_cpu.setDevRdy(!m_card_busy);
}

// vim: ts=8:et:sw=4:smarttab
