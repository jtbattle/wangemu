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
    DiskCtrlCfgState() = default;
    DiskCtrlCfgState(const DiskCtrlCfgState &obj) noexcept;             // copy
    DiskCtrlCfgState &operator=(const DiskCtrlCfgState &rhs) noexcept;  // assign

    // ------------ common CardCfgState interface ------------
    // See CardCfgState.h for the use of these functions.

    void setDefaults() noexcept override;
    void loadIni(const std::string &subgroup) override;
    void saveIni(const std::string &subgroup) const override;
    bool operator==(const CardCfgState &rhs) const noexcept override;
    bool operator!=(const CardCfgState &rhs) const noexcept override;
    std::shared_ptr<CardCfgState> clone() const override;
    bool configOk(bool warn) const noexcept override;
    bool needsReboot(const CardCfgState &other) const noexcept override;

    // ------------ unique to DiskCtrlCfgState ------------

    // set/get the number of disk drives associated with the controller
    void setNumDrives(int count) noexcept;
    int  getNumDrives() const noexcept;

    // set/get the disk controller intelligence mode
    enum disk_ctrl_intelligence_t {
        DISK_CTRL_DUMB,              // dumb disk controller
        DISK_CTRL_INTELLIGENT,       // intelligent disk controller
        DISK_CTRL_AUTO               // it depends on attached media
    };
    void setIntelligence(disk_ctrl_intelligence_t intelligence) noexcept;
    disk_ctrl_intelligence_t getIntelligence() const noexcept;

    // warn the user when small media is inserted into an intelligent controller,
    // or when large media is inserted into a dumb controller.
    void setWarnMismatch(bool warn) noexcept;
    bool getWarnMismatch() const noexcept;

private:
    bool m_initialized = false;    // for debugging and sanity checking
    int  m_num_drives = 0;         // number of associated disk drives
    disk_ctrl_intelligence_t
         m_intelligence = DISK_CTRL_INTELLIGENT; // dumb, smart, or automatically decide
    bool m_warn_mismatch = true;   // warn if media mismatches controller intelligence
};

#endif // _INCLUDE_DISK_CONTROLLER_CFG_H_

// vim: ts=8:et:sw=4:smarttab
