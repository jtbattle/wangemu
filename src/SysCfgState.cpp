// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "CardCfgState.h"
#include "CardInfo.h"
#include "Cpu2200.h"
#include "IoCard.h"
#include "SysCfgState.h"
#include "Ui.h"                 // for UI_Alert
#include "host.h"
#include "system2200.h"

#include <sstream>

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// default constructor
SysCfgState::SysCfgState()
{
    for (auto &slot : m_slot) {
        slot.type     = IoCard::card_t::none;
        slot.addr     = -1;
        slot.card_cfg = nullptr;
    }
}


SysCfgState::~SysCfgState()
{
    // drop all attached cards
    for (auto &slot : m_slot) {
        slot.card_cfg = nullptr;
    }
}


// assignment
SysCfgState&
SysCfgState::operator=(const SysCfgState &rhs)
{
    // don't copy something that hasn't been initialized
    assert(rhs.m_initialized);

    // check for self-assignment
    if (this == &rhs) {
        return *this;
    }

    for (int slot=0; slot < NUM_IOSLOTS; slot++) {
        m_slot[slot].type = rhs.m_slot[slot].type;
        m_slot[slot].addr = rhs.m_slot[slot].addr;
        // here we must do a deep copy:
        m_slot[slot].card_cfg = nullptr;
        if (rhs.m_slot[slot].card_cfg != nullptr) {
            m_slot[slot].card_cfg = rhs.m_slot[slot].card_cfg->clone();
        }
    }

    setCpuType(rhs.getCpuType());
    setRamKB(rhs.getRamKB());
    regulateCpuSpeed(rhs.isCpuSpeedRegulated());
    setDiskRealtime(rhs.getDiskRealtime());
    setWarnIo(rhs.getWarnIo());

    return *this;
}


// copy constructor
SysCfgState::SysCfgState(const SysCfgState &obj)
{
    assert(obj.m_initialized);

    for (int slot=0; slot < NUM_IOSLOTS; slot++) {
        m_slot[slot].type = obj.m_slot[slot].type;
        m_slot[slot].addr = obj.m_slot[slot].addr;
        // here we must do a deep copy:
        if (obj.m_slot[slot].card_cfg != nullptr) {
            m_slot[slot].card_cfg = obj.m_slot[slot].card_cfg->clone();
        } else {
            m_slot[slot].card_cfg = nullptr;
        }
    }

    m_cpu_type        = obj.m_cpu_type;
    m_ramsize         = obj.m_ramsize;
    m_speed_regulated = obj.m_speed_regulated;
    m_disk_realtime   = obj.m_disk_realtime;
    m_warn_io         = obj.m_warn_io;
    m_initialized     = true;
}


// equality comparison
bool
SysCfgState::operator==(const SysCfgState &rhs) const
{
    assert(    m_initialized);
    assert(rhs.m_initialized);

    for (int slot=0; slot < NUM_IOSLOTS; slot++) {
        if (m_slot[slot].type != rhs.m_slot[slot].type) {
            return false;
        }
        if (m_slot[slot].type == IoCard::card_t::none) {
            continue;  // on to next slot
        }
        if (   (m_slot[slot].addr != rhs.m_slot[slot].addr)
            || ((    m_slot[slot].card_cfg == nullptr) !=
                (rhs.m_slot[slot].card_cfg == nullptr))) {
            return false;
        }

        if ((    m_slot[slot].card_cfg != nullptr) &&
            (rhs.m_slot[slot].card_cfg != nullptr) &&
            (*m_slot[slot].card_cfg != *rhs.m_slot[slot].card_cfg)) {
            return false;
        }
    }

    return (m_cpu_type        == rhs.m_cpu_type)        &&
           (m_ramsize         == rhs.m_ramsize)         &&
           (m_speed_regulated == rhs.m_speed_regulated) &&
           (m_disk_realtime   == rhs.m_disk_realtime)   &&
           (m_warn_io         == rhs.m_warn_io)         ;
}


bool
SysCfgState::operator!=(const SysCfgState &rhs) const
{
    return !(*this == rhs);
}


