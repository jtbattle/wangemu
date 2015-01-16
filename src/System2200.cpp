// this encapsulates the system under emulation.

#include "CardInfo.h"
#include "Cpu2200.h"
#include "Host.h"
#include "IoCardDisk.h"
#include "Scheduler.h"
#include "System2200.h"
#include "SysCfgState.h"
#include "Ui.h"

// ----------------------------------------------------------------------------
// initialize static class members
// ----------------------------------------------------------------------------

// declare the system singleton members
bool          System2200::m_initialized = false;
Scheduler*    System2200::m_scheduler   = 0;
Cpu2200*      System2200::m_cpu         = 0;
SysCfgState*  System2200::m_config      = 0;

System2200::term_state_t
            System2200::m_term_state  = RUNNING;
bool        System2200::m_freeze_emu  = false;
bool        System2200::m_do_reconfig = false;

// mapping i/o addresses to devices
System2200::iomap_t System2200::m_IoMap[256];
IoCard*             System2200::m_cardInSlot[NUM_IOSLOTS];
int                 System2200::m_IoCurSelected;

// speed regulation
int64   System2200::perf_real_ms[100];    // realtime at start of each slice
int     System2200::perf_hist_len;        // number of entries written
int     System2200::perf_hist_ptr;        // next entry to write
bool    System2200::m_regulated_speed;    // try to match speed of real system
bool    System2200::m_firstslice;         // has m_realtimestart been initialized?
int64   System2200::m_realtimestart;      // wall time of when sim started
int     System2200::m_real_seconds;       // real time elapsed

unsigned long System2200::m_simsecs; // number of actual seconds simulated time elapsed

// amount of actual simulated time elapsed, in ms
int64 System2200::m_simtime;

// amount of adjusted simulated time elapsed, in ms because of user events
// (eg, menu selection), sim time is often interrupted and we don't
// actually desire to catch up.  m_simtime is the actual number of
// simulated slices, while this var has been fudged to account for those
// violations of realtime.
int64 System2200::m_adjsimtime;

// ------------------------------------------------------------------------
//  a small logging facility
// ------------------------------------------------------------------------

#include <cstdarg>      // for var args
#include <fstream>
using std::ofstream;

static ofstream dbg_ofs;

#ifdef _DEBUG
void
dbglog_open(const string &filename)
{
    assert(!dbg_ofs.is_open());     // only one log at a time
    dbg_ofs.open( filename.c_str(), ofstream::out | ofstream::trunc );
    if (!dbg_ofs.good()) {
        UI_Error("Error opening '%s' for logging.\n", filename.c_str());
        exit(-1);
    }
}

void
dbglog_close()
{
    if (dbg_ofs.is_open())
        dbg_ofs.close();
}
#endif // _DEBUG

void
dbglog(const char *fmt, ...)
{
#ifdef _DEBUG
    char buff[1000];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    if (dbg_ofs.good()) {
        dbg_ofs << buff;
        // this is useful if we are getting assert()s, causing
        // the last buffered block to not appear in the log.
        dbg_ofs.flush();
    }
#else
    bool foo = (fmt != 0); foo = foo;   // keep lint happy
#endif
}

// ------------------------------------------------------------------------
// public members
// ------------------------------------------------------------------------

// constructor
System2200::System2200()
{
    if (!m_initialized) {
        // must set this early, otherwise during ramp up, places that
        // invoke System2200 will cause recursion
        m_initialized = true;
#ifdef _DEBUG
        dbglog_open("w2200dbg.log");
#endif
        initialize();
    }

}


// destructor
System2200::~System2200()
{
    // this singleton is torn down all the time, but it doesn't mean we can
    // release any state.  cleanup() is the function that cleans house.
}


