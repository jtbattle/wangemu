#ifndef _INCLUDE_IOCARD_PRINTER_H_
#define _INCLUDE_IOCARD_PRINTER_H_

#include "Ui.h"         // pick up def of UI_gui_handle_t
#include "IoCard.h"

class Cpu2200;

class IoCardPrinter : public IoCard
{
public:
    // see the base class for the definition of the public functions

    // ----- common IoCard functions -----
    IoCardPrinter(Cpu2200 &cpu, int baseaddr, int cardslot);
    ~IoCardPrinter();

    vector<int> getAddresses() const;

    void  reset(int hard_reset=1);
    void  select();
    void  deselect();
    void  OBS(int val);
    void  CBS(int val);
    void  CPB(bool busy);

    // give access to associated gui window
    UI_gui_handle_t getGuiPtr() const;

private:
    // ---- card properties ----
    const string getDescription() const;
    const string getName() const;
    vector<int> getBaseAddresses() const;

    Cpu2200    &m_cpu;            // associated CPU
    const int   m_baseaddr;       // the address the card is mapped to
    const int   m_slot;           // which slot the card is plugged into
    bool        m_selected;       // the card is currently selected
    bool        m_cpb;            // the cpu is busy
    void       *m_wndhnd;         // opaque handle to UI window

    CANT_ASSIGN_OR_COPY_CLASS(IoCardPrinter);
};

#endif // _INCLUDE_IOCARD_PRINTER_H_

// vim: ts=8:et:sw=4:smarttab
