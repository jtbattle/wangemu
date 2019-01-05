// this encapsulates the system under emulation.

#include "CardInfo.h"
#include "Cpu2200.h"
#include "IoCardDisk.h"
#include "IoCardKeyboard.h"  // for KEYCODE_HALT
#include "Scheduler.h"
#include "ScriptFile.h"
#include "SysCfgState.h"
#include "Ui.h"
#include "host.h"
#include "system2200.h"

#include <algorithm>
#include <sstream>

// ----------------------------------------------------------------------------
// this is the "private" state of the system2200 namespace,
// invisible to anyone importing system2200.h
// ----------------------------------------------------------------------------

// for coordinating events in the emulator
static std::shared_ptr<Scheduler> scheduler = nullptr;

// the central processing unit
static std::shared_ptr<Cpu2200> cpu = nullptr;

// active system configuration
static std::shared_ptr<SysCfgState> current_cfg = nullptr;

// ------------------------------- I/O dispatch -------------------------------

// mapping i/o addresses to devices
struct iomap_t {
    int  slot;      // slot number of the device which "owns" this address
    bool ignore;    // if false, access generates a warning message if ...
                    // ... there is no device at that address
};

// pointer to card in a given slot
static std::array<std::unique_ptr<IoCard>, NUM_IOSLOTS> card_in_slot;

// pointer to card responding to given address
static std::array<iomap_t,256> ioMap;

// address of most recent ABS
static int curIoAddr;

// ----------------------------- speed regulation -----------------------------

static bool  first_slice    = false; // has realtime_start been initialized?
static int64 realtime_start = 0;     // relative wall time of when sim started
static int   real_seconds   = 0;     // real time elapsed
static uint32 sim_seconds   = 0;     // number of actual seconds simulated time

// amount of actual simulated time elapsed, in ms
static int64 sim_time_ns;

// amount of adjusted simulated time elapsed, in ms because of user events
// (eg, menu selection), sim time is often interrupted and we don't
// actually desire to catch up. sim_time_ns is the actual number of
// simulated slices, while this var has been fudged to account for those
// violations of realtime.
static int64 adjust_sim_time;

// keep a rolling average of how fast we are running in case it is reported.
// we want to report the running average over the last second of realtime
// to prevent the average from updating like crazy when running many times
// realtime.
static const int perf_hist_size = 100;  // # of timeslices to track
static       int perf_hist_len  = 0;    // number of entries written
static       int perf_hist_ptr  = 0;    // next entry to write
static     int64 perf_real_ms[100];     // realtime at start of each slice

// things which get called as time advances. it is used by the
// core 2200 CPU and any peripheral which uses a microprocessor.
// each device has a ns resolution counter, but we keep rebasing
// the count so the counter doesn't overflow.  all we care is the
// difference between the devices' sense of time.
struct clocked_device_t {
    clkCallback callback_fn;
    uint32      ns;          // nanoseconds
};
static std::vector<clocked_device_t> m_clocked_devices;

// ----------------------- keyboard input routing table -----------------------

struct kb_route_t {
    int         io_addr;
    int         term_num;       // 0..3 for smart terms; 0 for display controllers
    kbCallback  callback_fn;
    // can't use unique_ptr because it doesn't allow copy assignment
    // (only move) and std::vector requires copy assignment.
    std::shared_ptr<ScriptFile> script_handle;
};

static std::vector<kb_route_t> keyboard_routes;

// ----------------------------------------------------------------------------
// initialize static class members
// ----------------------------------------------------------------------------

// help ensure an orderly shutdown
enum term_state_t {
    RUNNING,
    TERMINATING,
    TERMINATED
};
static term_state_t m_termination_state = RUNNING;

static bool m_freeze_emu  = false;  // toggle to prevent time advancing
static bool m_do_reconfig = false;  // deferred request to reconfigure

static void
setTerminationState(term_state_t newstate) noexcept
{
    m_termination_state = newstate;
}

static term_state_t
getTerminationState() noexcept
{
    return m_termination_state;
}

// ----------------------------------------------------------------------------
// save/restore state to/from ini file
// ----------------------------------------------------------------------------

// returns true if the slot contains a disk controller, 0 otherwise
static bool
isDiskController(int slot) noexcept
{
    assert(slot >= 0 && slot < NUM_IOSLOTS);

    int cardtype_idx;
    const bool ok = system2200::getSlotInfo(slot, &cardtype_idx, nullptr);

    return ok && (cardtype_idx == static_cast<int>(IoCard::card_t::disk));
}


