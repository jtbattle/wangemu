// This code emulates a disk interface and a disk controller.
//
// Looking at the 6541-1 disk interface, the addressing is unusual.
// Address bus bits 1,2,3,4,5,6, and 8 must match whatever the dip
// switches request, but bit 7 is hardwired to match 1 (!ab7 must be 0).
// Ah, there are two decodes.  The normal busy/read status is conditioned
// by ABS qualified by matching bits 1,2,3,4,5,6,8, but bit 7 isn't part
// of that logic.  The fully qualified one, including bit 7, drives a
// flop to the disk controller called !DHS.  The partially qualified
// one conditions GKBD and GISO going to the disk controller and RB going
// back to the CPU.  If we have partial qualification and CPB goes low,
// then GKBD is asserted to the disk.  If we don't have partial qualification
// and OBS goes low, then GISO goes low to the disk.
//
// Realtime emulation notes:
// ---------------------------
// Looking at the microcode for the minidisk controller, the behavior
// appears to be as follows:
//     When an operation (read/write/verify) starts on a drive, it
//     has a counter set to 0, and the counter for the other drive
//     gets incremented.  If the count gets to 32, then that other
//     drive is turned off.  After each operation, the ucode checks
//     for a control strobe for "10 seconds" (although the code looks
//     like it will wait for 8*256*40ms, or 8*10.2 sec, or 82 sec.
//     (I should just try it on mine and see what happens.)  Anyway,
//     if that counter times out, both drives are turned off.
//
//
// SYSTEM 2200 DISK MEMORY REFERENCE MANUAL (700-3159G)
// ----------------------------------------------------
// This manual has some great information on pages 3 to 13 concerning the
// sector interleave, RPM, and some track encoding information for various
// model floppy/hard drives.
//
//  2230 information:
//      rotates at 1500 RPM
//      24 sectors per track
//      interleave factor=6 (1/4 revolution):
//          logical:  0, 4, 8, 12, 16, 20,   1, 5, 9, 13, 17, 21, ...
//          physical: 0, 1, 2,  3,  4,  5,   6, 7, 8,  9, 10, 11, ...
//          logical:  ...  2,  6, 10, 14, 18, 22,    3,  7, 11, 15, 29, 23
//          physical: ... 12, 13, 14, 15, 16, 17,   18, 19, 20, 21, 22, 23
//      physical(logical) = (logical / 4) + 6*(logical % 6)
//      even tracks are on the lower surface of a platter;
//       odd tracks are on the upper surface of a platter
//
//  2260 information:
//      rotates at 2400 RPM
//      24 sectors per track
//      interleave factor=12
//          logical:  0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22,  ...
//          physical: 0, 1, 2, 3, 4,  5,  6,  7,  8,  9, 10, 11,  ...
//          logical:  ...  1,  3,  5,  7,  9, 11, 13, 15, 17, 19, 21, 23
//          physical: ... 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23
//      physical(logical) = (logical / 2) + 12*(logical % 2)
//      even tracks are on the lower surface of a platter;
//       odd tracks are on the upper surface of a platter
//
//  2270 information:
//      64 tracks of 16 sectors, 256 bytes per sector
//      sector layout:
//          2 bytes sector address
//          256 data bytes
//          2 byte crc
//      interleave factor=4 (1/4 revolution):
//          logical:  0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14,  3,  7, 11, 15
//          physical: 0, 1, 2,  3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 15
//      physical(logical) = (logical / 4) + 4*(logical % 4)
//
//  2270A information:
//      77 tracks of 16 sectors, 256 bytes per sector
//      the format is otherwise identical with 2270
//      I think the 2270A is also generalized so that soft sector
//      disks can be read and written for exchange with other systems,
//          (the 3740 IBM disk exchange software package)
//
// 2275 Disk Drive User Manual (700-8673)
// --------------------------------------
// C1: 32 sectors/track
//     305 tracks/side (one track unavailable for user data)
//     2 dual sided disks
//     10MB capacity
//
// 2280 Disk Drive User Manual (700-5216A)
// ---------------------------------------
// p2: One DPU controls up to two 2280(N)'s
//     One 2280 contains up to four platters; the top one is removable
//     Rotates at 3600 RPM
// p3: The fixed platters have up to five data surfaces and one servo surface
//     2280-1: 1 removable, 1 fixed
//     2280-2: 1 removable, 3 fixed
//     2280-3: 1 removable, 5 fixed
// p6: 64 sectors/track
//     outermost track is track 0, innermost is track 822
// p7: track 822 is not user accessible; it contains spare sectors
// p8: disk is address as T/D<u><p>; <u>=disk unit, <p>=platter
//     eg, T/D10=removable, T/D11=first fixed, T/D12 is second fixed, etc
//     also, T/B10 is alias for removable, T/310 alias for first fixed
// p19: table of sectors/surface, total sectors, bytes/surface, total bytes
//      legal sector range: 0 to 52607, per platter
//      single step track-to-track time:             6    ms
//      average track-to-track time:                30    ms
//      maximum track-to-track time:                55    ms
//      average rotational latency:                  8.33 ms
//      average sequential read  time, per sector:   4.6  ms
//      average sequential write time, per sector:   3.6  ms
//      average random read/write time, per sector: 42.0  ms

#include "DiskCtrlCfgState.h"
#include "host.h"              // for dbglog()
#include "IoCardDisk.h"
#include "Ui.h"                // for UI_warn()
#include "Cpu2200.h"
#include "Scheduler.h"
#include "system2200.h"
#include "SysCfgState.h"
#include "Wvd.h"


#ifdef _DEBUG
    int iodisk_noisy=1;
    #define NOISY  (iodisk_noisy) // turn on some alert messages
    int iodisk_dbg=0;
    #define DBG    (iodisk_dbg)   // turn on some debug logging
#else
    #define NOISY  (0)          // turn on some alert messages
    #define DBG    (0)          // turn on some debug logging
