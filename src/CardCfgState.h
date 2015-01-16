// This is an abstract base class that contains per-card configuration state
// that the SysCfgState class doesn't hold itself.  Cards which need some
// extra configuration, like the disk controller, are derived from ths class.
// SysCfgState holds a CardCfgState* for each I/O card slot in the computer.
//
// This class allows the caller to:
//    + set the state to some reasonable default
//    + read the state from the ini file
//    + save the state to the ini file
//    + copy state
//    + compare the state of two configurations of the same type
//    + report if the state is valid
//    + report if the transition between two sets of state requires a
//      emulated system reboot, or just a soft state change

#ifndef _INCLUDE_CARD_CFG_STATE_H_
#define _INCLUDE_CARD_CFG_STATE_H_

#include "w2200.h"

class CardCfgState
{
public:
    virtual ~CardCfgState() {};

    // initialized with a reasonable default state
    virtual void setDefaults() = 0;

    // load/save a configuration from/to the .ini file
    virtual void loadIni(const string &subgroup) = 0;
    virtual void saveIni(const string &subgroup) const = 0;

    // compare to configurations for equality
    virtual bool operator==(const CardCfgState &rhs) const = 0;
    virtual bool operator!=(const CardCfgState &rhs) const = 0;

    // return a copy of self
    virtual CardCfgState* clone() const = 0;

    // returns true if the current configuration is valid and consistent.
    // if warn is true, errors produce a UI_Alert() explanation
    virtual bool configOk(bool warn) const = 0;

    // returns true if the state has changed in a way that requires a reboot
    virtual bool needsReboot(const CardCfgState &other) const = 0;
};

#endif // _INCLUDE_CARD_CFG_STATE_H_