// initialize singleton
void
System2200::initialize()
{
    // set up IO management
    for(int i=0; i<256; i++) {
        m_IoMap[i].inst   = nullptr;
        m_IoMap[i].ignore = false;
    }
    for(int i=0; i<NUM_IOSLOTS; i++)
        m_cardInSlot[i] = nullptr;
    m_IoCurSelected = -1;

    // CPU speed regulation
    m_firstslice = true;
    m_simtime    = m_adjsimtime = Host().getTimeMs();
    m_simsecs    = 0;

    m_realtimestart = 0;    // wall time of when sim started
    m_real_seconds  = 0;    // real time elapsed

    m_do_reconfig = false;
    freezeEmu(false);
    setTerminationState(RUNNING);

    m_scheduler = new Scheduler();
    assert(m_scheduler != 0);

    // attempt to load configuration from saved state
    SysCfgState ini_cfg;
    ini_cfg.loadIni();
    if (!ini_cfg.configOk(false)) {
        UI_Warn(".ini file wasn't usable -- using a default configuration");
        ini_cfg.setDefaults();
    }
    setConfig(ini_cfg);
}


// break down any resources currently committed
void
System2200::breakdown_cards(void)
{
    // clean out associations that no longer exist.
    // if we find a video display, we must trash it.
    for(int i=0; i<256; i++) {
        m_IoMap[i].inst   = 0;
        m_IoMap[i].ignore = false;   // restore bad I/O warning flags
    }
    for(int i=0; i<NUM_IOSLOTS; i++) {
        delete m_cardInSlot[i];
        m_cardInSlot[i] = nullptr;
    }

    if (cpuExists())
        cpu().setDevRdy(true); // nobody is driving, so it floats to 1
}


// build a system according to the spec.
// if a system already exists, tear it down and rebuild it.
void
System2200::setConfig(const SysCfgState &newcfg)
{
    if (m_config == 0) {
        // first time we don't need to tear anything down
        m_config = new SysCfgState();
    } else {
        // check if the change is minor, not requiring a teardown
        bool rebuild_required = config().needsReboot(newcfg);
        if (!rebuild_required) {
            *m_config = newcfg;  // make new config permanent
            // notify all configured cards about possible new configuration
            for(int slot=0; slot < NUM_IOSLOTS; slot++) {
                if (config().isSlotOccupied(slot)) {
                    IoCard::card_type_e ct = config().getSlotCardType(slot);
                    if (CardInfo::isCardConfigurable(ct)) {
                        IoCard *card = getInstFromSlot(slot);
                        const CardCfgState *cfg = config().getCardConfig(slot);
                        card->setConfiguration(*cfg);
                    }
                }
            }
            return;
        }

        // the change was major, so delete existing resources
        assert(m_cpu != 0);
        delete m_cpu;
        m_cpu = 0;

        // remember which virtual disks are installed
        saveDiskMounts();
        // throw away all existing devices
        breakdown_cards();
    }

    // save the new system configuration state
    *m_config = newcfg;

    // (re)build the CPU
    int ramsize = config().getRamKB();
    switch (config().getCpuType()) {
        case Cpu2200::CPUTYPE_2200B:
            m_cpu = new Cpu2200t( *this, scheduler(), ramsize, Cpu2200::CPUTYPE_2200B );
            break;
        default: assert(0);
        case Cpu2200::CPUTYPE_2200T:
            m_cpu = new Cpu2200t( *this, scheduler(), ramsize, Cpu2200::CPUTYPE_2200T );
            break;
        case Cpu2200::CPUTYPE_2200VP:
            m_cpu = new Cpu2200vp( *this, scheduler(), ramsize, Cpu2200::CPUTYPE_2200VP );
            break;
    }
    assert(m_cpu != 0);

    // build cards that go into each slot.
    // a hack -- when a display card is made, the crtframe status bar queries
    // to find out how many disk drives are associated with each drive.
    // if the display card is built before the disk controllers, the status
    // bar will be misconfigured.  the hack is to sweep the list twice,
    // skipping CRT's the first time, then doing only CRTs the second time.

    for(int pass=0; pass<2; pass++) {
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {

        if (!config().isSlotOccupied(slot))
            continue;

        IoCard::card_type_e cardtype = config().getSlotCardType(slot);
        int io_addr                  = config().getSlotCardAddr(slot) & 0xFF;

        bool display = (cardtype == IoCard::card_disp_64x16) ||
                       (cardtype == IoCard::card_disp_80x24) ;

        if ((pass==0 && display) || (pass==1 && !display))
            continue;

        IoCard *inst = IoCard::makeCard(scheduler(), cpu(), cardtype, io_addr,
                                        slot, config().getCardConfig(slot));
        if (inst == 0) {
            // failed to install
            UI_Warn("Configuration problem: failure to create slot %d card instance", slot);
        } else {
            m_cardInSlot[slot] = inst;
            vector<int> addresses = inst->getAddresses();
            for(unsigned int n=0; n<addresses.size(); n++)
                m_IoMap[addresses[n]].inst = inst;
        }
    }}

    restoreDiskMounts();         // remount disks
}