#endif
#ifdef _MSC_VER
    #pragma warning( disable: 4127 )  // conditional expression is constant
#endif

#define ASSERT_VALID_SLOT(s)  assert((s) >= 0 && (s) < NUM_IOSLOTS)
#define ASSERT_VALID_DRIVE(d) assert((d) >= 0 && (d) < 4)

// the minimum number of ticks for a callback event
const int64 DISK_MIN_TICKS = TIMER_US(20);

// =====================================================
//   public interface
// =====================================================

// instance constructor
IoCardDisk::IoCardDisk(std::shared_ptr<Scheduler> scheduler,
                       std::shared_ptr<Cpu2200>   cpu,
                       int base_addr, int card_slot, const CardCfgState *cfg) :
    m_scheduler(scheduler),
    m_cpu(cpu),
    m_base_addr(base_addr),
    m_slot(card_slot),
    m_selected(false),
    m_cpb(true),
    m_card_busy(false),
    m_compare_err(false),
    m_acting_intelligent(false),
    m_tmr_motor_off(nullptr),
    m_copy_pending(false)
{
    // only init if not doing a property probe
    if (m_slot >= 0) {
        assert(cfg != nullptr);
        const DiskCtrlCfgState *cp = dynamic_cast<const DiskCtrlCfgState*>(cfg);
        assert(cp != nullptr);
        m_cfg = *cp;
        createDiskController();
    }
}


// instance destructor
IoCardDisk::~IoCardDisk()
{
    // "temp" cards aren't fully initialized
    if (m_slot >= 0) {
        reset();
        for (int drive=0; drive<numDrives(); drive++) {
            m_d[drive].wvd = nullptr;
        }
    }
}


const std::string
IoCardDisk::getDescription() const
{
    return "Disk Controller";
}


const std::string
IoCardDisk::getName() const
{
    return "6541";
}


// return a list of the various base addresses a card can map to.
// list of common I/O addresses for this device taken from p. 2-5 of
// 2200 service manual.  the default comes first.
std::vector<int>
IoCardDisk::getBaseAddresses() const
{
    std::vector<int> v { 0x310, 0x320, 0x330 };
    return v;
}


// return the list of addresses that this specific card responds to.
// Disk controllers (at least some of them) ignore bits A8 and A7
// for purposes of address decoding, and use those two bits to signify
// something else:
//    A8=1: hog the controller         (eg, /390)
//    A8=0: controller isn't hogged    (eg, /310)
//    A7=1: address secondary drive    (eg, /350)
//    A7=0: address primary drive      (eg, /310)
// These two bits are orthogonal and may be asserted, or not, in any
// combination.
std::vector<int>
IoCardDisk::getAddresses() const
{
    std::vector<int> v;
    v.push_back(m_base_addr + 0x00);  // primary drive
    v.push_back(m_base_addr + 0x40);  // secondary drive
    v.push_back(m_base_addr + 0x80);  // hogged primary drive
    v.push_back(m_base_addr + 0xC0);  // hogged secondary drive
    return v;
}

// -----------------------------------------------------
// configuration management
// -----------------------------------------------------

// subclass returns its own type of configuration object
std::shared_ptr<CardCfgState>
IoCardDisk::getCfgState()
{
    return std::make_unique<DiskCtrlCfgState>();
}


// modify the existing configuration state
void
IoCardDisk::setConfiguration(const CardCfgState &cfg) noexcept
{
    const DiskCtrlCfgState &ccfg(dynamic_cast<const DiskCtrlCfgState&>(cfg));

    // FIXME: do sanity checking to make sure things don't change at a bad time?
    //        perhaps queue this change until the next WAKEUP phase?
    m_cfg = ccfg;
};


// -----------------------------------------------------
// operational
// -----------------------------------------------------

void
IoCardDisk::reset(bool /*hard_reset*/)
{
    // reset controller state
    m_selected  = false;
    m_cpb       = true;
    m_drive     = 0;
    m_card_busy = false;

    m_acting_intelligent = false;

    // reset drive state
    m_tmr_motor_off = nullptr;

    for (int drive=0; drive<numDrives(); drive++) {
        stopMotor(drive);
    }

    advanceState(EVENT_RESET);
    m_host_type = -1;
}


void
IoCardDisk::select()
{
    // we save the exact address in case of partial decode
    const int abs_value = m_cpu->getAB();
    m_primary = (abs_value & 0x40) != 0x40;  // primary drive
    m_selected = true;

    if (DBG > 1) {
        dbglog("disk ABS (addr=0x%02x)\n", abs_value);
    }

    m_cpu->setDevRdy(!m_card_busy);
    for (int drive=0; drive<numDrives(); drive++) {
        UI_diskEvent(m_slot, drive);
    }
}


void
IoCardDisk::deselect()
{
    if (DBG > 1) {
        dbglog("disk -ABS\n");
    }

    m_cpu->setDevRdy(false);
    m_selected = false;
    m_cpb      = true;

    for (int drive=0; drive<numDrives(); drive++) {
        UI_diskEvent(m_slot, drive);
    }
}


void
IoCardDisk::strobeOBS(int val)
{
    const int val8 = val & 0xFF;

    if (DBG > 2) {
        dbglog("disk OBS(AB=0x%02x): byte=0x%02x\n", m_cpu->getAB(), val8);
    }

    advanceState(EVENT_OBS, val8);
    m_cpu->setDevRdy(!m_card_busy);
}