// for all disk drives, save what is mounted in them (or not)
static void
saveDiskMounts()
{
    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (isDiskController(slot)) {
            std::ostringstream subgroup;
            subgroup << "io/slot-" << slot;
            const auto cfg = current_cfg->getCardConfig(slot);
            const auto dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg.get());
            assert(dcfg);
            const int num_drives = dcfg->getNumDrives();
            for (int drive=0; drive<num_drives; drive++) {
                std::ostringstream item;
                item << "filename-" << drive;
                std::string filename;
                if (isDiskController(slot)) {
                    const int stat = IoCardDisk::wvdDriveStatus(slot, drive);
                    if ((stat & IoCardDisk::WVD_STAT_DRIVE_OCCUPIED) != 0) {
                        const bool ok = IoCardDisk::wvdGetFilename(slot, drive, &filename);
                        assert(ok);
                    }
                }
                host::configWriteStr(subgroup.str(), item.str(), filename);
            } // drive
        } // if (isDiskController)
    } // slot
}


// remount all disks
static void
restoreDiskMounts()
{
    // look for disk controllers and populate drives
    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (isDiskController(slot)) {
            const auto cfg = current_cfg->getCardConfig(slot);
            const auto dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg.get());
            assert(dcfg);
            const int num_drives = dcfg->getNumDrives();
            for (int drive=0; drive<num_drives; drive++) {
                std::ostringstream subgroup;
                subgroup << "io/slot-" << slot;
                std::ostringstream item;
                item << "filename-" << drive;
                std::string filename;
                bool b = host::configReadStr(subgroup.str(), item.str(), &filename);
                if (b && !filename.empty()) {
                    IoCardDisk::wvdInsertDisk(slot, drive, filename);
                }
            } // for (drive)
        } // if (isDiskController)
    } // for (slot)
}


// break down any resources currently committed
static void
breakDownCards() noexcept
{
    // destroy card instances
    for (auto &card : card_in_slot) {
        card = nullptr;
    }

    // clean up mappings
    for (auto &mapentry : ioMap) {
        mapentry.slot   = -1;      // unoccupied
        mapentry.ignore = false;   // restore bad I/O warning flags
    }

    if (cpu) {
        cpu->setDevRdy(false);  // nobody is driving, so it floats to 0
    }

    curIoAddr = -1;
}

// ------------------------------------------------------------------------
// "public" members
// ------------------------------------------------------------------------

// build the world
void
system2200::initialize()
{
    // set up IO management
    for (auto &mapentry : ioMap) {
        mapentry.slot   = -1;    // unoccupied
        mapentry.ignore = false;
    }
    for (auto &card : card_in_slot) {
        card = nullptr;
    }
    curIoAddr = -1;

    // CPU speed regulation
    first_slice = true;
    sim_time_ns = adjust_sim_time = host::getTimeMs();
    sim_seconds = 0;

    realtime_start = 0;  // wall time of when sim started
    real_seconds  = 0;   // real time elapsed

    m_do_reconfig = false;
    freezeEmu(false);
    setTerminationState(RUNNING);

    scheduler = std::make_shared<Scheduler>();

    // attempt to load configuration from saved state
    SysCfgState ini_cfg;
    ini_cfg.loadIni();
    if (!ini_cfg.configOk(false)) {
        UI_warn(".ini file wasn't usable -- using a default configuration");
        ini_cfg.setDefaults();
    }
    setConfig(ini_cfg);

#if 0
    // intentional error, to see if the leak checker finds it
    int *leaker = new int[100];
    leaker[1] = 123;
#endif
}


// because everything is static, the destructor does nothing, so we
// need this function to know when the real Armageddon has arrived.
void
system2200::cleanup()
{
    saveDiskMounts();
    breakDownCards();

    cpu       = nullptr;
    scheduler = nullptr;

    current_cfg->saveIni();  // save state to ini file
    current_cfg = nullptr;
}


// called from CrtFrame to signal we want to end things.
// as such, at this point nothing has been shut down.
// after all windows have been shut down, we receive OnExit().
void
system2200::terminate() noexcept
{
    setTerminationState(TERMINATING);
}


// unregister a callback function which advances with the clock
void
system2200::registerClockedDevice(const clkCallback &cb)
{
    clocked_device_t cd = { cb, 0 };
    m_clocked_devices.push_back(cd);
}


// unregister a callback function which advances with the clock
void
system2200::unregisterClockedDevice(const clkCallback &/*cb*/) noexcept
{
#if 0
// FIXME: it is not possible to compare bound functions, so a different
// approach is needed
    for (auto it=begin(m_clocked_devices); it != end(m_clocked_devices); ++it) {
        if (it->callback_fn == cb) {
            m_clocked_devices.erase(it);
            break;
        }
    }
#else
    // if one board is unregistering, we are going to unregister everything.
    // big hammer, but it works.  FIXME find a nicer solution.
    m_clocked_devices.clear();
#endif
}


