// This is a skeleton file for patterning future I/O devices

#ifndef _INCLUDE_IOCARD_XXX_H_
#define _INCLUDE_IOCARD_XXX_H_

#include "IoCard.h"

class Cpu2200;

class IoCardXxx : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardXxx);

    // ----- common IoCard functions -----
    IoCardXxx(Cpu2200 &cpu, int baseaddr, int cardslot);
    ~IoCardXxx();

    std::vector<int> getAddresses() const override;

    void  reset(bool hard_reset=true) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    int   getIB() const override;
    void  CPB(bool busy) override;

    // ----- IoCardXxx specific functions -----
    // ...

private:
    // ---- card properties ----
    const std::string getDescription() const override;
    const std::string getName() const override;
    std::vector<int> getBaseAddresses() const override;

    // ...
    Cpu2200    &m_cpu;            // associated CPU
    const int   m_baseaddr;       // the address the card is mapped to
    const int   m_slot;           // which slot the card is plugged into
    bool        m_selected;       // the card is currently selected
    bool        m_cpb;            // the cpu is busy
    bool        m_card_busy;      // the card is busy doing something
};

#endif // _INCLUDE_IOCARD_XXX_H_

// vim: ts=8:et:sw=4:smarttab
