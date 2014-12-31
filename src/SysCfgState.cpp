// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "CardCfgState.h"
#include "CardInfo.h"
#include "Cpu2200.h"
#include "Host.h"
#include "IoCard.h"
#include "System2200.h"
#include "SysCfgState.h"
#include "Ui.h"                 // for UI_Alert

#include <sstream>
using std::ostringstream;

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// default constructor
SysCfgState::SysCfgState() :
    m_initialized(false),
    m_cputype(Cpu2200::CPUTYPE_2200T),
    m_ramsize(32),
    m_speed_regulated(true),
    m_disk_realtime(true),
    m_warn_io(true)
{
    for(int slot=0; slot<NUM_IOSLOTS; slot++)
        m_slot[slot].cardCfg = NULL;
}


SysCfgState::~SysCfgState()
{
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (m_slot[slot].cardCfg != NULL) {
            delete m_slot[slot].cardCfg;
            m_slot[slot].cardCfg = NULL;
        }
    }
}


// assignment
SysCfgState&
SysCfgState::operator=(const SysCfgState &rhs)
{
    // don't copy something that hasn't been initialized
    ASSERT(rhs.m_initialized);

    // check for self-assignment
    if (this == &rhs)
        return *this;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        m_slot[slot].type = rhs.m_slot[slot].type;
        m_slot[slot].addr = rhs.m_slot[slot].addr;
        // here we must do a deep copy:
        if (m_slot[slot].cardCfg != NULL) {
            delete m_slot[slot].cardCfg;
            m_slot[slot].cardCfg = NULL;
        }
        if (rhs.m_slot[slot].cardCfg != NULL)
            m_slot[slot].cardCfg = rhs.m_slot[slot].cardCfg->clone();
    }

    setCpuType( rhs.getCpuType() );
    setRamKB( rhs.getRamKB() );
    regulateCpuSpeed( rhs.isCpuSpeedRegulated() );
    setDiskRealtime( rhs.getDiskRealtime() );
    setWarnIo( rhs.getWarnIo() );

    return *this;
}


// copy constructor
SysCfgState::SysCfgState(const SysCfgState &obj)
{
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        m_slot[slot].type = obj.m_slot[slot].type;
        m_slot[slot].addr = obj.m_slot[slot].addr;
        // here we must do a deep copy:
        if (obj.m_slot[slot].cardCfg != NULL)
            m_slot[slot].cardCfg = obj.m_slot[slot].cardCfg->clone();
        else
            m_slot[slot].cardCfg = NULL;
    }

    setCpuType( obj.getCpuType() );
    setRamKB( obj.getRamKB() );
    regulateCpuSpeed( obj.isCpuSpeedRegulated() );
    setDiskRealtime( obj.getDiskRealtime() );
    setWarnIo( obj.getWarnIo() );
}


// equality comparison
bool
SysCfgState::operator==(const SysCfgState &rhs) const
{
    ASSERT(    m_initialized);
    ASSERT(rhs.m_initialized);

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if ( (m_slot[slot].type    != rhs.m_slot[slot].type) ||
             (m_slot[slot].addr    != rhs.m_slot[slot].addr) ||
             ( (    m_slot[slot].cardCfg == NULL) !=
               (rhs.m_slot[slot].cardCfg == NULL) ) )
            return false;

        if ( (    m_slot[slot].cardCfg != NULL) &&
             (rhs.m_slot[slot].cardCfg != NULL) &&
             (*m_slot[slot].cardCfg != *rhs.m_slot[slot].cardCfg) )
            return false;
    }

    return (getCpuType()          == rhs.getCpuType())          &&
           (getRamKB()            == rhs.getRamKB())            &&
           (isCpuSpeedRegulated() == rhs.isCpuSpeedRegulated()) &&
           (getDiskRealtime()     == rhs.getDiskRealtime())     &&
           (getWarnIo()           == rhs.getWarnIo())           ;
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
    for(int slot=0; slot < NUM_IOSLOTS; slot++) {
        setSlotCardType( slot, IoCard::card_none );
        setSlotCardAddr( slot, 0x000 );
    }

    setSlotCardType( 0, IoCard::card_keyboard );
    setSlotCardAddr( 0, 0x001 );

    setSlotCardType( 1, IoCard::card_disp_64x16 );
    setSlotCardAddr( 1, 0x005 );

    setSlotCardType( 2, IoCard::card_disk );
    setSlotCardAddr( 2, 0x310 );

    setSlotCardType( 3, IoCard::card_printer );
    setSlotCardAddr( 3, 0x215 );

    m_initialized = true;
}