// build a system according to the spec.
// if a system already exists, tear it down and rebuild it.
void
system2200::setConfig(const SysCfgState &new_cfg)
{
    if (!current_cfg) {
        // first time we don't need to tear anything down
        current_cfg = std::make_shared<SysCfgState>();
    } else {
        // check if the change is minor, not requiring a teardown
        const bool rebuild_required = current_cfg->needsReboot(new_cfg);
        if (!rebuild_required) {
            *current_cfg = new_cfg;  // make new config permanent
            // notify all configured cards about possible new configuration
            for (int slot=0; slot < NUM_IOSLOTS; slot++) {
                if (current_cfg->isSlotOccupied(slot)) {
                    const IoCard::card_t ct = current_cfg->getSlotCardType(slot);
                    if (CardInfo::isCardConfigurable(ct)) {
                        auto cfg = current_cfg->getCardConfig(slot);
                        auto card = getInstFromSlot(slot);
                        card->setConfiguration(*cfg);
                    }
                }
            }
            return;
        }

        // the change was major, so delete existing resources
        cpu = nullptr;

        // remember which virtual disks are installed
        saveDiskMounts();

        // throw away all existing devices
        breakDownCards();
    }

    // save the new system configuration state
    *current_cfg = new_cfg;

    // (re)build the CPU
    const int ram_size = (current_cfg->getRamKB()) * 1024;
    int cpu_type = current_cfg->getCpuType();
    switch (cpu_type) {
        default:
            assert(false);
            cpu_type = Cpu2200::CPUTYPE_2200T;
            // fall through in non-debug build if config type is invalid
        case Cpu2200::CPUTYPE_2200B:
        case Cpu2200::CPUTYPE_2200T:
            cpu = std::make_shared<Cpu2200t>(scheduler, ram_size, cpu_type);
            break;
        case Cpu2200::CPUTYPE_VP:
        case Cpu2200::CPUTYPE_MVPC:
        case Cpu2200::CPUTYPE_MICROVP:
            cpu = std::make_shared<Cpu2200vp>(scheduler, ram_size, cpu_type);
            break;
    }
    assert(cpu);

    // build cards that go into each slot.
    // a hack -- when a display card is made, the crtframe status bar queries
    // to find out how many disk drives are associated with each drive.
    // if the display card is built before the disk controllers, the status
    // bar will be misconfigured.  the hack is to sweep the list twice,
    // skipping CRT's the first time, then doing only CRTs the second time.

    for (int pass=0; pass<2; pass++) {
    for (int slot=0; slot<NUM_IOSLOTS; slot++) {

        if (!current_cfg->isSlotOccupied(slot)) {
            continue;
        }

        const IoCard::card_t cardtype = current_cfg->getSlotCardType(slot);
        const int io_addr             = current_cfg->getSlotCardAddr(slot) & 0xFF;

        const bool display = (cardtype == IoCard::card_t::disp_64x16)
                          || (cardtype == IoCard::card_t::disp_80x24)
                          || (cardtype == IoCard::card_t::term_mux) ;

        if ((pass==0 && display) || (pass==1 && !display)) {
            continue;
        }

        auto inst = IoCard::makeCard(scheduler, cpu, cardtype, io_addr,
                                     slot, current_cfg->getCardConfig(slot).get());
        if (inst == nullptr) {
            // failed to install
            UI_warn("Configuration problem: failure to create slot %d card instance", slot);
        } else {
            std::vector<int> addresses = inst->getAddresses();
            for (auto &addr : addresses) {
                ioMap[addr].slot = slot;
            }
            card_in_slot[slot] = std::move(inst);
        }
    }}

    restoreDiskMounts();    // remount disks
}


// give access to components
const SysCfgState&
system2200::config() noexcept
{
    return *current_cfg;
}


// the user requests a change in configuration from the UiFrontPanel.
// however, doing so often requires a tear down and rebuild of all the
// components.  destroying the frontpanel instance and then returning
// to it is uncouth.  instead, when the user wants to reconfigure, this
// function is called and that desire is noted, and we return immediately.
// later on, in the onIdle code, this flag is checked and the reconfiguration
// happens then.
void
system2200::reconfigure() noexcept
{
    m_do_reconfig = true;
}


// reset the cpu
void
system2200::reset(bool cold_reset)
{
    curIoAddr = -1;

    cpu->reset(cold_reset);

    // reset all I/O devices
    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (current_cfg->isSlotOccupied(slot)) {
            card_in_slot[slot]->reset(cold_reset);
        }
    }
}


// turn cpu speed regulation on (true) or off (false)
void
system2200::regulateCpuSpeed(bool regulated) noexcept
{
    current_cfg->regulateCpuSpeed(regulated);

    // reset the performance monitor history
    perf_hist_len = 0;
    perf_hist_ptr = 0;
}


// indicate if the CPU is throttled or not
bool
system2200::isCpuSpeedRegulated() noexcept
{
    return current_cfg->isCpuSpeedRegulated();
}


void
system2200::setDiskRealtime(bool realtime) noexcept
{
    current_cfg->setDiskRealtime(realtime);
}


// indicate if the disk emulation speed is throttled or not
bool
system2200::isDiskRealtime() noexcept
{
    return current_cfg->getDiskRealtime();
}


// halt emulation
void
system2200::freezeEmu(bool freeze) noexcept
{
    m_freeze_emu = freeze;
}


