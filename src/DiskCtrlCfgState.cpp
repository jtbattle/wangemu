
#include "DiskCtrlCfgState.h"
#include "Host.h"
//#include "IoCard.h"
//#include "System2200.h"
#include "Ui.h"                 // for UI_Alert

#include <sstream>

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// default constructor
DiskCtrlCfgState::DiskCtrlCfgState() :
        m_initialized(false),
        m_num_drives(0),
        m_intelligence(DISK_CTRL_INTELLIGENT),
        m_warn_mismatch(true)
{
}


// assignment
DiskCtrlCfgState&
DiskCtrlCfgState::operator=(const DiskCtrlCfgState &rhs)
{
    // don't copy something that hasn't been initialized
    assert(rhs.m_initialized);

    // check for self-assignment
    if (this != &rhs) {
        setNumDrives( rhs.getNumDrives() );
        setIntelligence( rhs.getIntelligence() );
        setWarnMismatch( rhs.getWarnMismatch() );
    }

    return *this;
}


// copy constructor
DiskCtrlCfgState::DiskCtrlCfgState(const DiskCtrlCfgState &obj)
    : CardCfgState()  // good hygiene, although base class is empty
{
    assert(obj.m_initialized);

    setNumDrives( obj.getNumDrives() );
    setIntelligence( obj.getIntelligence() );
    setWarnMismatch( obj.getWarnMismatch() );
}


// equality comparison
bool
DiskCtrlCfgState::operator==(const CardCfgState &rhs) const
{
    const DiskCtrlCfgState rrhs( dynamic_cast<const DiskCtrlCfgState&>(rhs) );

    assert(     m_initialized);
    assert(rrhs.m_initialized);

    return (getNumDrives()    == rrhs.getNumDrives())    &&
           (getIntelligence() == rrhs.getIntelligence()) &&
           (getWarnMismatch() == rrhs.getWarnMismatch()) ;
}

bool
DiskCtrlCfgState::operator!=(const CardCfgState &rhs) const
{
    return !(*this == rhs);
}


// establish a reasonable default state
void
DiskCtrlCfgState::setDefaults()
{
    setNumDrives( 2 );
    setIntelligence( DISK_CTRL_INTELLIGENT );
    setWarnMismatch( true );
}


// read from configuration file
void
DiskCtrlCfgState::loadIni(const std::string &subgroup)
{
    Host hst;
    int ival;
    (void)hst.ConfigReadInt(subgroup, "numDrives", &ival, 2);
    if (ival < 1 || ival > 4) {
        UI_Warn("config state messed up -- assuming something reasonable");
        ival = 2;
    }
    setNumDrives( ival );

    setIntelligence( DISK_CTRL_INTELLIGENT );  // default
    std::string sval;
    bool b = hst.ConfigReadStr(subgroup, "intelligence", &sval);
    if (b) {
             if (sval == "dumb")  { setIntelligence( DISK_CTRL_DUMB ); }
        else if (sval == "smart") { setIntelligence( DISK_CTRL_INTELLIGENT ); }
        else if (sval == "auto")  { setIntelligence( DISK_CTRL_AUTO ); }
    }

    bool bval;
    hst.ConfigReadBool(subgroup, "warnMismatch", &bval, true);
    setWarnMismatch( bval );

    m_initialized = true;
}


// save to configuration file
void
DiskCtrlCfgState::saveIni(const std::string &subgroup) const
{
    assert(m_initialized);

    Host hst;
    hst.ConfigWriteInt(subgroup, "numDrives", getNumDrives());

    std::string foo;
    switch (m_intelligence) {
        case DISK_CTRL_DUMB:        foo = "dumb";  break;
        case DISK_CTRL_INTELLIGENT: foo = "smart"; break;
        case DISK_CTRL_AUTO:        foo = "auto";  break;
        default: assert(false);     foo = "smart";  break;
    }
    hst.ConfigWriteStr(subgroup, "intelligence", foo);

    hst.ConfigWriteBool(subgroup, "warnMismatch", getWarnMismatch());
}


void
DiskCtrlCfgState::setNumDrives(int count)
{
    assert(count >= 1 && count <= 4);
    m_num_drives = count;
    m_initialized = true;
}

void
DiskCtrlCfgState::setIntelligence(disk_ctrl_intelligence_t intelligence)
{
    assert( intelligence == DISK_CTRL_DUMB        ||
            intelligence == DISK_CTRL_INTELLIGENT ||
            intelligence == DISK_CTRL_AUTO        );

    m_intelligence = intelligence;
}

void
DiskCtrlCfgState::setWarnMismatch(bool warn)
{
    m_warn_mismatch = warn;
    m_initialized = true;
}

int
DiskCtrlCfgState::getNumDrives() const
        { return m_num_drives; }

DiskCtrlCfgState::disk_ctrl_intelligence_t
DiskCtrlCfgState::getIntelligence() const
        { return m_intelligence; }

bool
DiskCtrlCfgState::getWarnMismatch() const
        { return m_warn_mismatch; }


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
DiskCtrlCfgState::configOk(bool warn) const
{
    assert(m_initialized);
    if (!m_initialized) {
        return false;
    }

    warn = warn;  // keep lint happy
    return true;  // pretty hard to screw it up
}


// returns true if the state has changed in a way that requires a reboot
bool
DiskCtrlCfgState::needsReboot(const CardCfgState &other) const
{
    const DiskCtrlCfgState oother( dynamic_cast<const DiskCtrlCfgState&>(other) );

    return (getNumDrives() != oother.getNumDrives());
}

// vim: ts=8:et:sw=4:smarttab