// read from configuration file
void
SysCfgState::loadIni()
{
    Host hst;

    // get CPU attributes and start accumulating new configuration changes
    {
        string subgroup("cpu");
        string sval;
        int    ival;

        setCpuType( Cpu2200::CPUTYPE_2200T );  // default
        bool b = hst.ConfigReadStr(subgroup, "cpu", &sval);
        if (b) {
                 if (sval == "2200B")  setCpuType( Cpu2200::CPUTYPE_2200B );
            else if (sval == "2200T")  setCpuType( Cpu2200::CPUTYPE_2200T );
            else if (sval == "2200VP") setCpuType( Cpu2200::CPUTYPE_2200VP );
        }

        int dflt_ram = (getCpuType() == Cpu2200::CPUTYPE_2200VP) ? 64 : 32;
        b = hst.ConfigReadInt(subgroup, "memsize", &ival, dflt_ram);
        if (b) {
            switch (m_cputype) {
                case Cpu2200::CPUTYPE_2200B:
                case Cpu2200::CPUTYPE_2200T:
                    if (ival ==  4 || ival ==  8 || ival == 12 ||
                        ival == 16 || ival == 24 || ival == 32)
                        setRamKB( ival );
                    break;
                case Cpu2200::CPUTYPE_2200VP:
                    if (ival ==  32 || ival ==  64 ||
                        ival == 128 || ival == 256 ||
                        ival == 512 )
                        setRamKB( ival );
                    break;
                default:
                    ASSERT(0);
            }
        }

        // learn whether CPU speed is regulated or not
        regulateCpuSpeed( true );  // default
        b = hst.ConfigReadStr(subgroup, "speed", &sval);
        if (b && (sval == "unregulated"))
            regulateCpuSpeed( false );
    }

    // get IO slot attributes
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {

        ostringstream osf;
        osf << "io/slot-" << slot;
        string subgroup(osf.str());
        string sval;
        int b;
        IoCard::card_type_e cardtype = IoCard::card_none;

        int io_addr;
        (void)hst.ConfigReadInt(subgroup, "addr", &io_addr, -1);
        b =   hst.ConfigReadStr(subgroup, "type", &sval);
        if (b)
            cardtype = CardInfo::getCardTypeFromName(sval);

        // TODO: ideally, we'd check the card type against the list of
        //       addresses allowed for that card type
        bool plausible_card =
            ((0 <= cardtype) && (cardtype < (int)IoCard::NUM_CARDTYPES)) &&
            ((0 <= io_addr)  && (io_addr <= 0xFFF));

        if (plausible_card) {
            setSlotCardType( slot, cardtype );
            setSlotCardAddr( slot, io_addr );
            // if cardtype isCardConfigurable(), make a new config object,
            // and try to load it from the ini
            if (CardInfo::isCardConfigurable(cardtype)) {
                char cardsubgroup[30];
                sprintf(cardsubgroup, "io/slot-%d/cardcfg", slot);
                // dump any attached cardCfg state
                if (m_slot[slot].cardCfg != NULL) {
                    delete m_slot[slot].cardCfg;
                    m_slot[slot].cardCfg = NULL;
                }
                m_slot[slot].cardCfg = CardInfo::getCardCfgState(cardtype);
                m_slot[slot].cardCfg->loadIni(cardsubgroup);
            }
        } else {
            // the slot is empty
            setSlotCardType( slot, IoCard::card_none );
            setSlotCardAddr( slot, 0x000 );
        }
    }

    // get misc other config bits
    {
        string subgroup("misc");
        bool bval;

        hst.ConfigReadBool(subgroup, "disk_realtime", &bval, true);
        setDiskRealtime( bval );  // default

        hst.ConfigReadBool(subgroup, "warnio", &bval, true);
        setWarnIo( bval );  // default
    }

    m_initialized = true;
}