// called whenever there is free time
bool
system2200::onIdle()
{
    if (m_do_reconfig) {
        m_do_reconfig = false;
        freezeEmu(true);
        UI_systemConfigDlg();
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
                host::sleep(10);
            } else {
                emulateTimeslice(slice_duration);
            }
            return true;        // want more idle events
        case TERMINATING:
            // we've been signaled to shut down the universe.
            // change the flag to know we've already cleaned up, in case
            // we receive another onIdle call.
            setTerminationState(TERMINATED);
            cleanup();
            break;
        case TERMINATED:
            // do nothing -- we already requested a cleanup
            break;
        default:
            assert(false);
            break;
    }

    return false;        // don't want any more idle events
}


// simulate a few ms worth of instructions
void
system2200::emulateTimeslice(int ts_ms)
{
    const int num_devices = m_clocked_devices.size();

    // try to stae reatime within this window
    const int64 adj_window = 10LL*ts_ms;  // look at the last 10 timeslices

    if (cpu->status() != Cpu2200::CPU_RUNNING) {
        return;
    }

    const uint64 now_ms = host::getTimeMs();

    if (first_slice) {
        first_slice = false;
        realtime_start = now_ms;
    }
    const int64 realtime_elapsed = now_ms - realtime_start;
    int64 offset = adjust_sim_time - realtime_elapsed;

    if (offset > adj_window) {
        // we're way ahead (probably because we are running unregulated)
        adjust_sim_time = realtime_elapsed + adj_window;
        offset = adj_window;
    } else if (offset < -adj_window) {
        // we've fallen way behind; catch up so we don't
        // run like mad after any substantial pause
        adjust_sim_time = realtime_elapsed - adj_window;
        offset = -adj_window;
    }

    if ((offset > 0) && isCpuSpeedRegulated()) {

        // we are running ahead of schedule; kill some time.
        // we don't kill the full amount because the sleep function is
        // allowed to, and very well might, sleep longer than we asked.
        const unsigned int ioffset = static_cast<unsigned int>(offset & 0xFFFLL);  // bottom 4 sec or so
        host::sleep(ioffset/2);

    } else {

        // keep track of when each slice started
        perf_real_ms[perf_hist_ptr++] = now_ms;
        if (perf_hist_ptr >= perf_hist_size) {
            perf_hist_ptr -= perf_hist_size;
        }
        if (perf_hist_len < perf_hist_size) {
            perf_hist_len++;
        }

        // simulate one timeslice's worth of instructions
        int slice_ns = ts_ms*1000000;
        if (num_devices == 1) {

            auto cb = m_clocked_devices[0].callback_fn;
            while (slice_ns > 0) {
                const int op_ns = cb();
                if (op_ns > 10000) {
                    slice_ns = 0; // finish the timeslice
                } else  {
                    // the guard is to skip this if the device signals error
                    slice_ns -= op_ns;
                    scheduler->timerTick(op_ns);
                }
            }

        } else if (num_devices == 2) {
            // this is an important case of #0 being cpu, and #1 being
            // the 8080 in the MXD.  the cpu is faster than the 8080,
            // so let it run a few cycles between reevaluations of which
            // device is ahead in time.

            // at the start of a timeslice, shift time for all cards towards
            // zero to prevent overflowing the 32b nanosecond counters
            const uint32 rebase = std::min(m_clocked_devices[0].ns,
                                           m_clocked_devices[1].ns);
            m_clocked_devices[0].ns -= rebase;
            m_clocked_devices[1].ns -= rebase;

            // we try to keep the devices in time lockstep as much as we can.
            // each device has a nanosecond counter. the list of devices is
            // kept in sorted order of increasing time. we call entry 0, adjust
            // its time, then move it to the right place in the list.
            while (slice_ns > 0) {
                const bool run_vp = (   m_clocked_devices[0].ns
                                     <= m_clocked_devices[1].ns);
                int op_ns_signed = 0;
                uint32 op_ns = 0;
                if (run_vp) {
                    // the 2200vp executes about six instructions in the time
                    // the 8080 does one typical instruction
                    auto cb = m_clocked_devices[0].callback_fn;
                    op_ns_signed  = cb();
                    op_ns_signed += cb();
                    op_ns_signed += cb();
                    op_ns_signed += cb();
                    op_ns_signed += cb();
                    op_ns_signed += cb();
                    op_ns = static_cast<uint32>(op_ns_signed);
                    m_clocked_devices[0].ns += op_ns;
                } else {
                    auto cb = m_clocked_devices[1].callback_fn;
                    op_ns_signed = cb();
                    op_ns = static_cast<uint32>(op_ns_signed);
                    m_clocked_devices[1].ns += op_ns;
                }

                if (op_ns > 50000) {
                    // something went wrong; finish the timeslice
                    slice_ns = 0;
                } else {
                    const uint32 clamp_ns =
                        std::max(m_clocked_devices[1].ns, m_clocked_devices[0].ns)
                      - std::min(m_clocked_devices[1].ns, m_clocked_devices[0].ns);
                    const uint32 delta_ns = std::min(op_ns, clamp_ns);
                    slice_ns -= delta_ns;
                    scheduler->timerTick(delta_ns);
                }
            }

        } else {

            // there are multiple devices

            // moving m_clocked_devices entries around it expensive,
            // so this array is used to maintain the order of the devices
            // where [0] is the one most behind in time.
            std::vector<int> order;
            for (int n=0; n<num_devices; n++) {
                order.push_back(n);
            }
            // bubble sort in time order
            for (int n=0; n<num_devices; n++) {
                for (int k=0; k<num_devices-1; k++) {
                    if (m_clocked_devices[order[k]].ns >
                        m_clocked_devices[order[k+1]].ns) {
                        std::swap(order[k], order[k+1]);
                    }
                }
            }
            // double check they are sorted in time order
            for (int k=0; k<num_devices-1; k++) {
                assert (m_clocked_devices[order[k]].ns <=
                        m_clocked_devices[order[k+1]].ns);
            }

            // at the start of a timeslice, shift time for all cards towards
            // zero to prevent overflowing the 32b nanosecond counters
            const uint32 rebase = m_clocked_devices[order[0]].ns;
            for (auto idx : order) {
                assert(m_clocked_devices[idx].ns >= rebase);
                m_clocked_devices[idx].ns -= rebase;
            }

            // we try to keep the devices in time lockstep as much as we can.
            // each device has a nanosecond counter. the list of devices is
            // kept in sorted order of increasing time. we call entry 0, adjust
            // its time, then move it to the right place in the list.
            while (slice_ns > 0) {
                auto cb = m_clocked_devices[order[0]].callback_fn;
                const int op_ns_signed = cb();
                const uint32 op_ns = static_cast<uint32>(op_ns_signed);
                if (op_ns > 50000) {
                    slice_ns = 0; // finish the timeslice
                } else {
                    // we can't advance world time by op_ns if the next most
                    // oldest device would conceptually start before time
                    // gets to where device[0] ended up after op_ns.
                    // TODO: investigate a more efficient way to do all
                    // of this vs min() and then later sorting the clocked devices.
                    // at the very least, special case having two devices,
                    // rather than all this generalized sorting for N devices.
                    const uint32 clamp_ns = m_clocked_devices[order[1]].ns
                                          - m_clocked_devices[order[0]].ns;
                    const uint32 delta_ns = std::min(op_ns, clamp_ns);
                    slice_ns -= delta_ns;
                    scheduler->timerTick(delta_ns);
                    const uint32 new_ns = (m_clocked_devices[order[0]].ns += op_ns);
                    auto entry0 = order[0];
                    int i;
                    for (i=0; i<num_devices-1; ++i) {
                        if (m_clocked_devices[order[i+1]].ns < new_ns) {
                            order[i] = order[i+1];
                        } else {
                            break;
                        }
                    }
                    order[i] = entry0;
                }
            }
        } // 3 or more clocked devices

        sim_time_ns     += ts_ms;
        adjust_sim_time += ts_ms;

        if (cpu->status() != Cpu2200::CPU_RUNNING) {
            UI_warn("CPU halted -- must reset");
            cpu->reset(true);  // hard reset
            return;
        }

        sim_seconds = static_cast<unsigned long>(
                            (sim_time_ns/1000) & 0xFFFFFFFF);

        const int real_seconds_now = static_cast<int>(realtime_elapsed/1000);
        if (real_seconds != real_seconds_now) {
            real_seconds = real_seconds_now;
            if (perf_hist_len > 10) {
                // compute running performance average over the
                // last real second or so
                const int n1 = (perf_hist_ptr - 1 + perf_hist_size)
                             % perf_hist_size;
                int64 ms_diff = 0;
                int slices = 0;
                for (int n=1; n<perf_hist_len; n+=10) {
                    const int n0 = (n1 - n + perf_hist_size) % perf_hist_size;
                    slices = n;
                    ms_diff = (perf_real_ms[n1] - perf_real_ms[n0]);
                    if (ms_diff > 1000) {
                        break;
                    }
                }
                const float relative_speed = static_cast<float>(slices*ts_ms)
                                           / static_cast<float>(ms_diff);

                // update the status bar with simulated seconds and performance
                UI_setSimSeconds(sim_seconds, relative_speed);
            }
        }

        // at least yield so we don't hog the whole machine
        host::sleep(0);
    }
}