void
IoCardDisk::strobeCBS(int /*val*/) noexcept
{
//  int val8 = val & 0xFF;

    // unexpected -- the real hardware ignores this byte
    if (NOISY > 0) {
        // FIXME: MVP spews these two a lot. what do they mean?
        // UI_warn("unexpected disk CBS: Output of byte 0x%02x", val8);
    }

    // TODO:
#if 0
    // later disk controllers allowed controlling disk hog mode via
    // sending a CBS with data bit OB8 set.  For example see the 6543
    // schematic, which has logic for both the A8 addressing bit hog
    // selection and the CBS hog selection method.  The controller is
    // hogged if either mode is hogged (ie, they are OR'd together).
    //
    // the emulator just ignores this mode as it has no effect as there
    // is no other system competing for the disk.
    hogged = !!(val8 & 0x80);
#endif

    // FIXME:
#if 0
    // according to Paul Szudzik, CBS with the ls data bit high is
    // hardwired to cause a hard reset of the disk controller.
    // I don't see that hardware in the early floppy disk controller
    // design, but they could have added it later.
    if (val8 & 1) {
        reset(true);
    }
#endif
}


// change of CPU Busy state
void
IoCardDisk::setCpuBusy(bool busy)
{
    // it appears that except for reset, ucode only ever clears it,
    // and of course the IBS sets it back.
    if (DBG > 2) {
        dbglog("disk CPB%c\n", busy?'+':'-');
    }

    m_cpb = busy;
    checkDiskReady();
}

// ==========================================================
// IO card interface
// ==========================================================

// stop the motor on the specified drive of the specified controller
void
IoCardDisk::stopMotor(int drive)
{
    ASSERT_VALID_DRIVE(drive);

    if (m_d[drive].state != DRIVE_EMPTY) {
        m_d[drive].state = DRIVE_IDLE;
    }
    m_d[drive].sector     = 0;    // which sector is being read
    m_d[drive].idle_cnt   = 0;    // number of operations done w/o this drive
    m_d[drive].secwait    = -1;
    m_d[drive].tmr_track  = nullptr;
    m_d[drive].tmr_sector = nullptr;

    UI_diskEvent(m_slot, drive);        // let UI know things have changed
}


// create a disk controller & associated drives
void
IoCardDisk::createDiskController()
{
    m_tmr_motor_off = nullptr;

    for (int drive=0; drive<4; drive++) {
        m_d[drive].wvd = (drive < numDrives()) ? std::make_unique<Wvd>()
                                               : nullptr;
        m_d[drive].state = DRIVE_EMPTY;
        // timing emulation:
        m_d[drive].track      = 0;    // which track head is on
        m_d[drive].tmr_track  = nullptr;
        m_d[drive].tmr_sector = nullptr;
    }

    reset();
}


// this is called either when cpb changes state or card_busy changes state.
// if cpu is not busy it means it is waiting on I/O, and if we aren't busy,
// maybe the data is ready now.  if so, check with the state machine.
void
IoCardDisk::checkDiskReady()
{
    if (m_selected) {
        if (!m_cpb && !m_card_busy) {
            const bool data_ready = advanceState(EVENT_IBS_POLL);
            if (data_ready) {
                if (DBG > 2) {
                    dbglog("disk IBS of 0x%02x\n", m_byte_to_send);
                }
                m_cpu->ioCardCbIbs(m_byte_to_send);
            }
        }
        m_cpu->setDevRdy(!m_card_busy);
    }
}


// ==========================================================
// core emulation routines
// ==========================================================

// timer constants
const int64 ONE_SECOND  = TIMER_MS( 1000.0);
const int64 TEN_SECONDS = TIMER_MS(10000.0);

// update the card's busy/idle state and notify the CPU
void
IoCardDisk::setBusyState(bool busy)
{
    if ((DBG> 1) && (m_card_busy != busy)) {
        dbglog("disk setBusyState(%s)\n", busy ? "true" : "false");
    }
    m_card_busy = busy;
    if (m_selected) {
        m_cpu->setDevRdy(!m_card_busy);
    }
}


// true=same timing as real disk, false=going fast
bool
IoCardDisk::realtimeDisk() noexcept
{
    return system2200::config().getDiskRealtime();
}


// retrigger the motor turn off timer, if appropriate.
// since the emulator allows a given controller to control both a floppy
// disk (which spins up and down) and a hard disk (which doesn't spin down)
// at the same time, we semi-arbitrarily say that the motor off timer
// gets reset only for accesses to floppy drives.
void
IoCardDisk::wvdTickleMotorOffTimer()
{
    assert(m_drive >= 0 && m_drive < numDrives());

    const int disktype = m_d[m_drive].wvd->getDiskType();
    if ((disktype == Wvd::DISKTYPE_FD5) || (disktype == Wvd::DISKTYPE_FD8)) {
        m_tmr_motor_off = m_scheduler->createTimer(TEN_SECONDS,
                                                   [&](){ tcbMotorOff(m_drive); });
    }
}


// return the number of ns to get from the current disk position to the
// new disk position.  the position isn't actually updated.
int64
IoCardDisk::wvdGetNsToTrack(int track)
{
    assert(m_drive >= 0 && m_drive < numDrives());

    const int track_diff = std::abs(track - m_d[m_drive].track);

    int64 time_ns = 0;
    switch (m_d[m_drive].wvd->getDiskType()) {

        // assume a fixed stepping rate per track
        case Wvd::DISKTYPE_FD5:
        case Wvd::DISKTYPE_FD8:
            time_ns = m_d[m_drive].ns_per_track * track_diff;
            break;

        // The 2280 literature says that it takes 6 ms to do a single track
        // step, 30 ms to step half way (408 tracks) and 55 ms to sweep all
        // the way across the disk (816 tracks).  The half way and full sweep
        // timings are consistent with this linear fit:
        //     t(# tracks) = 6 ms + 0.06 ms * (# tracks)
        //
        // The 2260 literature doesn't give timing, but it is probably
        // similar to, but a little worse than, the 2280 timing.
        case Wvd::DISKTYPE_HD60:
        case Wvd::DISKTYPE_HD80:
            if (track_diff == 0) {
                time_ns = 0LL;
            } else {
                time_ns = TIMER_MS(6.0 + 0.06*track_diff);
            }
            break;

        default:
            assert(false);
            break;
    }

    return time_ns;
}