// because everything is static, the destructor does nothing, so we
// need this function to know when the real armageddon has arrived.
void
System2200::cleanup()
{
    // remember which disks are saved in each drive
    saveDiskMounts();

    breakdown_cards();

    delete m_cpu;
    m_cpu = 0;

    delete m_scheduler;
    m_scheduler = 0;

    config().saveIni();  // save state to ini file
    delete m_config;
    m_config = 0;

#ifdef _DEBUG
    dbglog_close();               // turn off logging
#endif
}


// reset the cpu
void
System2200::reset(bool cold_reset)
{
    cpu().reset(cold_reset);

    // reset all I/O devices
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (config().isSlotOccupied(slot))
            m_cardInSlot[slot]->reset(cold_reset);
    }
}


// called whenever there is free time
bool
System2200::onIdle()
{
    if (m_do_reconfig) {
        m_do_reconfig = false;
        freezeEmu(true);
        UI_SystemConfigDlg();
        freezeEmu(false);
    }

    // this is something that needs to be tuned.  there is some unholy
    // interaction of this parameter with the windows scheduling due to
    // the fact that we call sleep(0) at the end of each slice.  small
    // changes in this value can have significant non-monotonic impact
    // on emulator performance.
    static const int slice_duration = 30;   // in ms

    switch (getTerminationState()) {
        case RUNNING:
            // this is the normal case during emulation
            if (m_freeze_emu) {
                // if we don't call sleep, we just get another onIdle event
                // and end up pegging the host CPU
                Host().sleep(10);
            } else
                emulateTimeslice(slice_duration);
            return true;        // want more idle events
        case TERMINATING:
            // we've been signalled to shut down the universe.
            // change the flag to know we've already cleaned up, in case
            // we receive another onIdle call.
            setTerminationState(TERMINATED);
            cleanup();
            break;
        case TERMINATED:
            // do nothing -- we already requested a cleanup
            break;
        default:
            assert(0);
            break;
    }

    return false;        // don't want any more idle events
}


// simulate a few ms worth of instructions
void
System2200::emulateTimeslice(int ts_ms)
{
    Host hst;

    // try to stae reatime within this window
    const int64 adj_window = 10*ts_ms;  // look at the last 10 timeslices

    if (cpu().status() != Cpu2200::CPU_RUNNING)
        return;

    uint64 now_ms = hst.getTimeMs();
    int64 realtime_elapsed;
    int64 offset;

    if (m_firstslice) {
        m_firstslice = false;
        m_realtimestart = now_ms;
    }
    realtime_elapsed = now_ms - m_realtimestart;
    offset = m_adjsimtime - realtime_elapsed;

    if (offset > adj_window) {
        // we're way ahead (probably because we are running unregulated)
        m_adjsimtime = realtime_elapsed + adj_window;
        offset = adj_window;
    } else if (offset < -adj_window) {
        // we've fallen way behind; catch up so we don't
        // run like mad after any substantial pause
        m_adjsimtime = realtime_elapsed - adj_window;
        offset = -adj_window;
    }

    if ((offset > 0) && isCpuSpeedRegulated()) {

        // we are running ahead of schedule; kill some time.
        // we don't kill the full amount becuase the sleep function is
        // allowed to, and very well might, sleep longer than we asked.
        unsigned int ioffset = (unsigned int)(offset & (int64)0xFFF);  // bottom 4 sec or so
        hst.sleep(ioffset/2);

    } else {

        // keep track of when each slice started
        perf_real_ms[perf_hist_ptr++] = now_ms;
        if (perf_hist_ptr >= perf_hist_size)
            perf_hist_ptr -= perf_hist_size;
        if (perf_hist_len < perf_hist_size)
            perf_hist_len++;

        // simulate one timeslice's worth of instructions
        cpu().run(ts_ms*10000);  // 10 MHz = 10,000 clks/ms
        m_simtime    += ts_ms;
        m_adjsimtime += ts_ms;

        if (cpu().status() != Cpu2200::CPU_RUNNING) {
            UI_Warn("CPU halted -- must reset");
            cpu().reset(true);  // hard reset
            return;
        }

        m_simsecs = (unsigned long)((m_simtime/1000) & 0xFFFFFFFF);

        int real_seconds_now = (int)(realtime_elapsed/1000);
        if (m_real_seconds != real_seconds_now) {
            m_real_seconds = real_seconds_now;
            if (perf_hist_len > 10) {
                // compute running performance average over the
                // last real second or so
                int n1 = (perf_hist_ptr - 1 + perf_hist_size)
                       % perf_hist_size;
                int64 ms_diff = 0;
                int slices = 0;
                for(int n=1; n<perf_hist_len; n+=10) {
                    int n0 = (n1 - n + perf_hist_size) % perf_hist_size;
                    slices = n;
                    ms_diff = (perf_real_ms[n1] - perf_real_ms[n0]);
                    if (ms_diff > 1000)
                        break;
                }
                float relative_speed = (float)(slices*ts_ms) / (float)ms_diff;

                // update the status bar with simulated seconds and performance
                UI_setSimSeconds(m_simsecs, relative_speed);
            }
        }

        // at least yield so we don't hog the whole machine
        hst.sleep(0);
    }
}


