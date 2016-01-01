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

    vector<int> getAddresses() const;

    void  reset(int hard_reset=1);
    void  select();
    void  deselect();
    void  OBS(int val);
    void  CBS(int val);
    int   getIB5() const;
    void  CPB(bool busy);

    // ----- IoCardXxx specific functions -----
    // ...

private:
    // ---- card properties ----
    const string getDescription() const;
    const string getName() const;
    vector<int> getBaseAddresses() const;

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
