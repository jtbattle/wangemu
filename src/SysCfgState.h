// This class manages the system configuration state:
//    + set the state to some reasonable default
//    + read the state from the ini file
//    + save the state to the ini file
//    + copy state
//    + compare two sets of state for (in)equality
//    + report if the state is valid
//    + report if the transition between two sets of state requires a
//      emulated system reboot, or just a soft state change
//
// The class contains state for one-off choices, such as the choice of cpu,
// ram size, realtime control, and warning on invalid I/O accesses.  It also
// contains an array of information about what kind of card, if any, is
// plugged into each slot in the system.
//
// Part of this state is a pointer to a CardCfgState object, which is
// subclassed to be be unique for each kind of card that has its own
// configuration state, or null for cards that don't need configuration.
// When the SysCfgState is copied or compared, part of the logic must be
// handled by that subordinate CardCfgState class.

#ifndef _INCLUDE_SYS_CONFIG_STATE_H_
#define _INCLUDE_SYS_CONFIG_STATE_H_

#include "w2200.h"
#include "IoCard.h"

class CardCfgState;

class SysCfgState
{
public:
    SysCfgState();
    ~SysCfgState();

    SysCfgState(const SysCfgState &obj);              // copy
    SysCfgState &operator=(const SysCfgState &rhs);   // assign

    // compare to configurations for equality
    bool operator==(const SysCfgState &rhs) const;
    bool operator!=(const SysCfgState &rhs) const;

    // returns true if the state has changed in a way that requires a reboot.
    bool needsReboot(const SysCfgState &other) const;

    // initialized with a reasonable default state
    void setDefaults();

    // load/save a configuration from/to the .ini file
    void loadIni();
    void saveIni() const;

    // set/get the type of CPU.  CPUTYPE_2200{B,T,VP}
    void setCpuType(int type);
    int  getCpuType() const;

    // throttle the CPU to match the real hardware, or not
    void regulateCpuSpeed(bool regulated);
    bool isCpuSpeedRegulated() const;

    // set/get amount of RAM in the system configuration
    void setRamKB(int kb);
    int  getRamKB() const;

    // set/get control over whether disk operations are performed in realtime
    void setDiskRealtime(bool realtime);
    bool getDiskRealtime() const;

    // warn the user when an attempt is made to access a device at a bad addr
    void setWarnIo(bool warn);
    bool getWarnIo() const;

    // retrieve the pointer to the per-card configuration state
    const CardCfgState* getCardConfig(int slot) const;

    // twiddle the state of the card in the given slot
    void editCardConfig(int slot);

    // set/get the type of the card in the specified backplane slot
    void setSlotCardType(int slot, IoCard::card_type_e type);
    IoCard::card_type_e getSlotCardType(int slot) const;
    bool        isSlotOccupied(int slot) const;

    // set/get the address of the card in the specified backplane slot
    void setSlotCardAddr(int slot, int addr);
    int  getSlotCardAddr(int slot) const;

    // returns true if the current configuration is valid and consistent.
    // if warn is true, errors produce a UI_Alert() explanation
    bool configOk(bool warn) const;

private:
    // just for debugging -- make sure we don't attempt to use such a config
    bool m_initialized;

    // -------------- information about slots --------------
    // a list of which cards are plugged into each slot of the IO backplane
    struct {
        IoCard::card_type_e
                      type;   // what is plugged into the slot
        int           addr;   // where it will be located (only 8 lsb matter on match)
        CardCfgState *cardCfg; // per-card configuration state, or nullptr
    } m_slot[NUM_IOSLOTS];

    // -------------- misc information --------------
    int  m_cputype;          // which CPU type
    int  m_ramsize;          // amount of memory in CPU
    bool m_speed_regulated;  // emulation speed throttling
    bool m_disk_realtime;    // boolean whether disk emulation is realtime or not
    bool m_warn_io;          // boolean whether to warn on access to invalid IO device
};

#endif // _INCLUDE_SYS_CONFIG_STATE_H_

// vim: ts=8:et:sw=4:smarttab