// time out how long it takes to get to the track implied by the sector
// address of the current command
void
IoCardDisk::wvdStepToTrack()
{
    assert(m_drive >= 0 && m_drive < numDrives());

    const bool empty = (m_d[m_drive].state == DRIVE_EMPTY);

    assert(m_secaddr < WVD_MAX_SECTORS);
    assert(empty || (m_secaddr < m_d[m_drive].wvd->getNumSectors()));

    const int sec_per_trk = m_d[m_drive].sectors_per_track;
    const int to_track = m_secaddr / sec_per_trk;

    const int64 ns = (empty) ? 0 : wvdGetNsToTrack(to_track);

    m_d[m_drive].track = to_track;  // update head position
    wvdSeekTrack(ns);
}


// this utility is passed the timing, in ticks, that a disk operation
// should take.  this value may be increased by the disk start up time,
// if the selected disk isn't already spinning.  it also takes care of
// some other bookkeeping.
//
// (1) time out the motor for the "other" drive, if appropriate
//
// (2) if the current drive isn't spinning, add a disk start-up time,
//     and start up the sector counter callback timer
//
// (3) set up the callback timer if we are doing realtime, otherwise just
//     fire off the timer event
//
void
IoCardDisk::wvdSeekTrack(int64 nominal_ns)
{
    assert(m_drive >= 0 && m_drive < numDrives());

    const int other_drive = m_drive^1;  // the drive not being accessed
    const bool empty = (m_d[m_drive].state == DRIVE_EMPTY);

    // this shouldn't already be in use
    assert(m_d[m_drive].tmr_track == nullptr);

    // the disk controller counts how many times a command has been
    // issued without accessing a given drive.  if that count exceeds 32,
    // then it turns off the inactive drive.  the ucode comments claim
    // an 8 second timeout, but it appears to actually be just based on
    // operation count.
    if (other_drive < numDrives()) {
        const int disktype = m_d[other_drive].wvd->getDiskType();
        const bool floppy = (disktype == Wvd::DISKTYPE_FD5) ||
                            (disktype == Wvd::DISKTYPE_FD8);
        if ((m_d[other_drive].state == DRIVE_SPINNING) && floppy) {
            if (++m_d[other_drive].idle_cnt == 32) {
                stopMotor(other_drive);
            }
        }
    }

    // figure out if the disk needs to spin up
    const int disktype = m_d[m_drive].wvd->getDiskType();
    const bool hard_disk = (disktype == Wvd::DISKTYPE_HD60) ||
                           (disktype == Wvd::DISKTYPE_HD80);

    int64 ns = nominal_ns;
    if (!hard_disk) { // hard disks are always running
        switch (m_d[m_drive].state) {
            case DRIVE_EMPTY:
            case DRIVE_IDLE:
                assert(m_d[m_drive].tmr_sector == nullptr);
                ns += ONE_SECOND;
                break;
            case DRIVE_SPINNING:
                break;
            default:
                assert(false);
        }
    }

    // start sector timer
    if (!empty && (m_d[m_drive].tmr_sector == nullptr)) {
        m_d[m_drive].tmr_sector = m_scheduler->createTimer(
                                    m_d[m_drive].ns_per_sector,
                                    [&](){ tcbSector(m_drive); });
    }

    if (ns <= 0) {
        ns = DISK_MIN_TICKS;
    }
    if (!realtimeDisk()) {
        ns = DISK_MIN_TICKS;
    }

    m_d[m_drive].tmr_track =
        m_scheduler->createTimer(ns, [&](){ tcbTrack(m_drive); });
}


// this assumes (enforces) that we are already on the right track,
// and then computes how long it will take to get to the right
// sector.  it does so by setting the secwait sector value and
// the tcbSector routine checks when that sector is reached.
void
IoCardDisk::wvdSeekSector() noexcept
{
    assert(m_drive >= 0 && m_drive < numDrives());

    const int sec_per_trk = m_d[m_drive].sectors_per_track;
    const int interleave  = m_d[m_drive].interleave;

    const int track       = m_secaddr / sec_per_trk;
    const int logical_sec = m_secaddr % sec_per_trk;

    // how many "groups" of sectors per track.  for instance, if a track has
    // has 24 sectors and an interleave of 4, there are six groups of four.
    const int groups = sec_per_trk / interleave;
    // if the interleave isn't an integral factor of the number of sectors
    // per track, that is, the sector count and interleave factor are
    // relatively prime, then a different logical->physical equation applies.
    assert(groups * interleave == sec_per_trk);

    const int phys_sec = (logical_sec / groups)
                       + (logical_sec % groups) * interleave;

    // make sure this was already taken care of before we got called
    assert(track == m_d[m_drive].track);

    m_d[m_drive].secwait = phys_sec;
}


// ==========================================================
//   timer callback functions
// ==========================================================

// this routine is called after the track seek operation is over
void
IoCardDisk::tcbTrack(int arg)
{
    const int drive = arg;
    assert(drive >= 0 && drive < numDrives());
    assert(m_card_busy);

    const bool empty = (m_d[m_drive].state == DRIVE_EMPTY);

    if (DBG > 1) {
        dbglog("TRACK SEEK timer fired\n");
    }

    assert(m_d[m_drive].tmr_track != nullptr);
    m_d[m_drive].tmr_track = nullptr;

    if (m_d[m_drive].state == DRIVE_IDLE) {
        m_d[m_drive].state = DRIVE_SPINNING;
    }

    if ((m_command == CMD_SPECIAL) ||
            // special commands do track-at-a-time processing,
            // so there is no need to call SeekSector()
        empty ||
            // when an access is made to an empty drive, the motor spins up,
            // as the way it senses the lack of disk is not seeing index holes
            // once this is determined, status is returned
        (m_command == CMD_WRITE) ||
            // the write command seeks to the given track, then sends status,
            // then receives data from the host, then does the sector seek
        !realtimeDisk()
            // there is no desire to model sector level timing
       ) {
        advanceState(EVENT_DISK);
    } else {
        // read or compare operation -- remain busy until sector is read
        wvdSeekSector();
    }
}