// establish a reasonable default state
void
SysCfgState::setDefaults()
{
    setCpuType(Cpu2200::CPUTYPE_2200T);
    setRamKB(32);
    setDiskRealtime(true);
    setWarnIo(true);

    // wipe out all cards
    for (int slot=0; slot < NUM_IOSLOTS; slot++) {
        setSlotCardType(slot, IoCard::card_t::none);
        setSlotCardAddr(slot, 0x000);
    }

    setSlotCardType(0, IoCard::card_t::keyboard);
    setSlotCardAddr(0, 0x001);

    setSlotCardType(1, IoCard::card_t::disp_64x16);
    setSlotCardAddr(1, 0x005);

    setSlotCardType(2, IoCard::card_t::disk);
    setSlotCardAddr(2, 0x310);

    setSlotCardType(3, IoCard::card_t::printer);
    setSlotCardAddr(3, 0x215);

    m_initialized = true;
}


// read from configuration file
void
SysCfgState::loadIni()
{
    // get CPU attributes and start accumulating new configuration changes
    {
        std::string subgroup("cpu");
        std::string sval;

        std::string defaultCpu = "2200T";
        host::configReadStr(subgroup, "cpu", &sval, &defaultCpu);
        auto cpuCfg = system2200::getCpuConfig(sval);
        if (cpuCfg == nullptr) {
            UI_warn("The ini didn't specify a legal cpu type.\n"
                    "Delete your .ini and start over.");
            cpuCfg = system2200::getCpuConfig("2200T");
        }
        setCpuType(cpuCfg->cpu_type);

        const int ram_choices = cpuCfg->ram_size_options.size();
        const int min_ram     = cpuCfg->ram_size_options[0];
        const int max_ram     = cpuCfg->ram_size_options[ram_choices-1];
        const int dflt_ram    = max_ram;
        int ival;
        host::configReadInt(subgroup, "memsize", &ival, dflt_ram);
        if (ival < min_ram) { ival = min_ram; }
        if (ival > max_ram) { ival = max_ram; }
        for (const int kb : cpuCfg->ram_size_options) {
            if (ival <= kb) {
                // round up to the next biggest ram in the list of valid sizes
                setRamKB(kb);
                break;
            }
        }

        // learn whether CPU speed is regulated or not
        regulateCpuSpeed(true);  // default
        const bool b = host::configReadStr(subgroup, "speed", &sval);
        if (b && (sval == "unregulated")) {
            regulateCpuSpeed(false);
        }
    }

    // get IO slot attributes
    for (int slot=0; slot < NUM_IOSLOTS; slot++) {

        std::ostringstream osf;
        osf << "io/slot-" << slot;
        std::string subgroup(osf.str());
        std::string sval;
        IoCard::card_t card_type = IoCard::card_t::none;

        int io_addr;
        host::configReadInt(subgroup, "addr", &io_addr, -1);  // -1 if not found
        const bool b = host::configReadStr(subgroup, "type", &sval);
        if (b) {
            card_type = CardInfo::getCardTypeFromName(sval);
        }

        // TODO: ideally, we'd check the card type against the list of
        //       addresses allowed for that card type
        const bool plausible_card = IoCard::legalCardType(card_type)
                                 && ((0 <= io_addr) && (io_addr <= 0xFFF));

        if (plausible_card) {
            setSlotCardType(slot, card_type);
            setSlotCardAddr(slot, io_addr);
            // if card_type isCardConfigurable(), make a new config object,
            // and try to load it from the ini
            if (CardInfo::isCardConfigurable(card_type)) {
                std::string cardsubgroup("io/slot-" + std::to_string(slot) + "/cardcfg");
                m_slot[slot].card_cfg = nullptr;  // dump any existing card_cfg
                m_slot[slot].card_cfg = CardInfo::getCardCfgState(card_type);
                m_slot[slot].card_cfg->loadIni(cardsubgroup);
            }
        } else {
            // the slot is empty
            setSlotCardType(slot, IoCard::card_t::none);
            setSlotCardAddr(slot, 0x000);
        }
    }

    // get misc other config bits
    {
        std::string subgroup("misc");
        bool bval;

        host::configReadBool(subgroup, "disk_realtime", &bval, true);
        setDiskRealtime(bval);  // default

        host::configReadBool(subgroup, "warnio", &bval, true);
        setWarnIo(bval);  // default
    }

    m_initialized = true;
}


