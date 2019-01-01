
#include "DiskCtrlCfgState.h"
#include "host.h"
#include "Ui.h"                 // for UI_Alert

#include <sstream>

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// assignment
DiskCtrlCfgState&
DiskCtrlCfgState::operator=(const DiskCtrlCfgState &rhs) noexcept
{
    // don't copy something that hasn't been initialized
    assert(rhs.m_initialized);

    // check for self-assignment
    if (this != &rhs) {
        m_num_drives    = rhs.m_num_drives;
        m_intelligence  = rhs.m_intelligence;
        m_warn_mismatch = rhs.m_warn_mismatch;
        m_initialized   = true;
    }

    return *this;
}


// copy constructor
DiskCtrlCfgState::DiskCtrlCfgState(const DiskCtrlCfgState &obj) noexcept
{
    assert(obj.m_initialized);
    m_num_drives    = obj.m_num_drives;
    m_intelligence  = obj.m_intelligence;
    m_warn_mismatch = obj.m_warn_mismatch;
    m_initialized   = true;
}


// equality comparison
bool
DiskCtrlCfgState::operator==(const CardCfgState &rhs) const noexcept
{
    const DiskCtrlCfgState rrhs(dynamic_cast<const DiskCtrlCfgState&>(rhs));

    assert(     m_initialized);
    assert(rrhs.m_initialized);

    return (getNumDrives()    == rrhs.getNumDrives())    &&
           (getIntelligence() == rrhs.getIntelligence()) &&
           (getWarnMismatch() == rrhs.getWarnMismatch()) ;
}

bool
DiskCtrlCfgState::operator!=(const CardCfgState &rhs) const noexcept
{
    return !(*this == rhs);
}


// establish a reasonable default state on a newly minted card
void
DiskCtrlCfgState::setDefaults() noexcept
{
    setNumDrives(2);
    setIntelligence(DISK_CTRL_INTELLIGENT);
    setWarnMismatch(true);
}


// read from configuration file
void
DiskCtrlCfgState::loadIni(const std::string &subgroup)
{
    int ival;
    host::configReadInt(subgroup, "numDrives", &ival, 2);
    if (ival < 1 || ival > 4) {
        UI_warn("config state messed up -- assuming something reasonable");
        ival = 2;
    }
    setNumDrives(ival);

    setIntelligence(DISK_CTRL_INTELLIGENT);  // default
    std::string sval;
    bool b = host::configReadStr(subgroup, "intelligence", &sval);
    if (b) {
             if (sval == "dumb")  { setIntelligence(DISK_CTRL_DUMB); }
        else if (sval == "smart") { setIntelligence(DISK_CTRL_INTELLIGENT); }
        else if (sval == "auto")  { setIntelligence(DISK_CTRL_AUTO); }
    }

    bool bval;
    host::configReadBool(subgroup, "warnMismatch", &bval, true);
    setWarnMismatch(bval);

    m_initialized = true;
}


// save to configuration file
void
DiskCtrlCfgState::saveIni(const std::string &subgroup) const
{
    assert(m_initialized);

    host::configWriteInt(subgroup, "numDrives", getNumDrives());

    std::string foo;
    switch (m_intelligence) {
        case DISK_CTRL_DUMB:        foo = "dumb";  break;
        case DISK_CTRL_INTELLIGENT: foo = "smart"; break;
        case DISK_CTRL_AUTO:        foo = "auto";  break;
        default: assert(false);     foo = "smart";  break;
    }
    host::configWriteStr(subgroup, "intelligence", foo);

    host::configWriteBool(subgroup, "warnMismatch", getWarnMismatch());
}


void
DiskCtrlCfgState::setNumDrives(int count) noexcept
{
    assert(count >= 1 && count <= 4);
    m_num_drives = count;
    m_initialized = true;
}

void
DiskCtrlCfgState::setIntelligence(disk_ctrl_intelligence_t intelligence) noexcept
{
    assert(intelligence == DISK_CTRL_DUMB        ||
           intelligence == DISK_CTRL_INTELLIGENT ||
           intelligence == DISK_CTRL_AUTO        );

    m_intelligence = intelligence;
}

void
DiskCtrlCfgState::setWarnMismatch(bool warn) noexcept
{
    m_warn_mismatch = warn;
    m_initialized = true;
}

int
DiskCtrlCfgState::getNumDrives() const noexcept
{
    return m_num_drives;
}

DiskCtrlCfgState::disk_ctrl_intelligence_t
DiskCtrlCfgState::getIntelligence() const noexcept
{
    return m_intelligence;
}

bool
DiskCtrlCfgState::getWarnMismatch() const noexcept
{
    return m_warn_mismatch;
}


// return a copy of self
std::shared_ptr<CardCfgState>
DiskCtrlCfgState::clone() const
{
    return std::make_shared<DiskCtrlCfgState>(*this);
}


// returns true if the current configuration is reasonable, and false if not.
// if returning false, this routine first calls UI_Alert() describing what
// is wrong.
bool
DiskCtrlCfgState::configOk(bool /*warn*/) const noexcept
{
    assert(m_initialized);
    return true;  // pretty hard to screw it up
}


// returns true if the state has changed in a way that requires a reboot
bool
DiskCtrlCfgState::needsReboot(const CardCfgState &other) const noexcept
{
    const DiskCtrlCfgState oother(dynamic_cast<const DiskCtrlCfgState&>(other));

    return (getNumDrives() != oother.getNumDrives());
}

// vim: ts=8:et:sw=4:smarttab