// this routine is activated after the disk controller has not had
// any commands for 10 seconds.  turn off the motors on both drives.
void
IoCardDisk::tcbMotorOff(int /*arg*/)
{
    assert(m_tmr_motor_off != nullptr);
    m_tmr_motor_off = nullptr;

    if (DBG > 1) {
        dbglog("MOTOR OFF timer fired\n");
    }

    for (int drive=0; drive<numDrives(); drive++) {
        stopMotor(drive);
    }
}


// this routine gets called after every sector.
// it must check to if notification is pending for that sector.
void
IoCardDisk::tcbSector(int arg)
{
    int drive = arg;

    assert(drive >= 0 && drive < numDrives());
    assert(m_d[drive].tmr_sector != nullptr);

    if (false && (NOISY > 2)) {
        dbglog("Drive %d SECTOR timer fired: sector %d\n", drive, m_d[drive].sector);
    }

    // retrigger the timer
    m_d[drive].tmr_sector = m_scheduler->createTimer(
                                    m_d[drive].ns_per_sector,
                                    [&, drive](){ tcbSector(drive); });
    assert(m_d[drive].tmr_sector != nullptr);

    // advance to next sector, mod sectors per track
    const int prev_sec = m_d[drive].sector;
    m_d[drive].sector++;
    if (m_d[drive].sector >= m_d[drive].sectors_per_track) {
        m_d[drive].sector = 0;
    }

    // check if an operation was pending on having passed the sector
    if (m_d[drive].secwait == prev_sec) {
        m_d[drive].secwait = -1; // clear status
        advanceState(EVENT_DISK);
    }
}


// ==========================================================
//   exported functions
// ==========================================================

// return properties of the named disk.
// parameters that are nullptr won't be returned.
// returns true if nothing went wrong.
bool
IoCardDisk::wvdGetWriteProtect(const std::string &filename, bool *write_protect)
{
    assert(write_protect != nullptr);
    Wvd dsk;
    if (!dsk.open(filename)) {
        return false;
    }

    *write_protect = dsk.getWriteProtect();
    dsk.close();
    return true;
}


// returns true and type of disk in given (slot,drive) if occupied,
// otherwise it returns false.
bool
IoCardDisk::wvdGetDiskType(const int slot, const int drive, int *disktype)
{
    assert(disktype != nullptr);
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);

    const IoCardDisk *tthis =
        dynamic_cast<IoCardDisk*>(system2200::getInstFromSlot(slot));
    assert(tthis != nullptr);

    if (tthis->m_d[drive].state == DRIVE_EMPTY) {
        return false;
    }

    *disktype = tthis->m_d[drive].wvd->getDiskType();
    return true;
}


// returns true and name of disk in given (slot,drive) if occupied,
// otherwise it returns false.
bool
IoCardDisk::wvdGetFilename(const int slot,
                           const int drive,
                           std::string *filename)
{
    assert(filename != nullptr);
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);

    const IoCardDisk *tthis =
        dynamic_cast<IoCardDisk*>(system2200::getInstFromSlot(slot));
    assert(tthis != nullptr);

    if (tthis->m_d[drive].state == DRIVE_EMPTY) {
        return false;
    }

    *filename = tthis->m_d[drive].wvd->getPath();
    return true;
}


// given a slot and a drive number, return drive status
// returns a bitwise 'or' of the WVD_STAT_DRIVE_* enums
int
IoCardDisk::wvdDriveStatus(int slot, int drive) noexcept
{
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);
    const IoCardDisk *tthis =
        dynamic_cast<IoCardDisk*>(system2200::getInstFromSlot(slot));

    if (tthis == nullptr) {
        // can happen at init time -- this routine is called when the
        // CRT is init'd before the disk controllers
        return 0;       // !EXISTENT, !OCCUPIED, !RUNNING, !SELECTED
    }

    int rv = 0; // default return value

    if (drive < tthis->numDrives()) {
        rv |= WVD_STAT_DRIVE_EXISTENT;
    }

    if ((rv != 0) && tthis->m_d[drive].state != DRIVE_EMPTY) {
        rv |= WVD_STAT_DRIVE_OCCUPIED;
    }

    if ((rv != 0) && tthis->m_selected && (tthis->m_drive == drive)) {
        rv |= WVD_STAT_DRIVE_SELECTED;
    }

    if ((rv != 0) && !tthis->inIdleState()) {
        rv |= WVD_STAT_DRIVE_BUSY;
    }

    if ((rv != 0) && tthis->m_d[drive].state != DRIVE_IDLE) {
        rv |= WVD_STAT_DRIVE_RUNNING;
    }

    return rv;
}


// returns false if something went wrong, true otherwise
bool
IoCardDisk::wvdInsertDisk(int slot,
                          int drive,
                          const std::string &filename)
{
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);
    IoCardDisk *tthis =
        dynamic_cast<IoCardDisk*>(system2200::getInstFromSlot(slot));
    assert(tthis != nullptr);

    const bool ok = tthis->iwvdInsertDisk(drive, filename);
    UI_diskEvent(slot, drive);
    return ok;
}


// remove the disk from the specified drive
// returns true if removed, or false if canceled.
bool
IoCardDisk::wvdRemoveDisk(int slot, int drive)
{
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);
    IoCardDisk *tthis =
        dynamic_cast<IoCardDisk*>(system2200::getInstFromSlot(slot));
    assert(tthis != nullptr);

    const bool ok = tthis->iwvdRemoveDisk(drive);
    UI_diskEvent(slot, drive);
    return ok;
}


