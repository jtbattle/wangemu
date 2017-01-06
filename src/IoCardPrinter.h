#ifndef _INCLUDE_IOCARD_PRINTER_H_
#define _INCLUDE_IOCARD_PRINTER_H_

#include "Ui.h"
#include "IoCard.h"

class Cpu2200;

class IoCardPrinter : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardPrinter);

    // ----- common IoCard functions -----
    IoCardPrinter(std::shared_ptr<Cpu2200> cpu,
                  int baseaddr, int cardslot);
    ~IoCardPrinter();

    vector<int> getAddresses() const override;

    void  reset(bool hard_reset=true) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    void  CPB(bool busy) override;

    // give access to associated gui window
    PrinterFrame *getGuiPtr() const;

private:
    // ---- card properties ----
    const string getDescription() const override;
    const string getName() const override;
    vector<int> getBaseAddresses() const override;

    std::shared_ptr<Cpu2200> m_cpu;  // associated CPU
    const int     m_baseaddr;     // the address the card is mapped to
    const int     m_slot;         // which slot the card is plugged into
    bool          m_selected;     // the card is currently selected
    bool          m_cpb;          // the cpu is busy
    PrinterFrame *m_wndhnd;       // opaque handle to UI window
};

#endif // _INCLUDE_IOCARD_PRINTER_H_

// vim: ts=8:et:sw=4:smarttab
