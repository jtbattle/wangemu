// Emulate a printer device.  Most of the work is done in the GUI side.

#include "Cpu2200.h"
#include "IoCardPrinter.h"
#include "Ui.h"
#include "system2200.h"

#define NOISY  0        // turn on some debugging messages

#ifdef _MSC_VER
#pragma warning( disable: 4127 )  // conditional expression is constant
#endif


IoCardPrinter::IoCardPrinter(std::shared_ptr<Cpu2200> cpu,
                             int base_addr, int card_slot) :
    m_cpu(cpu),
    m_base_addr(base_addr),
    m_slot(card_slot),
    m_wndhnd(nullptr)
{
    if (m_slot >= 0) {
        int io_addr;
        const bool ok = system2200::getSlotInfo(card_slot, nullptr, &io_addr);
        assert(ok);
        m_wndhnd = UI_printerInit(io_addr);
        reset(true);
    }
}


// instance destructor
IoCardPrinter::~IoCardPrinter()
{
    if (m_slot >= 0) {
        reset(true);  // turns off handshakes in progress
        UI_printerDestroy(getGuiPtr());
    }
}


const std::string
IoCardPrinter::getDescription() const
{
    return "Printer Controller";
}


const std::string
IoCardPrinter::getName() const
{
    return "7079";
}


// return a list of the various base addresses a card can map to
// the default comes first.
std::vector<int>
IoCardPrinter::getBaseAddresses() const
{
    std::vector<int> v { 0x215, 0x216 };
    return v;
}


// return the list of addresses that this specific card responds to
std::vector<int>
IoCardPrinter::getAddresses() const
{
    std::vector<int> v;
    v.push_back(m_base_addr);
    return v;
}


void
IoCardPrinter::reset(bool /*hard_reset*/) noexcept
{
    // reset card state
    m_selected   = false;
    m_cpb        = true;   // CPU busy
}


void
IoCardPrinter::select()
{
    if (NOISY) {
        UI_info("printer ABS");
    }

    m_selected = true;
    m_cpu->setDevRdy(true);
}


void
IoCardPrinter::deselect()
{
    if (NOISY) {
        UI_info("printer -ABS");
    }
    m_cpu->setDevRdy(false);

    // ...

    m_selected = false;
    m_cpb      = true;
}


void
IoCardPrinter::strobeOBS(int val)
{
    const uint8 val8 = val & 0xFF;

    if (NOISY) {
        UI_info("printer OBS: Output of byte 0x%02x", val8);
    }

    UI_printerChar(getGuiPtr(), val8);

    m_cpu->setDevRdy(true);
}


void
IoCardPrinter::strobeCBS(int val)
{
    strobeOBS(val);
}


// change of CPU Busy state
void
IoCardPrinter::setCpuBusy(bool busy)
{
    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (NOISY) {
        UI_info("printer CPB%c", busy ? '+' : '-');
    }

    m_cpb = busy;
    // FIXME: return printer status (requires handshaking logic, though)
    m_cpu->setDevRdy(true);
}


// ---- accessor of opaque gui pointer ----

PrinterFrame *
IoCardPrinter::getGuiPtr() const noexcept
{
    return m_wndhnd;
}

// vim: ts=8:et:sw=4:smarttab