// close the filehandle associated with the specified drive
void
IoCardDisk::wvdFlush(int slot, int drive)
{
    ASSERT_VALID_SLOT(slot);
    ASSERT_VALID_DRIVE(drive);

    const IoCardDisk *tthis = dynamic_cast<IoCardDisk*>
                                    (system2200::getInstFromSlot(slot));
    assert(tthis != nullptr);

    tthis->m_d[drive].wvd->flush();
}


// format a disk by filename
// returns true if successful
bool
IoCardDisk::wvdFormatFile(const std::string &filename)
{
    Wvd dsk;
    bool ok = dsk.open(filename);
    if (!ok) {
        return false;
    }

    const int num_platters = dsk.getNumPlatters();
    for (int p=0; ok && p<num_platters; p++) {
        ok = dsk.format(p);
    }

    return ok;
}


// ==========================================================
// private member functions
// ==========================================================

// return true if the selected disk is idle.
// if the disk is busy, ask the user to confirm the action,
// and return true if OK, or false if cancel.
bool
IoCardDisk::iwvdIsDiskIdle(int drive) const
{
    assert(drive >= 0 && drive < numDrives());
    assert(m_d[drive].state != DRIVE_EMPTY);

    if (!m_selected || (m_drive != drive) || inIdleState()) {
        return true;
    }

    return UI_confirm("This disk is in the middle of an operation.\n"
                      "Are you sure you want to do that?");
}


// returns false if something went wrong, true otherwise
bool
IoCardDisk::iwvdInsertDisk(int drive,
                           const std::string &filename)
{
    assert(drive >= 0 && drive < numDrives());
    assert(m_d[drive].state == DRIVE_EMPTY);

    char disk_loc[10];
    const bool drive_r =  (drive & 1) != 0;
    const int addr_off = ((drive & 2) != 0) ? 0x40 : 0x00;
    sprintf(&disk_loc[0], "%c/3%02X",
                (drive_r ? 'R' : 'F'), m_base_addr + addr_off);
    const bool ok = m_d[drive].wvd->open(filename);
    if (!ok) {
        return false;
    }

    // we check a few issues here.
    //
    //   1) the first generation machines couldn't deal with platters with
    //      more than 32768 sectors, nor disks with more than one platter.
    //      warn if the user puts in a large disk in a system that can't deal.
    //
    //   2) the 2200A/B/C/S/T only knows about the small disk sizes.
    //      for unknown reasons, when a file is created on a disk in the R
    //      drive in such a system, the unused msb of the sector address is
    //      stored as '1'.  This causes no problems as this bit is ignored
    //      later when it is read back.  The problem is that intelligent
    //      disk controllers don't mask off the bit, and thus illegal sector
    //      addresses will result.
    //
    //      when we have an intelligent disk controller and a small disk,
    //      check if the disk has any msb sector addresses set, and if so,
    //      warn the user and offer to clean these bits.
    const int  max_sectors = 32768;
    const int  num_sectors  = m_d[drive].wvd->getNumSectors();
    const int  num_platters = m_d[drive].wvd->getNumPlatters();
    const bool large_disk   = (num_sectors > max_sectors) || (num_platters > 1);
    const bool first_gen    = (system2200::config().getCpuType() == Cpu2200::CPUTYPE_2200B)
                           || (system2200::config().getCpuType() == Cpu2200::CPUTYPE_2200T);
    const bool dumb_ctrl    = (intelligence() == DiskCtrlCfgState::DISK_CTRL_DUMB);
    const bool warn         = warnMismatch();

    if (warn && first_gen && large_disk) {
        UI_warn("The disk in drive %s has %d sectors and %d platters.\n\n"
                 "The 2200A/B/C/S/T can't access any sector number greater than %d,\n"
                 "nor anything other than the first platter.\n\n"
                 "Proceed with caution.",
                 &disk_loc[0], num_sectors, num_platters, max_sectors);
    }

    if (warn && !first_gen && dumb_ctrl && large_disk) {
        UI_warn("The disk in drive %s has %d sectors and %d platters.\n\n"
                 "Dumb disk controllers can't access any sector number greater than %d,\n"
                 "nor anything other than the first platter.\n\n"
                 "You might want to reconfigure the disk controller to be intelligent.\n"
                 "Proceed with caution.",
                 &disk_loc[0], num_sectors, num_platters, max_sectors);
    }

    if (warn && !first_gen && !dumb_ctrl && !large_disk) {
        const bool bit_15 = diskHasBit15Problem(m_d[drive].wvd.get(), false);
        if (bit_15) {
            const bool do_it = UI_confirm(
                "This disk in drive %s has extraneous bits set on some sector\n"
                "addresses which might confuse an intelligent disk controller.\n\n"
                "Either switch the disk controller configuration to be dumb,\n"
                "or click \"Yes\" below to automatically clear these bits.",
                &disk_loc[0]);
            if (do_it) {
                diskHasBit15Problem(m_d[drive].wvd.get(), true);
            }
        }
    }

    m_d[drive].state = DRIVE_IDLE;
    m_d[drive].tmr_track  = nullptr;
    m_d[drive].tmr_sector = nullptr;
    m_d[drive].secwait    = -1;
    m_d[drive].idle_cnt   = 0;

    // cache disk timing properties
    int track_seek_ms, disk_rpm;
    getDiskGeometry(m_d[drive].wvd->getDiskType(),
                    &m_d[drive].sectors_per_track,
                    &track_seek_ms,
                    &disk_rpm,
                    &m_d[drive].interleave);

    m_d[drive].tracks_per_platter = static_cast<int>(
             (num_sectors + m_d[drive].sectors_per_track - 1) /
                            m_d[drive].sectors_per_track      );
    m_d[drive].ns_per_track  = TIMER_MS(track_seek_ms);
    m_d[drive].ns_per_sector =
                TIMER_MS(  60000.0 // ms per minute
                         / (double)((int64)disk_rpm * m_d[drive].sectors_per_track));

    return true;
}