// called from EmCRTframe to signal we want to end things.
// as such, at this point nothing has been shut down.
// after all windows have been shut down, we receive OnExit().
void
System2200::terminate()
{
    setTerminationState(TERMINATING);
}


// the user requests a change in configuration from the UiFrontPanel.
// however, doing so often requires a tear down and rebuild of all the
// components.  destroying the frontpanel instance and then returning
// to it is uncouth.  instead, when the user wants to reconfigure, this
// function is called and that desire is noted, and we return immediately.
// later on, in the onIdle code, this flag is checked and the reconfiguration
// happens then.
void
System2200::reconfigure()
{
    m_do_reconfig = true;
}


// turn cpu speed regulation on (true) or off (false)
// this is a convenience function
void
System2200::regulateCpuSpeed(bool regulated)
{
    config().regulateCpuSpeed(regulated);

    // reset the performance monitor history
    perf_hist_len = 0;
    perf_hist_ptr = 0;
}


// indicate if the CPU is throttled or not
// this is a convenience function
bool
System2200::isCpuSpeedRegulated() const
{
    return m_config->isCpuSpeedRegulated();
}


// ========================================================================
//    io dispatch functions (used by core sim)
// ========================================================================

// address byte strobe
void
System2200::cpu_ABS(uint8 byte)
{
    // done if reselecting same device
    if (byte == m_IoCurSelected)
        return;

    if ((m_IoCurSelected > 0) && (m_IoMap[m_IoCurSelected].inst != nullptr)) {
        (m_IoMap[m_IoCurSelected].inst)->deselect();
        m_IoCurSelected = -1;
    }

    cpu().setDevRdy(true); // nobody is driving, so it floats to 1

    m_IoCurSelected = byte;
    if (m_IoMap[m_IoCurSelected].inst == nullptr) {
        // ignore 0x00, which is used as a "deselect everything" address
        if ( (m_IoCurSelected != 0x00) &&
             !m_IoMap[m_IoCurSelected].ignore ) {
            if (config().getWarnIo()) {
                bool response = UI_Confirm(
                            "Warning: selected non-existent I/O device %02X\n"
                            "Should I warn you of further accesses to this device?",
                            m_IoCurSelected );
                // suppress further warnings
                m_IoMap[m_IoCurSelected].ignore = !response;
            }
        }
    } else {
        (m_IoMap[m_IoCurSelected].inst)->select();
    }
}