// save to configuration file
void
SysCfgState::saveIni() const
{
    assert(m_initialized);

    // save IO information
    for (int slot=0; slot < NUM_IOSLOTS; slot++) {
        std::string subgroup("io/slot-" + std::to_string(slot));

        const IoCard::card_t card_type = m_slot[slot].type;
        if (card_type != IoCard::card_t::none) {
            char val[10];
            sprintf(&val[0], "0x%03X", m_slot[slot].addr);
            std::string cardname = CardInfo::getCardName(card_type);
            host::configWriteStr(subgroup, "type", cardname);
            host::configWriteStr(subgroup, "addr", &val[0]);
        } else {
            host::configWriteStr(subgroup, "type", "");
            host::configWriteStr(subgroup, "addr", "");
        }

        if (m_slot[slot].card_cfg != nullptr) {
            std::string cardsubgroup("io/slot-" + std::to_string(slot) + "/cardcfg");
            m_slot[slot].card_cfg->saveIni(cardsubgroup);
        }

    } // for (slot=NUM_IOSLOTS)

    // save CPU information
    {
        const std::string subgroup("cpu");

        auto cpuCfg = system2200::getCpuConfig(m_cpu_type);
        assert(cpuCfg != nullptr);
        std::string cpu_label = cpuCfg->label;
        host::configWriteStr(subgroup, "cpu", cpu_label);

        host::configWriteInt(subgroup, "memsize", getRamKB());

        const char *foo = (system2200::isCpuSpeedRegulated()) ? "regulated" : "unregulated";
        host::configWriteStr(subgroup, "speed", foo);
    }

    // save misc other config bits
    {
        const std::string subgroup("misc");
        host::configWriteBool(subgroup, "disk_realtime", getDiskRealtime());
        host::configWriteBool(subgroup, "warnio",        getWarnIo());
    }
}


void
SysCfgState::setCpuType(int type) noexcept
{
    m_cpu_type = type;
    m_initialized = true;
}


void
SysCfgState::regulateCpuSpeed(bool regulated) noexcept
{
    m_speed_regulated = regulated;
    m_initialized = true;
}


void
SysCfgState::setRamKB(int kb) noexcept
{
    m_ramsize = kb;
    m_initialized = true;
}


void
SysCfgState::setDiskRealtime(bool realtime) noexcept
{
    m_disk_realtime = realtime;
    m_initialized = true;
}


void
SysCfgState::setWarnIo(bool warn) noexcept
{
    m_warn_io = warn;
    m_initialized = true;
}


// set the card type.  if the card type is configurable, set up a card_cfg
// object of the appropriate type, discarding whatever was there before.
void
SysCfgState::setSlotCardType(int slot, IoCard::card_t type)
{
    m_slot[slot].type = type;

    // create a config state object if the card type needs one
    if ((type != IoCard::card_t::none) && (CardInfo::isCardConfigurable(type))) {
        m_slot[slot].card_cfg = nullptr;  // kill any existing card
        m_slot[slot].card_cfg = CardInfo::getCardCfgState(type);
        m_slot[slot].card_cfg->setDefaults();
    }
    m_initialized = true;
}


void
SysCfgState::setSlotCardAddr(int slot, int addr) noexcept
{
    m_slot[slot].addr = addr;
    m_initialized = true;
}


int
SysCfgState::getCpuType() const noexcept
{
    return m_cpu_type;
}


bool
SysCfgState::isCpuSpeedRegulated() const noexcept
{
    return m_speed_regulated;
}


int
SysCfgState::getRamKB() const noexcept
{
    return m_ramsize;
}


bool
SysCfgState::getDiskRealtime() const noexcept
{
    return m_disk_realtime;
}


bool
SysCfgState::getWarnIo() const noexcept
{
    return m_warn_io;
}


IoCard::card_t
SysCfgState::getSlotCardType(int slot) const noexcept
{
    return m_slot[slot].type;
}


bool
SysCfgState::isSlotOccupied(int slot) const noexcept
{
    return m_slot[slot].type != IoCard::card_t::none;
}


int
SysCfgState::getSlotCardAddr(int slot) const noexcept
{
    return m_slot[slot].addr;
}


// retrieve the pointer to the per-card configuration state
const std::shared_ptr<CardCfgState>
SysCfgState::getCardConfig(int slot) const noexcept
{
    assert(isSlotOccupied(slot));
    return m_slot[slot].card_cfg;
}