// remove the disk from the specified drive.
// return true on success; return false if the drive is busy doing something.
bool
IoCardDisk::iwvdRemoveDisk(int drive)
{
    assert(drive >= 0 && drive < numDrives());
    assert(m_d[drive].state != DRIVE_EMPTY);

    if (iwvdIsDiskIdle(drive)) {
        m_d[drive].wvd->close();
        m_d[drive].state      = DRIVE_EMPTY;
        m_d[drive].secwait    = -1;
        m_d[drive].tmr_track  = nullptr;
        m_d[drive].tmr_sector = nullptr;
        return true;
    }

    return false;
}


// flush out completed sector to the virtual disk imag
// return true on success
bool
IoCardDisk::iwvdWriteSector()
{
    assert(m_drive >= 0 && m_drive < numDrives());
    assert(m_platter < m_d[m_drive].wvd->getNumPlatters());
    assert(m_secaddr < m_d[m_drive].wvd->getNumSectors());

    if (DBG > 0) {
        dbglog(">> writing virtual sector %d, platter %d, drive %d <<\n", m_secaddr, m_platter, m_drive);
    }
    return m_d[m_drive].wvd->writeSector(m_platter, m_secaddr, &m_buffer[0]);
}


// read a sector from the virtual disk image
// return true on success
bool
IoCardDisk::iwvdReadSector()
{
    assert(m_drive >= 0 && m_drive < numDrives());
    assert(m_platter < m_d[m_drive].wvd->getNumPlatters());
    assert(m_secaddr < m_d[m_drive].wvd->getNumSectors());

    if (DBG > 0) {
        dbglog(">> reading virtual sector %d, platter %d, drive %d <<\n", m_secaddr, m_platter, m_drive);
    }
    const bool ok = m_d[m_drive].wvd->readSector(m_platter, m_secaddr, &m_buffer[0]);

    // compute LRC
    int cksum = 0;
    for (int i=0; i<256; i++) {
        cksum += m_buffer[i];
    }
    m_buffer[256] = static_cast<uint8>(cksum & 0xFF);  // LRC byte

    return ok;
}


// get disk drive geometry from the disk type
// return params are allowed to be nullptr
void
IoCardDisk::getDiskGeometry(int disktype,
                            int *sectorsPerTrack,
                            int *trackSeekMs,
                            int *diskRpm,
                            int *interleave
                           ) noexcept
{
    int sectors_per_track=0, track_seek_ms=0, disk_rpm=0, inter=0;

    switch (disktype) {

        default:
            assert(false);
            // fall through in release mode ...
        case Wvd::DISKTYPE_FD5:
            sectors_per_track = 10;
            // this interleave factor is a guess; only 2 and 5 make sense.
            // booting BASIC-2 ver 2.3 off an emulated 5.25" floppy takes
            // 16 seconds with interleave=2, and 30 seconds with interleave=5.
            // the difference is much less dramatic with more random accesses,
            // but sequential accesses are very common: booting, loading, and
            // saving programs.
            inter             = 2;
            track_seek_ms     = 40;
            disk_rpm          = 300;
            break;

        case Wvd::DISKTYPE_FD8:
            // info from: SYSTEM 2200 DISK MEMORY REFERENCE MANUAL (700-3159G)
            // 2270 has 1024 sectors, so at 16 sectors/track, it has 64 tracks
            sectors_per_track = 16;
            inter             = 4;
            track_seek_ms     = 40;
            disk_rpm          = 360;
            break;

        case Wvd::DISKTYPE_HD60:
            // info from: SYSTEM 2200 DISK MEMORY REFERENCE MANUAL (700-3159G)
            // 2260 has 816 tracks at 24 sectors/track, or 19584 sectors.
            sectors_per_track = 24;
            inter             = 12;
            // track seek time is probably similar to 2280's
            track_seek_ms     = 4;
            disk_rpm          = 2400;
            break;

        case Wvd::DISKTYPE_HD80:
            // info from: 2280 Disk Drive User Manual (700-5216A)
            // 2280 has 822 tracks at 64 sectors/track, or 52608 sectors.
            sectors_per_track = 64;
            inter             = 32;  // this is a guess
            // track seek time is really 6 + 0.06*(# tracks)
            track_seek_ms     = 4;
            disk_rpm          = 3600;
            break;
    }

    // return what the caller asked for
    if (sectorsPerTrack != nullptr) {
        *sectorsPerTrack = sectors_per_track;
    }
    if (trackSeekMs != nullptr) {
        *trackSeekMs = track_seek_ms;
    }
    if (diskRpm != nullptr) {
        *diskRpm = disk_rpm;
    }
    if (interleave != nullptr) {
        *interleave = inter;
    }
}


// =========================================================================
// detect if a disk with <= 32K sectors has any sector addresses with bit 15
// set.  optionally clear them.
//
// We must check in a few different places.  The first sector of the disk
// contains information about the catalog, including the number of sectors
// set aside for the catalog (the SCRATCH DISK END=nnnn parameter).
// Each entry in the index also contains two sector addresses: the start
// and end sector address set aside for the file.
//
// We must be careful though -- disks aren't required to have a catalog,
// and may be a pure data disk.  If so, the data might just happen to look
// like a valid catalog'd disk.  To prevent false positives, we do a bit
// of sanity checking to make sure it really looks like a valid catalog.