// output byte strobe
void
System2200::cpu_OBS(uint8 byte)
{
// p 6-2 of the Wang 2200 Service Manual:
// When the controller is selected (select latch set), the Ready/Busy
// Decoder will indicate Ready (active low) to the CPU only if the peripheral
// device is not doing an operation.  The Ready indicator will stay active
// until the peripheral device being used generates a Busy indicator,
// allowing the CPU to do another I/O operaiton.  Normally, the device
// being used will generate a Busy indicator after the I/O Bus (!OB1 - !OB8)
// has been strobed by !OBS, the CPU output strobe.
    if (m_IoCurSelected > 0) {
        if (m_IoMap[m_IoCurSelected].inst != nullptr) {
            (m_IoMap[m_IoCurSelected].inst)->OBS(byte);
        }
    }
}


// handles CBS strobes
void
System2200::cpu_CBS()
{
    // each card handles CBS in its own way.
    //   * many cards simply ignore it
    //   * some use it like another OBS strobe to capture some type
    //     of command word
    //   * some cards use it to trigger an IBS strobe
    if ((m_IoCurSelected > 0) && (m_IoMap[m_IoCurSelected].inst != nullptr))
        (m_IoMap[m_IoCurSelected].inst)->CBS(0);
}


// notify selected card when CPB changes
void
System2200::cpu_CPB(bool busy)
{
    if  ((m_IoCurSelected > 0) && (m_IoMap[m_IoCurSelected].inst != nullptr))
        // signal that we want to get something
        (m_IoMap[m_IoCurSelected].inst)->CPB(busy);
}


// the CPU can poll IB5 without any other strobe.  return that bit.
// FIXME: on the VP cpu, the entire I bus is strobed into K, not
//        just bit 5.  I don't know of any IO cards that use more
//        than bit 5, but generalize this anyway for the day that
//        I run into such a card.
int
System2200::cpu_poll_IB5()
{
    if  ((m_IoCurSelected > 0) && (m_IoMap[m_IoCurSelected].inst != nullptr)) {
        // signal that we want to get something
        return (int)(m_IoMap[m_IoCurSelected].inst)->getIB5();
    }
    return 0;
}


// ========================================================================
// external interface to the slot manager
// ========================================================================

// returns false if the slot is empty, otherwise true.
// returns card type index and io address via pointers.
bool
System2200::getSlotInfo(int slot, int *cardtype_idx, int *addr)
{
    assert(0 <= slot && slot < NUM_IOSLOTS);
    if (!config().isSlotOccupied(slot))
        return false;

    if (cardtype_idx != 0)
        *cardtype_idx = (int)config().getSlotCardType(slot);

    if (addr != 0)
        *addr = config().getSlotCardAddr(slot);

    return true;
}


// returns the IO address of the n-th keyboard (0-based).
// if (n >= # of keyboards), returns -1.
int
System2200::getKbIoAddr(int n)
{
    int num = 0;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (config().getSlotCardType(slot) == IoCard::card_keyboard) {
            if (num == n)
                return config().getSlotCardAddr(slot);
            num++;
        }
    }

    return -1; // failed to find it
}


// returns the IO address of the n-th printer (0-based).
// if (n >= # of printers), returns -1.
int
System2200::getPrinterIoAddr(int n)
{
    int num = 0;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (config().getSlotCardType(slot) == IoCard::card_printer) {
            if (num == n)
                return config().getSlotCardAddr(slot);
            num++;
        }
    }

    return -1;  // failed to find it
}


// return the instance handle of the device at the specified IO address
IoCard*
System2200::getInstFromIoAddr(int io_addr)
{
    assert( (io_addr >= 0) && (io_addr <= 0xFFF) );
    return m_IoMap[io_addr & 0xFF].inst;
}


// given a slot, return the "this" element
IoCard*
System2200::getInstFromSlot(int slot)
{
    assert(slot >=0 && slot < NUM_IOSLOTS);

    int io_addr = config().getSlotCardAddr(slot);
    if (io_addr < 0)
        return 0;

    return m_IoMap[io_addr & 0xFF].inst;
}


// returns true if the slot contains a disk controller, 0 otherwise
bool
System2200::isDiskController(int slot)
{
    assert(slot >= 0 && slot < NUM_IOSLOTS);

    int cardtype_idx;
    bool ok = System2200().getSlotInfo(slot, &cardtype_idx, 0);

    return (ok && (cardtype_idx == IoCard::card_disk));
}