// ========================================================================
//    io dispatch functions (used by core sim)
// ========================================================================

// address byte strobe
void
system2200::dispatchAbsStrobe(uint8 byte)
{
    // done if reselecting same device
    if (byte == curIoAddr) {
        return;
    }

    // deselect old card
    if ((curIoAddr > 0) && (ioMap[curIoAddr].slot >= 0)) {
        (card_in_slot[ioMap[curIoAddr].slot])->deselect();
    }
    curIoAddr = byte;

    const int cpu_type = cpu->getCpuType();
    const bool vp_mode = (cpu_type != Cpu2200::CPUTYPE_2200B)
                      && (cpu_type != Cpu2200::CPUTYPE_2200T);

    // by default, assume the device is not ready.
    // the addressed card will turn it back below if appropriate
    if (curIoAddr == 0x00 && vp_mode) {
        // the (M)VP CPU special cases address 00 and forces ready true
        cpu->setDevRdy(true);
        return;
    }
    // nobody is driving, so it defaults to 0
    cpu->setDevRdy(false);

    // let the selected card know it has been chosen
    if (ioMap[curIoAddr].slot >= 0) {
        const int slot = ioMap[curIoAddr].slot;
        card_in_slot[slot]->select();
        return;
    }

    // MVP OS probes addr 80 to test for the bank select register (BSR).
    // for non-VSLI CPUs, it would be annoying to get warned about it.
    if (vp_mode && (curIoAddr == 0x80)) {
        return;
    }

    // warn the user that a non-existant device has been selected
    if (!ioMap[curIoAddr].ignore && current_cfg->getWarnIo()
        && (curIoAddr != 0x00)  // intentionally select nothing
        && (curIoAddr != 0x46)  // testing for mxd at 0x4n
        && (curIoAddr != 0x86)  // testing for mxd at 0x8n
        && (curIoAddr != 0xC6)  // testing for mxd at 0xCn
       ) {
        const bool response = UI_confirm(
                    "Warning: selected non-existent I/O device %02X\n"
                    "Should I warn you of further accesses to this device?",
                    curIoAddr);
        // suppress further warnings
        ioMap[curIoAddr].ignore = !response;
    }
}