// twiddle the state of the card in the given slot
void
SysCfgState::editCardConfig(int slot)
{
    assert(isSlotOccupied(slot));
    auto card_cfg = m_slot[slot].card_cfg;
    if (card_cfg != nullptr) {
        UI_configureCard(m_slot[slot].type, card_cfg.get());
    }
}


// returns true if the current configuration is reasonable, and false if not.
// if returning false, this routine first calls UI_Alert() describing what
// is wrong.
bool
SysCfgState::configOk(bool warn) const
{
    if (!m_initialized) {
        return false;
    }

    bool pri_crt_found = false;
    bool pri_kb_found  = false;

    for (int slot=0; slot < NUM_IOSLOTS; slot++) {

        if (!isSlotOccupied(slot)) {
            continue;
        }

        // make sure we have a keyboard at 0x01
        if ((m_slot[slot].type == IoCard::card_t::keyboard) &&
            (m_slot[slot].addr & 0xFF) == 0x01) {
            pri_kb_found = true;
        }

        if ((m_slot[slot].type == IoCard::card_t::term_mux) &&
            (m_slot[slot].addr & 0xFF) == 0x00) {
            pri_kb_found = true;
        }

        // make sure we have a crt at 0x05
        if (   (   (m_slot[slot].type == IoCard::card_t::disp_64x16)
                || (m_slot[slot].type == IoCard::card_t::disp_80x24))
            && (m_slot[slot].addr & 0xFF) == 0x05) {
            pri_crt_found = true;
        }

        if (   (m_slot[slot].type == IoCard::card_t::term_mux)
            && (m_slot[slot].addr & 0xFF) == 0x00) {
            pri_crt_found = true;
        }

        auto slotInst = IoCard::makeTmpCard(getSlotCardType(slot),
                                            getSlotCardAddr(slot) & 0xFF);
        std::vector<int> slotAddresses = slotInst->getAddresses();
        slotInst = nullptr;

        // check for address conflicts
        for (int slot2=slot+1; slot2 < NUM_IOSLOTS; slot2++) {

            if (!isSlotOccupied(slot2)) {
                continue;
            }

            auto slot2Inst = IoCard::makeTmpCard(getSlotCardType(slot2),
                                                 getSlotCardAddr(slot2) & 0xFF);
            std::vector<int> slot2Addresses = slot2Inst->getAddresses();
            slot2Inst = nullptr;

            // sweep through all occupied addresses and look for collisions
            for (const int slot_addr : slotAddresses) {
                for (const int slot2_addr : slot2Addresses) {
                    if ((slot_addr & 0xFF) == (slot2_addr & 0xFF)) {
                        if (warn) {
                            UI_error("Configuration problem: "
                                     "card in slots %d and %d both responding to address 0x%02X",
                                     slot, slot2, slot_addr & 0xFF);
                        }
                        return false;
                    }
                } // slot2_addr
            } // slot_addr
        }
    }

    if (!pri_kb_found) {
        if (warn) {
            UI_error("Configuration problem: there must be a keyboard controller at address 0x01");
        }
        return false;
    }

    if (!pri_crt_found) {
        if (warn) {
            UI_error("Configuration problem: there must be a CRT controller at address 0x05");
        }
        return false;
    }

    return true;
}


// returns true if the state has changed in a way that requires a reboot.
// that is, if the disk realtime or warning flags are the only things to
// have changed, or if nothing has changed, then a reboot isn't required.
bool
SysCfgState::needsReboot(const SysCfgState &other) const
{
    if (!m_initialized) {
        return true;
    }

    // check for things that do require a reset
    if (m_cpu_type != other.m_cpu_type) {
        return true;
    }

    if (m_ramsize != other.m_ramsize) {
        return true;
    }

    for (int slot=0; slot < NUM_IOSLOTS; slot++) {

        if (m_slot[slot].type != other.m_slot[slot].type) {
            return true;
        }

        if ((m_slot[slot].type != IoCard::card_t::none) &&
            (m_slot[slot].addr & 0xFF) != (other.m_slot[slot].addr & 0xFF)) {
            return true;
        }

        if ((m_slot[slot].card_cfg != nullptr) &&
             m_slot[slot].card_cfg->needsReboot(*other.m_slot[slot].card_cfg)) {
            return true;
        }
    }

    return false;
}

// vim: ts=8:et:sw=4:smarttab