// find slot number of disk controller #n.
// returns true if successful.
bool
System2200::findDiskController(const int n, int *slot)
{
    int num_ioslots = NUM_IOSLOTS;
    int numfound = 0;

    assert(n >= 0);

    for(int probe=0; probe<num_ioslots; probe++) {
        if (isDiskController(probe)) {
            if (numfound++ == n) {
                *slot = probe;
                return true;
            }
        }
    } // for probe

    return false;
}


// find slot,drive of any disk controller with disk matching name.
// returns true if successful.
bool
System2200::findDisk(const string &filename,
                     int *slot, int *drive, int *io_addr)
{
    for(int controller=0; ; controller++) {

        int slt;
        if (!findDiskController(controller, &slt))
            break;

        const CardCfgState *cfg = config().getCardConfig(slt);
        const DiskCtrlCfgState *dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg);
        assert(dcfg != 0);
        int num_drives = dcfg->getNumDrives();
        for(int d=0; d<num_drives; d++) {
            int stat = IoCardDisk::wvdDriveStatus(slt, d);
            if (stat & IoCardDisk::WVD_STAT_DRIVE_OCCUPIED) {
                string fname;
                bool ok = IoCardDisk::wvdGetFilename(slt, d, &fname);
                assert(ok); ok=ok;
                if (filename == fname) {
                    if (slot != nullptr)
                        *slot = slt;
                    if (drive != nullptr)
                        *drive = d;
                    if (io_addr != nullptr) {
                        ok = System2200().getSlotInfo(slt, 0, io_addr);
                        assert(ok); ok = ok;
                    }
                    return true;
                }
            }
        } // for(d)
    } // for(controller)

    return false;
}


// ========================================================================
//     save and restore state
// ========================================================================

#include <sstream>
using std::ostringstream;

// for all disk drives, save what is mounted in them (or not)
void
System2200::saveDiskMounts(void)
{
    Host hst;

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (isDiskController(slot)) {
            ostringstream subgroup;
            subgroup << "io/slot-" << slot;
            string val;
            const CardCfgState *cfg = config().getCardConfig(slot);
            const DiskCtrlCfgState *dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg);
            assert(dcfg != 0);
            int num_drives = dcfg->getNumDrives();
            for(int drive=0; drive<num_drives; drive++) {
                ostringstream item;
                item << "filename-" << drive;
                string filename("");
                if (isDiskController(slot)) {
                    int stat = IoCardDisk::wvdDriveStatus(slot, drive);
                    if (stat & IoCardDisk::WVD_STAT_DRIVE_OCCUPIED) {
                        bool ok = IoCardDisk::wvdGetFilename(slot, drive, &filename);
                        assert(ok); ok=ok;
                    }
                }
                hst.ConfigWriteStr(subgroup.str(), item.str(), filename);
            } // drive
        } // if (isDiskController)
    } // slot
}

// remount all disks
void
System2200::restoreDiskMounts(void)
{
    Host hst;

    // look for disk controllers and populate drives
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (isDiskController(slot)) {
            const CardCfgState *cfg = config().getCardConfig(slot);
            const DiskCtrlCfgState *dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg);
            assert(dcfg != 0);
            int num_drives = dcfg->getNumDrives();
            for(int drive=0; drive<num_drives; drive++) {
                ostringstream subgroup;
                subgroup << "io/slot-" << slot;
                ostringstream item;
                item << "filename-" << drive;
                string filename;
                bool b = hst.ConfigReadStr(subgroup.str(), item.str(), &filename);
                if (b && !filename.empty()) {
                    bool bs = IoCardDisk::wvdInsertDisk(slot, drive, filename.c_str());
            #if 0 // wvdInsertDisk() already generated a warning
                    if (!bs) {
                        UI_Warn("Problem loading '%s' into slot %d, drive %d",
                                filename, slot, drive);
                    }
            #else
                    bs = bs; // keep lint happy
            #endif
                }
            } // for(drive)
        } // if (isDiskController)
    } // for(slot)
}