// output byte strobe
void
system2200::dispatchObsStrobe(uint8 byte)
{
// p 6-2 of the Wang 2200 Service Manual:
// When the controller is selected (select latch set), the Ready/Busy
// Decoder will indicate Ready (active low) to the CPU only if the peripheral
// device is not doing an operation.  The Ready indicator will stay active
// until the peripheral device being used generates a Busy indicator,
// allowing the CPU to do another I/O operation.  Normally, the device
// being used will generate a Busy indicator after the I/O Bus (!OB1 - !OB8)
// has been strobed by !OBS, the CPU output strobe.
    if (curIoAddr > 0) {
        if (ioMap[curIoAddr].slot >= 0) {
            card_in_slot[ioMap[curIoAddr].slot]->strobeOBS(byte);
        }
    }
}


// handles CBS strobes
void
system2200::dispatchCbsStrobe(uint8 byte)
{
    // each card handles CBS in its own way.
    //   * many cards simply ignore it
    //   * some use it like another OBS strobe to capture some type
    //     of command word
    //   * some cards use it to trigger an IBS strobe
    if ((curIoAddr > 0) && (ioMap[curIoAddr].slot >= 0)) {
        card_in_slot[ioMap[curIoAddr].slot]->strobeCBS(byte);
    }
}


// notify selected card when CPB changes
void
system2200::dispatchCpuBusy(bool busy)
{
    if ((curIoAddr > 0) && (ioMap[curIoAddr].slot >= 0)) {
        // signal that we want to get something
        card_in_slot[ioMap[curIoAddr].slot]->setCpuBusy(busy);
    }
}


// the CPU can poll IB5 without any other strobe.  return that bit.
int
system2200::cpuPollIB()
{
    if  ((curIoAddr > 0) && (ioMap[curIoAddr].slot >= 0)) {
        // signal that we want to get something
        return card_in_slot[ioMap[curIoAddr].slot]->getIB();
    }
    return 0;
}


// ========================================================================
// keyboard input routing
// ========================================================================

// register a handler for a key event to a given keyboard terminal
void
system2200::registerKb(int io_addr, int term_num, const kbCallback &cb)
{
    // check that it isn't already registered
    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr && term_num == kb.term_num) {
            UI_warn("Attempt to register kb handler at io_addr=0x%02x, term_num=%d twice",
                    io_addr, term_num);
            return;
        }
    }
    kb_route_t kb = { io_addr, term_num, cb, nullptr };
    keyboard_routes.push_back(kb);
}


void
system2200::unregisterKb(int io_addr, int term_num)
{
    for (auto it = begin(keyboard_routes); it != end(keyboard_routes); ++it) {
        if (io_addr == it->io_addr && term_num == it->term_num) {
            keyboard_routes.erase(it);
            return;
        }
    }
    UI_warn("Attempt to unregister unknown kb handler at io_addr=0x%02x, term_num=%d",
            io_addr, term_num);
}