// save to configuration file
void
SysCfgState::saveIni() const
{
    ASSERT(m_initialized);

    Host hst;

    // save IO information
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        char subgroup[20];
        sprintf(subgroup, "io/slot-%d", slot);

        IoCard::card_type_e cardtype = m_slot[slot].type;
        if (cardtype != IoCard::card_none) {
            char val[10];
            sprintf(val, "0x%03X", m_slot[slot].addr);
            string cardname = CardInfo::getCardName(cardtype);
            hst.ConfigWriteStr(subgroup, "type", cardname);
            hst.ConfigWriteStr(subgroup, "addr", val);
        } else {
            hst.ConfigWriteStr(subgroup, "type", "");
            hst.ConfigWriteStr(subgroup, "addr", "");
        }

        if (m_slot[slot].cardCfg != NULL) {
            char cardsubgroup[30];
            sprintf(cardsubgroup, "io/slot-%d/cardcfg", slot);
            m_slot[slot].cardCfg->saveIni(cardsubgroup);
        }

    } // for(slot=NUM_IOSLOTS)

    // save CPU information
    {
        const string subgroup("cpu");

        const char *foo = (m_cputype == Cpu2200::CPUTYPE_2200B) ? "2200B" :
                          (m_cputype == Cpu2200::CPUTYPE_2200T) ? "2200T" :
                                                                  "2200VP";
        hst.ConfigWriteStr(subgroup, "cpu", foo);

        hst.ConfigWriteInt(subgroup, "memsize", getRamKB());

        System2200 sys;
        foo = (sys.isCpuSpeedRegulated()) ? "regulated" : "unregulated";
        hst.ConfigWriteStr(subgroup, "speed", foo);
    }

    // save misc other config bits
    {
        const string subgroup("misc");
        hst.ConfigWriteBool(subgroup, "disk_realtime", getDiskRealtime());
        hst.ConfigWriteBool(subgroup, "warnio",        getWarnIo());
    }
}


void
SysCfgState::setCpuType(int type)
{
    m_cputype = type;
    m_initialized = true;
}

void
SysCfgState::regulateCpuSpeed(bool regulated)
{
    m_speed_regulated = regulated;
    m_initialized = true;
}

void
SysCfgState::setRamKB(int kb)
{
    switch (kb) {

    // OK for A,B,C,S,T
        case  4:
        case  8:
        case 12:
        case 16:
        case 24:
            ASSERT(m_cputype != Cpu2200::CPUTYPE_2200VP);
            m_ramsize = kb;
            break;

    // OK for either CPU type
        case 32:
            m_ramsize = kb;
            break;

    // OK only for VP
        case 64:
        case 128:
        case 256:
        case 512:
            ASSERT(m_cputype == Cpu2200::CPUTYPE_2200VP);
            m_ramsize = kb;
            break;

    // should never happen
        default:
            ASSERT(0);
            m_ramsize = 32;
            break;
    }

    m_initialized = true;
}

void
SysCfgState::setDiskRealtime(bool realtime)
{
    m_disk_realtime = realtime;
    m_initialized = true;
}

void
SysCfgState::setWarnIo(bool warn)
{
    m_warn_io = warn;
    m_initialized = true;
}

// set the card type.  if the card type is configurable, set up a cardCfg
// object of the appropriate type, discarding whatever was there before.
void
SysCfgState::setSlotCardType(int slot, IoCard::card_type_e type)
{
    m_slot[slot].type = type;

    // create a config state object if the card type needs one
    if ((type != IoCard::card_none) && (CardInfo::isCardConfigurable(type))) {
        if (m_slot[slot].cardCfg != NULL) {
            delete m_slot[slot].cardCfg;
            m_slot[slot].cardCfg = NULL;
        }
        m_slot[slot].cardCfg = CardInfo::getCardCfgState(type);
        m_slot[slot].cardCfg->setDefaults();
    }
    m_initialized = true;
}

void
SysCfgState::setSlotCardAddr(int slot, int addr)
{
    m_slot[slot].addr = addr;
    m_initialized = true;
}

