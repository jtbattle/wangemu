#include "TermMuxCfgState.h"
#include "Ui.h"                 // for UI_Alert
#include "host.h"

#include <sstream>

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// assignment
TermMuxCfgState&
TermMuxCfgState::operator=(const TermMuxCfgState &rhs) noexcept
{
    // don't copy something that hasn't been initialized
    assert(rhs.m_initialized);

    // check for self-assignment
    if (this != &rhs) {
        m_num_terms   = rhs.m_num_terms;
        m_initialized = true;
    }

    return *this;
}


// copy constructor
TermMuxCfgState::TermMuxCfgState(const TermMuxCfgState &obj) noexcept
{
    assert(obj.m_initialized);
    m_num_terms   = obj.m_num_terms;
    m_initialized = true;
}


// equality comparison
bool
TermMuxCfgState::operator==(const CardCfgState &rhs) const noexcept
{
    const TermMuxCfgState rrhs(dynamic_cast<const TermMuxCfgState&>(rhs));

    assert(     m_initialized);
    assert(rrhs.m_initialized);

    return (getNumTerminals() == rrhs.getNumTerminals());
}


bool
TermMuxCfgState::operator!=(const CardCfgState &rhs) const noexcept
{
    return !(*this == rhs);
}


// establish a reasonable default state on a newly minted card
void
TermMuxCfgState::setDefaults() noexcept
{
    setNumTerminals(1);
}


// read from configuration file
void
TermMuxCfgState::loadIni(const std::string &subgroup)
{
    int ival;
    host::configReadInt(subgroup, "numTerminals", &ival, 1);
    if (ival < 1 || ival > 4) {
        UI_warn("config state messed up -- assuming something reasonable");
        ival = 1;
    }
    setNumTerminals(ival);
    m_initialized = true;
}


// save to configuration file
void
TermMuxCfgState::saveIni(const std::string &subgroup) const
{
    assert(m_initialized);
    host::configWriteInt(subgroup, "numTerminals", getNumTerminals());
}


void
TermMuxCfgState::setNumTerminals(int count) noexcept
{
    assert(count >= 1 && count <= 4);
    m_num_terms   = count;
    m_initialized = true;
}


int
TermMuxCfgState::getNumTerminals() const noexcept
{
    return m_num_terms;
}


// return a copy of self
std::shared_ptr<CardCfgState>
TermMuxCfgState::clone() const
{
    return std::make_shared<TermMuxCfgState>(*this);
}


// returns true if the current configuration is reasonable, and false if not.
// if returning false, this routine first calls UI_Alert() describing what
// is wrong.
bool
TermMuxCfgState::configOk(bool /*warn*/) const noexcept
{
    return true;  // pretty hard to screw it up
}


// returns true if the state has changed in a way that requires a reboot
bool
TermMuxCfgState::needsReboot(const CardCfgState &other) const noexcept
{
    const TermMuxCfgState oother(dynamic_cast<const TermMuxCfgState&>(other));
    return (getNumTerminals() != oother.getNumTerminals());
}

// vim: ts=8:et:sw=4:smarttab
