// This class is derived from CardCfgState and holds some configuration state
// particular to this type of card.  In short, the number of drives associated
// with the controller (1-4), whether the controller is dumb or intelligent,
// and whether the controller's intelligence is not suitable for use with
// a given virtual disk image (eg, dumb controllers can access only the first
// platter of disks that have more than one).

#ifndef _INCLUDE_DISK_CONTROLLER_CFG_H_
#define _INCLUDE_DISK_CONTROLLER_CFG_H_

#include "CardCfgState.h"

class DiskCtrlCfgState : public CardCfgState
{
public:
    DiskCtrlCfgState();
    DiskCtrlCfgState(const DiskCtrlCfgState &obj);              // copy
    DiskCtrlCfgState &operator=(const DiskCtrlCfgState &rhs);   // assign

    // ------------ common CardCfgState interface ------------
    // See CardCfgState.h for the use of these functions.

    void setDefaults();
    void loadIni(const string &subgroup);
    void saveIni(const string &subgroup) const;
    bool operator==(const CardCfgState &rhs) const;
    bool operator!=(const CardCfgState &rhs) const;
    DiskCtrlCfgState* clone() const;
    bool configOk(bool warn) const;
    bool needsReboot(const CardCfgState &other) const;

    // ------------ unique to DiskCtrlCfgState ------------

    // set/get the number of disk drives associated with the controller
    void setNumDrives(int count);
    int  getNumDrives() const;

    // set/get the disk controller intelligence mode
    enum disk_ctrl_intelligence_t {
        DISK_CTRL_DUMB,              // dumb disk controller
        DISK_CTRL_INTELLIGENT,       // intelligent disk controller
        DISK_CTRL_AUTO               // it depends on attached media
    };
    void setIntelligence(disk_ctrl_intelligence_t intelligence);
    disk_ctrl_intelligence_t getIntelligence() const;

    // warn the user when small media is inserted into an intelligent controller,
    // or when large media is inserted into a dumb controller.
    void setWarnMismatch(bool warn);
    bool getWarnMismatch() const;

private:
    // just for debugging -- make sure we don't attempt to use such a config
    bool m_initialized;

    int  m_num_drives;       // number of associated disk drives
    disk_ctrl_intelligence_t
         m_intelligence;     // dumb, smart, or automatically decide
    bool m_warn_mismatch;    // warn if media mismatches controller intelligence
};

#endif // _INCLUDE_DISK_CONTROLLER_CFG_H_