// return true if the specified platter 'p' appears to have a valid catalog
bool
platterHasValidCatalog(Wvd *wvd, int p)
{
    assert(wvd != nullptr);
    uint8 sector_buff[257]; // 256B of data plus an LRC byte

    // get sector 0 of the platter;
    // the first 16 bytes contains info about the index/catalog structure
    bool ok = wvd->readSector(p, 0, &sector_buff[0]);
    if (!ok) {
        return false;
    }

    // a catalog disk has a byte 0 of 0x00, 0x01, or 0x02
    // 0x02 means the disk is using long (24b) sector addresses,
    // which I've never personally seen and I presume never suffers
    // from this problem, so we can opt out quickly.
    if (sector_buff[0] > 0x01) {
        return false;
    }

    // how many sectors are set aside for the catalog?
    const int index_sectors = sector_buff[1];

    // first as-yet unallocated sector in the catalog area
    const int first_unused_sector = (256*sector_buff[2] + sector_buff[3]) & 0x7fff;

    // start of non-catalog sectors (SCRATCH DISK END=nnnn parameter)
    const int end_sector          = (256*sector_buff[4] + sector_buff[5]) & 0x7fff;

    if (first_unused_sector > end_sector) {
        return false;  // nonsense
    }

    const int num_sectors = wvd->getNumSectors();   // sectors/platter
    if (first_unused_sector > num_sectors) {
        return false;  // nonsense
    }

    // sweep through the presumed index, and make sure the data looks
    // like a valid catalog index.  the first sector has 15 index entries,
    // and the others have 16.
    for (int idx=0; idx < index_sectors; idx++) {
        ok = wvd->readSector(p, idx, &sector_buff[0]);
        if (!ok) {
            return false;  // that isn't good!
        }
        bool unused_seen = false;
        const int first_idxoff = (idx == 0) ? 1 : 0;
        for (int idxoff = first_idxoff; idxoff < 16; idxoff++) {
            const uint8 *entry = &sector_buff[16*idxoff];
            // byte 0 of the index indicates if the file is unused (0x00),
            // valid (0x10), scratched (0x11), or reclaimed (0x21).
            // any other value is a problem.  also, once an unused entry is
            // seen, the remaining entries should also be unused.
            if (entry[0] == 0x00) {
                unused_seen = true;
                continue;
            }
            if (unused_seen) {
                return false;  // should only see unused entries at this point
            }

            if ((entry[0] != 0x10) && (entry[0] != 11) && (entry[0] != 0x21)) {
                return false;  // illegal state
            }

            const int file_first_sector = (256*entry[2] + entry[3]) & 0x7fff;
            const int file_last_sector  = (256*entry[4] + entry[5]) & 0x7fff;
            if (file_first_sector > file_last_sector) {
                return false;  // nonsense
            }
            if (file_last_sector >= num_sectors) {
                return false;  // nonsense
            }
            // TODO: one other check that could be done here is to read the
            //       final sector of the file, grab the count of used sectors,
            //       and make sure it is consistent with the file size.
        }
    }

    // if we got this far, the catalog looked credible
    return true;
}


// return true if bit 15 is ever set on the given platter 'p'.
// if 'fix_it' is true, the problem should be fixed in place.
bool
IoCardDisk::platterHasBit15Problem(Wvd *wvd, int p, bool fix_it)
{
    assert(wvd != nullptr);
    uint8 sector_buff[257]; // 256B of data plus an LRC byte

    // get sector 0 of the platter;
    // the first 16 bytes contains info about the index/catalog structure
    bool ok = wvd->readSector(p, 0, &sector_buff[0]);
    if (!ok) {
        return false;
    }

    // how many sectors are set aside for the catalog?
    const int index_sectors = sector_buff[1];

    // these might have bit 15 set, but even if it does, the remaining
    // 15 bits must be consistent
    bool bit_15 = false;
    if ((sector_buff[2] >= 0x80) || (sector_buff[4] >= 0x80)) {
        bit_15 = true;
        if (fix_it) {
            sector_buff[2] &= 0x7f;  // msb of first unused sector location
            sector_buff[4] &= 0x7f;  // msb of SCRATCH DISK END=nnnn parameter
            ok = wvd->writeSector(p, 0, &sector_buff[0]);
            if (!ok) {
                return true;
            }
        }
    }

    // sweep the index, checking for bit 15, and fixing any violations.
    // the first sector has 15 index entries, and the others have 16.
    for (int idx=0; idx < index_sectors; idx++) {

        ok = wvd->readSector(p, idx, &sector_buff[0]);
        if (!ok) {
            return false;  // that isn't good!
        }

        bool sector_modified = false;

        const int first_idxoff = (idx == 0) ? 1 : 0;
        for (int idxoff = first_idxoff; idxoff < 16; idxoff++) {
            uint8 *entry = &sector_buff[16*idxoff];
            // byte 0 of the index indicates if the file is unused (0x00),
            // valid (0x10), scratched (0x11), or reclaimed (0x21).
            // any other value is a problem.  also, once an unused entry is
            // seen, the remaining entries should also be unused.
            if (entry[0] == 0x00) {
                continue;
            }

            // if the first or last sector address >= 0x8000, the problem exists
            if ((entry[2] >= 0x80) || (entry[4] >= 0x80)) {
                sector_modified = true;
                bit_15 = true;
                entry[2] &= 0x7f;
                entry[4] &= 0x7f;
            }
        }  // idxoff

        if (sector_modified && fix_it) {
            ok = wvd->writeSector(p, idx, &sector_buff[0]);
            if (!ok) {
                return true;
            }
        }
    }

    // indicate if we saw any problems
    return bit_15;
}


// return true if bit 15 is ever set
bool
IoCardDisk::diskHasBit15Problem(Wvd *wvd, bool fix_it)
{
    assert(wvd != nullptr);
    const int num_platters = wvd->getNumPlatters();  // platters/disk
    const int num_sectors  = wvd->getNumSectors();   // sectors/platter

    // large disks don't have this problem
    if ((num_platters > 1) || (num_sectors > 32768)) {
        return false;
    }

    bool has_problem = false;

    for (int p=0; p<num_platters; p++) {
        const bool has_catalog = platterHasValidCatalog(wvd, p);
        if (has_catalog) {
            has_problem |= platterHasBit15Problem(wvd, p, fix_it);
        }
    }

    return has_problem;
}

// vim: ts=8:et:sw=4:smarttab