// send a key event to the specified keyboard/terminal
void
system2200::dispatchKeystroke(int io_addr, int term_num, int keyvalue)
{
    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr && term_num == kb.term_num) {
            if (kb.script_handle) {
                // a script is running; ignore everything but HALT
                // (on pc, ctrl-C or pause/break key)
                if (keyvalue == IoCardKeyboard::KEYCODE_HALT) {
                    kb.script_handle = nullptr;
                }
                return;
            }
            auto cb = kb.callback_fn;
            cb(keyvalue);
            return;
        }
    }
    UI_warn("Attempt to route key to unknown kb handler at io_addr=0x%02x, term_num=%d",
            io_addr, term_num);
}


// request the contents of a file to be fed in as a keyboard stream
void
system2200::invokeKbScript(int io_addr, int term_num,
                           const std::string &filename)
{
    if (isScriptModeActive(io_addr, term_num)) {
        UI_warn("Attempt to invoke a script while one already active, io_addr=0x%02x, term_num=%d",
                io_addr, term_num);
        return;
    }

    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr && term_num == kb.term_num) {
            const int flags = ScriptFile::SCRIPT_META_INC
                            | ScriptFile::SCRIPT_META_HEX
                            | ScriptFile::SCRIPT_META_KEY ;
            kb.script_handle = std::make_shared<ScriptFile>(
                                   filename, flags, 3 /*max nesting*/
                               );
            if (!kb.script_handle->openedOk()) {
                kb.script_handle = nullptr;
            } else {
                // possibly get the first character
                pollScriptInput(io_addr, term_num);
            }
            return;
        }
    }
    UI_warn("Attempt to invoke script on unknown kb handler at io_addr=0x%02x, term_num=%d",
            io_addr, term_num);
}


// indicates if a script is currently active on a given terminal
bool
system2200::isScriptModeActive(int io_addr, int term_num)
{
    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr && term_num == kb.term_num) {
            return (kb.script_handle != nullptr);
        }
    }
    UI_warn("Attempt to query script status on unknown kb handler at io_addr=0x%02x, term_num=%d",
            io_addr, term_num);
    return false;
}


// return how many terminals at this io_addr have active scripts
int
system2200::numActiveScripts(int io_addr) noexcept
{
    int count = 0;
    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr) {
            count++;
        }
    }
    return count;
}


// when invoked on a terminal in script mode, causes key callback to be
// invoked with the next character from the script.  it returns true if
// a script supplied a character.
bool
system2200::pollScriptInput(int io_addr, int term_num)
{
    for (auto &kb : keyboard_routes) {
        if (io_addr == kb.io_addr && term_num == kb.term_num) {
            if (!kb.script_handle) {
                return false;
            }
            int ch;
            if (kb.script_handle->getNextByte(&ch)) {
                // yes, a key is available
                auto cb = kb.callback_fn;
                cb(ch);
                return true;
            }
            // EOF
            kb.script_handle = nullptr;
            return false;
        }
    }
    return false;
}

// ========================================================================
// external interface to the slot manager
// ========================================================================

// returns false if the slot is empty, otherwise true.
// returns card type index and io address via pointers.
bool
system2200::getSlotInfo(int slot, int *cardtype_idx, int *addr) noexcept
{
    assert(0 <= slot && slot < NUM_IOSLOTS);
    if (!current_cfg->isSlotOccupied(slot)) {
        return false;
    }

    if (cardtype_idx != nullptr) {
        *cardtype_idx = static_cast<int>(current_cfg->getSlotCardType(slot));
    }

    if (addr != nullptr) {
        *addr = current_cfg->getSlotCardAddr(slot);
    }

    return true;
}


// returns the IO address of the n-th keyboard (0-based).
// if (n >= # of keyboards), returns -1.
int
system2200::getKbIoAddr(int n) noexcept
{
    int num = 0;

    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (current_cfg->getSlotCardType(slot) == IoCard::card_t::keyboard) {
            if (num == n) {
                return current_cfg->getSlotCardAddr(slot);
            }
            num++;
        }
    }

    return -1; // failed to find it
}


// returns the IO address of the n-th printer (0-based).
// if (n >= # of printers), returns -1.
int
system2200::getPrinterIoAddr(int n) noexcept
{
    int num = 0;

    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        if (current_cfg->getSlotCardType(slot) == IoCard::card_t::printer) {
            if (num == n) {
                return current_cfg->getSlotCardAddr(slot);
            }
            num++;
        }
    }

    return -1;  // failed to find it
}


// return the instance handle of the device at the specified IO address.
// consumers of this function don't own the card, so a raw pointer is used.
IoCard*
system2200::getInstFromIoAddr(int io_addr) noexcept
{
    assert((io_addr >= 0) && (io_addr <= 0xFFF));
    return card_in_slot[ioMap[io_addr & 0xFF].slot].get();
}


// given a slot, return the "this" element
// consumers of this function don't own the card, so a raw pointer is used.
IoCard*
system2200::getInstFromSlot(int slot) noexcept
{
    assert(slot >=0 && slot < NUM_IOSLOTS);
    return card_in_slot[slot].get();
}