int
SysCfgState::getCpuType() const
        { return m_cputype; }

bool
SysCfgState::isCpuSpeedRegulated() const
        { return m_speed_regulated; }

int
SysCfgState::getRamKB() const
        { return m_ramsize; }

bool
SysCfgState::getDiskRealtime() const
        { return m_disk_realtime; }

bool
SysCfgState::getWarnIo() const
        { return m_warn_io; }

IoCard::card_type_e
SysCfgState::getSlotCardType(int slot) const
        { return m_slot[slot].type; }

bool
SysCfgState::isSlotOccupied(int slot) const
        { return m_slot[slot].type != IoCard::card_none; }

int
SysCfgState::getSlotCardAddr(int slot) const
        { return m_slot[slot].addr; }


// retrieve the pointer to the per-card configuration state
const CardCfgState *
SysCfgState::getCardConfig(int slot) const
{
    ASSERT(isSlotOccupied(slot));
    return m_slot[slot].cardCfg;
}


// twiddle the state of the card in the given slot
void
SysCfgState::editCardConfig(int slot)
{
    ASSERT(isSlotOccupied(slot));

    CardCfgState *cardCfg = m_slot[slot].cardCfg;
    if (cardCfg != NULL) {
        IoCard *inst = System2200().getInstFromSlot(slot);
        if (inst == NULL) {
            // this must be a newly created slot that hasn't been put into
            // the IoMap yet.  create a temp object so we can edit the cardCfg.
            inst = IoCard::makeTmpCard(m_slot[slot].type);
            inst->editConfiguration(cardCfg);
            delete inst;
        } else {
            inst->editConfiguration(cardCfg);
        }
    }
}


// returns true if the current configuration is reasonable, and false if not.
// if returning false, this routine first calls UI_Alert() describing what
// is wrong.
bool
SysCfgState::configOk(bool warn) const
{
    if (!m_initialized)
        return false;

    bool pri_crt_found = false;
    bool pri_kb_found  = false;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {

        if (m_slot[slot].type == IoCard::card_none)
            continue;

        // make sure we have a keyboard at 0x01
        if ( (m_slot[slot].type == IoCard::card_keyboard) &&
             (m_slot[slot].addr & 0xFF) == 0x01)
            pri_kb_found = true;

        // make sure we have a crt at 0x05
        if ( ((m_slot[slot].type == IoCard::card_disp_64x16) ||
              (m_slot[slot].type == IoCard::card_disp_80x24)) &&
             (m_slot[slot].addr & 0xFF) == 0x05)
            pri_crt_found = true;

        // check for address conflicts
        for(int j=slot+1; j<NUM_IOSLOTS; j++) {

            if (m_slot[j].type == IoCard::card_none)
                continue;

            if ((m_slot[slot].type != IoCard::card_none) &&
                (m_slot[slot].addr & 0xFF) == (m_slot[j].addr & 0xFF)) {
                if (warn)
                    UI_Error("Configuration problem: "
                             "card in slots %d and %d both responding to address 0x%02X",
                             slot, j, m_slot[slot].addr & 0xFF);
                return false;
            }
        }
    }

    if (!pri_kb_found) {
        if (warn)
            UI_Error("Configuration problem: there must be a keyboard controller at address 0x01");
        return false;
    }

    if (!pri_crt_found) {
        if (warn)
            UI_Error("Configuration problem: there must be a CRT controller at address 0x05");
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
    if (!m_initialized)
        return true;

    // check for things that do require a reset
    if (m_cputype != other.m_cputype)
        return true;

    if (m_ramsize != other.m_ramsize)
        return true;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {

        if (m_slot[slot].type != other.m_slot[slot].type)
            return true;

        if ( (m_slot[slot].type != IoCard::card_none) &&
             (m_slot[slot].addr & 0xFF) != (other.m_slot[slot].addr & 0xFF) )
            return true;

        if ( (m_slot[slot].cardCfg != NULL) &&
              m_slot[slot].cardCfg->needsReboot(*other.m_slot[slot].cardCfg))
            return true;
    }

    return false;
}