// find slot number of disk controller #n.
// returns true if successful.
bool
system2200::findDiskController(const int n, int *slot) noexcept
{
    const int num_ioslots = NUM_IOSLOTS;
    int numfound = 0;
    assert(slot != nullptr);

    assert(n >= 0);

    for (int probe=0; probe<num_ioslots; probe++) {
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
// note: slot, drive, io_addr are optionally nullptr.
bool
system2200::findDisk(const std::string &filename,
                     int *slot, int *drive, int *io_addr)
{
    for (int controller=0; ; controller++) {

        int slt = 0;
        if (!findDiskController(controller, &slt)) {
            break;
        }

        const auto cfg = current_cfg->getCardConfig(slt);
        const auto dcfg = dynamic_cast<const DiskCtrlCfgState*>(cfg.get());
        assert(dcfg);
        const int num_drives = dcfg->getNumDrives();
        for (int d=0; d<num_drives; d++) {
            const int stat = IoCardDisk::wvdDriveStatus(slt, d);
            if ((stat & IoCardDisk::WVD_STAT_DRIVE_OCCUPIED) != 0) {
                std::string fname;
                bool ok = IoCardDisk::wvdGetFilename(slt, d, &fname);
                assert(ok);
                if (filename == fname) {
                    if (slot != nullptr) {
                        *slot = slt;
                    }
                    if (drive != nullptr) {
                        *drive = d;
                    }
                    if (io_addr != nullptr) {
                        ok = getSlotInfo(slt, nullptr, io_addr);
                        assert(ok);
                    }
                    return true;
                }
            }
        } // for (d)
    } // for (controller)

    return false;
}

// ------------------------------------------------------------------------
// legal cpu system configurations
// ------------------------------------------------------------------------

const std::vector<system2200::cpuconfig_t> system2200::m_cpu_configs = {
    // https://wang2200.org/docs/datasheet/2200ABC_CPU_DataSheet.700-3491.11-74.pdf
    {   Cpu2200::CPUTYPE_2200B,    // .cpu_type
        "2200B",                   // .label
        { 4, 8, 12, 16, 24, 32 },  // .ram_size_options (20K and 28K were conceptually options too)
        { 0 },                     // .ucode_size_options
        false                      // .has_oneshot
    },

    // https://wang2200.org/docs/datasheet/2200T_CPU_DataSheet.700-3723D.1-79.pdf
    {   Cpu2200::CPUTYPE_2200T,    // .cpu_type
        "2200T",                   // .label
        { 8, 16, 24, 32 },         // .ram_size_options
        { 0 },                     // .ucode_size_options
        false                      // .has_oneshot
    },

    // https://wang2200.org/docs/datasheet/2200VP_CPU_DataSheet.700-4051C.10-80.pdf
    {   Cpu2200::CPUTYPE_VP,       // .cpu_type
        "2200VP",                  // .label
        { 16, 32, 48, 64 },        // .ram_size_options
        { 32 },                    // .ucode_size_options
        false                      // .has_oneshot
    },

#if 0 // remove it to reduce cognitive overload
    // https://wang2200.org/docs/datasheet/2200MVP_DataSheet.700-4656G.10-82.pdf
    {   Cpu2200::CPUTYPE_MVP,      // .cpu_type
        "2200MVP",                 // .label
        { 32, 64, 128, 256 },      // .ram_size_options
        { 32 },                    // .ucode_size_options
        true                       // .has_oneshot
    },
#endif

    // https://wang2200.org/docs/datasheet/MVP_LVP_SystemsOptionC.700-6832.4-81.pdf
    // https://wang2200.org/docs/system/2200SystemsOptionC.729-1062.pdf
    {   Cpu2200::CPUTYPE_MVPC,     // .cpu_type
        "2200MVP-C",               // .label
        { 32, 64, 128, 256, 512 }, // .ram_size_options
        { 64 },                    // .ucode_size_options
        true                       // .has_oneshot
    },

    // https://wang2200.org/docs/system/2200MicroVP_MaintenanceManual.741-1668.5-88.pdf
    {   Cpu2200::CPUTYPE_MICROVP,  // .cpu_type
        "MicroVP",                 // .label
        { 128, 512, 1024, 2048, 4096, 8192 },  // .ram_size_options
        { 64 },                    // .ucode_size_options
        true                       // .has_oneshot
    },
};


const system2200::cpuconfig_t*
system2200::getCpuConfig(const std::string &config_name) noexcept
{
    for (auto const &cfg : system2200::m_cpu_configs) {
        if (cfg.label == config_name) {
            return &cfg;
        }
    }
    return nullptr;
}


const system2200::cpuconfig_t*
system2200::getCpuConfig(int config_id) noexcept
{
    for (auto const &cfg : system2200::m_cpu_configs) {
        if (cfg.cpu_type == config_id) {
            return &cfg;
        }
    }
    return nullptr;
}

// vim: ts=8:et:sw=4:smarttab
