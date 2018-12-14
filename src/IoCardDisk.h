#ifndef _INCLUDE_IOCARD_DISK_H_
#define _INCLUDE_IOCARD_DISK_H_

#define WVD_MAX_SECTORS 65535   // largest disk platter, in sectors

#define WVD_MAX_LABEL_LEN (256-16-1) // don't forget the zero terminator

#include "IoCard.h"
#include "DiskCtrlCfgState.h"

class Cpu2200;
class Scheduler;
class Wvd;
class Timer;

class IoCardDisk : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardDisk);

    // ----- common IoCard functions -----
    IoCardDisk(std::shared_ptr<Scheduler> scheduler,
               std::shared_ptr<Cpu2200>   cpu,
               int baseaddr,        // eg 0x310, 0x320, 0x330
               int cardslot,        // which backplane slot this card is in
               const CardCfgState *cfg);
    ~IoCardDisk();

    std::vector<int> getAddresses() const override;

    void  setConfiguration(const CardCfgState &cfg) override;
    void  editConfiguration(CardCfgState *cfg) override;

    void  reset(bool hard_reset=true) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    void  CPB(bool busy) override;

    // ----- IoCardDisk specific functions -----

    // disk drive status
    enum { WVD_STAT_DRIVE_EXISTENT = 0x01,  // drive exists
           WVD_STAT_DRIVE_OCCUPIED = 0x02,  // there is a disk in there
           WVD_STAT_DRIVE_RUNNING  = 0x04,  // the motor is running
           WVD_STAT_DRIVE_SELECTED = 0x08,  // the drive is selected
           WVD_STAT_DRIVE_BUSY     = 0x10,  // the disk is in the middle of an operation
         };

    // return properties of the named disk.
    // parameters that are nullptr won't be returned.
    // returns true if nothing went wrong.
    static bool wvdGetWriteProtect(const std::string &filename,
                                   bool *write_protect);

    // returns true and type of disk in given (slot,drive) if occupied,
    // otherwise it returns false.
    static bool wvdGetDiskType(const int slot, const int drive, int *disktype);

    // returns true and name of disk in given (slot,drive) if occupied,
    // otherwise it returns false.
    static bool wvdGetFilename(const int slot,
                               const int drive,
                               std::string *filename);

    // given a slot and a drive number, return drive status
    // returns a bitwise 'or' of the WVD_STAT_DRIVE_* enums
    static int wvdDriveStatus(int slot, int drive);

    // returns false if something went wrong, true otherwise
    static bool wvdInsertDisk(int slot,
                              int drive,
                              const std::string &filename);

    // remove the disk from the specified drive
    // returns true if removed, or false if canceled.
    static bool wvdRemoveDisk(int slot,
                              int drive);

    // close the filehandle associated with the specified drive
    static void wvdFlush(int slot, int drive);

    // format a disk by filename
    // returns true if successful
    static bool wvdFormatFile(const std::string &filename);

    // get disk drive geometry from the disk type
    // return params are allowed to be nullptr
    static void getDiskGeometry(int disktype,
                                int *sectorsPerTrack,
                                int *trackSeekMs,
                                int *diskRpm,
                                int *interleave
                               );

private:
    // ---- card properties ----
    const std::string  getDescription() const override;
    const std::string  getName() const override;
    std::vector<int>   getBaseAddresses() const override;
    bool               isConfigurable() const override { return true; }
    std::shared_ptr<CardCfgState> getCfgState() override;

    // ---- disk access functions, tied to an object ----
    // "iwvd" == internal wang virtual disk function

    // return true if the selected disk is idle.
    // if the disk is busy, ask the user to confirm the action,
    // and return true if OK, or false if cancel.
    bool iwvdIsDiskIdle(int drive) const;

    // returns false if something went wrong, true otherwise
    bool iwvdInsertDisk(int drive,
                        const std::string &filename);

    // return false if action is carried out, else true
    bool iwvdRemoveDisk(int drive);

    // flush out completed sector to the virtual disk image
    // return true on success
    bool iwvdWriteSector();

    // read a sector from the virtual disk image
    // return true on success
    bool iwvdReadSector();

    // stop the motor on the specified drive of the specified controller
    void stopMotor(int drive);

    // create a disk controller & associated drives
    void createDiskController();

    void checkDiskReady();

    void wvdTickleMotorOffTimer();

    int64 wvdGetNsToTrack(int track);    // calc timing to step to target track
    void wvdSeekSector();                // do interleave computation
    void wvdStepToTrack();               // step to track specified by command
    void wvdSeekTrack(int64 nominal_ns); // machinery to actually wait for track

    // timer callback functions
    void tcbTrack(int arg);
    void tcbSector(int arg);
    void tcbMotorOff(int arg);

    // card busy state
    void setBusyState(bool busy);

    // return true if bit 15 is ever set on the given platter 'p'.
    // if 'fix_it' is true, the problem should be fixed in place.
    static bool platterHasBit15Problem(Wvd *wvd, int p, bool fix_it);

    // return true if bit 15 is ever set
    static bool diskHasBit15Problem(Wvd *wvd, bool fix_it);

    // ---- internal state ----

    // number of attached drives (1-4)
    int numDrives() const { return m_cfg.getNumDrives(); }
    // intelligence: dumb, intelligent, auto
    DiskCtrlCfgState::disk_ctrl_intelligence_t
        intelligence() { return m_cfg.getIntelligence(); }
    // issue warnings on media mismatch?
    bool warnMismatch() { return m_cfg.getWarnMismatch(); }

    DiskCtrlCfgState m_cfg;                          // current configuration
    std::shared_ptr<Scheduler> m_scheduler;          // system event scheduler
    std::shared_ptr<Cpu2200>   m_cpu;                // associated CPU
    const int                  m_baseaddr;           // the address the card is mapped to
    const int                  m_slot;               // which slot the card sits in
    bool                       m_selected;           // this card is being addressed
    bool                       m_cpb;                // cpb is asserted
    bool                       m_card_busy;          // the card isn't ready to accept a command or reply
    bool                       m_compare_err;        // compare status (true=miscompare)
    bool                       m_acting_intelligent; // what we told the host most recently
    std::shared_ptr<Timer>     m_tmr_motor_off;      // turn off both drives after a period of inactivity

    enum state_t { DRIVE_EMPTY, DRIVE_IDLE, DRIVE_SPINNING };
    struct drive_t {
        state_t              state;
        std::unique_ptr<Wvd> wvd;   // virtual disk object

        // used for emulating timing behavior
        int     tracks_per_platter; // disk property
        int     sectors_per_track;  // disk property
        int     interleave;         // disk property
        int64   ns_per_sector;      // derived: timer constant per sector
        int64   ns_per_track;       // derived: timer constant per track step

        int     track;              // track counter
        int     sector;             // physical sector counter
        int     secwait;            // waiting for this sector (<0: not waiting)

        int     idle_cnt;           // number of operations done w/o this drive

        std::shared_ptr<Timer> tmr_track;      // spin up + track seek timer
        std::shared_ptr<Timer> tmr_sector;     // sector timer
    };
    drive_t m_d[4];   // drives: two primary, two secondary

    // ---- emulation sequencing logic ----

    enum disk_sm_t {
        CTRL_WAKEUP,            // waiting to be contacted by host
        CTRL_STATUS1,           // responding with capabilities and readiness

        CTRL_GET_BYTES,         // subroutine to receive ...
        CTRL_GET_BYTES2,        // ... and echo N bytes

        CTRL_SEND_BYTES,        // subroutine to send N bytes

        CTRL_COMMAND,           // receiving command and sector bytes
        CTRL_COMMAND_ECHO,      // echoing command and sector bytes
        CTRL_COMMAND_ECHO_BAD,  // echoing bad command byte
        CTRL_COMMAND_STATUS,    // replying whether command is valid

        CTRL_READ1,             // swallowing byte from host - unknown purpose
        CTRL_READ2,             // sending status of sector read operation
        CTRL_READ3,             // sending sector data and LRC byte

        CTRL_WRITE1,            // receiving write data & LRC
        CTRL_WRITE2,            // waiting to send final status byte

        CTRL_VERIFY1,           // receiving data for comparison
        CTRL_VERIFY2,           // sending final status

        // special commands:
        CTRL_COPY1,
        CTRL_COPY2,
        CTRL_COPY3,
        CTRL_COPY4,
        CTRL_COPY5,
        CTRL_COPY6,
        CTRL_COPY7,
        CTRL_FORMAT1,
        CTRL_FORMAT2,
        CTRL_FORMAT3,
        CTRL_MSECT_WR_START,
        CTRL_MSECT_WR_END1,
        CTRL_MSECT_WR_END2,
        CTRL_VERIFY_RANGE1,
        CTRL_VERIFY_RANGE2,
        CTRL_VERIFY_RANGE3,
        CTRL_VERIFY_RANGE4,
        CTRL_VERIFY_RANGE5,
    };

    // disk channel commands
    enum disk_cmd_t {
        CMD_READ=0, CMD_WRITE=2, CMD_VERIFY=4,
        CMD_SPECIAL=1   // intelligent disk controllers only
    };

    // special commands, used only by intelligent disk controllers.
    // not all of these extended commands are supported by every controller,
    // and in fact not all of them are even understood!
    // *1 means it is described in the LvpDiskCommandSequences document
    // *2 means it is described in the Paul Szudzik SDS excerpt
    // *3 means it is appears in a $GIO statement in the program
    //    "DISKCM1" on the boot-2.3.wvd virtual disk image.
    //    Note that DISKCM1 was written 5/29/78, so it is relatively old,
    //    and subsequent controllers probably had different commands.
    //
    // I've attempted to trigger the VERIFY_SECTOR_RANGE command,
    // both with "SAVE ... $ ..." and the "VERIFY F/310,(0,1024)"
    // command bit it doesn't issue the command.
    enum disk_cmd_special_t {
        SPECIAL_COPY                     = 0x01,   // *1, *2, *3
        SPECIAL_FORMAT                   = 0x02,   // *1, *2, *3
        SPECIAL_FORMAT_SECTOR            = 0x03,   //         *3
        SPECIAL_READ_SECTOR_HEADER       = 0x04,   //         *3
        SPECIAL_CLEAR_ERROR_COUNT        = 0x08,   //         *3
        SPECIAL_READ_ERROR_COUNT         = 0x09,   //         *3
        // commands 0x0A through 0x0F              //         *3
        // via the "field service command" menu item.
        // it is not at all document what these do.
        SPECIAL_MULTI_SECTOR_WRITE_START = 0x10,   // *1, *2
        SPECIAL_MULTI_SECTOR_WRITE_END   = 0x11,   // *1, *2
        SPECIAL_VERIFY_SECTOR_RANGE      = 0x12,   // *1, *2
        SPECIAL_READ_SECTOR_AND_HANG     = 0x15,   //     *2
        SPECIAL_READ_STATUS              = 0x16,   // *1, *2
        SPECIAL_FORMAT_TRACK             = 0x18    // *1,     *3
    };

    std::string unsupportedExtendedCommandName(int cmd);

    // indicates reason why advanceState() is being called
    enum disk_event_t {
        EVENT_RESET,    // uh, reset
        EVENT_OBS,      // received OBS
        EVENT_IBS_POLL, // checking to see if return data is ready
        EVENT_DISK      // a disk operation completed; advance controller state
    };

    // true=same timing as real disk, false=going fast
    static bool realtime_disk();

    // return true if this was a sw reset command and set state appropriately,
    // otherwise return false
    bool cax_init();

    // indicate if the controller state machine is idle or busy
    bool inIdleState() const;

    // report if a given drive is occupied and has media that is suitable
    // for the intelligent disk protocol, namely disks with > 32K sectors,
    // or multiplatter disks.  these aren't necessarily opposite, as a
    // drive might be empty.
    bool driveIsSmart(int drive) const;
    bool driveIsDumb(int drive) const;

    // helper routine to set the conditions to receive and echo bytes from the host
    void getBytes(int count, disk_sm_t return_state);

    // helper routine to set the conditions to send bytes to the host
    void sendBytes(int count, disk_sm_t return_state);

    // for debugging
    std::string statename(int state) const;

    // centralized function to handle updating sequencing state
    bool advanceState(disk_event_t event, const int val=0);
    bool advanceStateInt(disk_event_t event, const int val);

    int        m_host_type;          // 00=2200 T or PROM mode, 01=2200 VP, 02=2200 MVP
    int        m_command;            // command byte
    int        m_special_command;    // special command byte
    int        m_primary;            // primary or secondary drive address
    int        m_drive;              // drive selection, extracted from command byte
    int        m_platter;            // platter address
    int        m_lastdrive;          // previously selected drive
    int        m_secaddr;            // sector address
    int        m_byte_to_send;        // the value that IBS returns

    uint8      m_buffer[257];        // 256B of data plus an LRC byte
    int        m_bufptr;             // which buffer entry is read or written next
    uint8      m_header[10];         // header bytes
    int        m_state_cnt;          // how many bytes of the have been processed
    int        m_xfer_length;        // number of bytes in this part of transaction

    // stuff for state machine subroutines
    disk_sm_t  m_state;               // the current controller state
    disk_sm_t  m_calling_state;       // who performed call
    disk_sm_t  m_return_state;        // where to go when subroutine is done
    int        m_byte_count;          // how many bytes to send/receive
    int        m_get_bytes[300];      // received bytes
    int        m_send_bytes[300];     // bytes to send
    int        m_get_bytes_ptr;       // get pointer
    int        m_send_bytes_ptr;      // put pointer

    // the special COPY command sets up the following state.
    // the next command is a normal READ in form, but the normal READ
    // behavior is taken over by the COPY semantics.
    bool       m_copy_pending;        // the state below is meaningful

    // the special COPY and VERIFY RANGE commands save into this state
    int        m_range_drive;         // chosen drive
    int        m_range_platter;       // chosen platter
    int        m_range_start;         // 24b sector address
    int        m_range_end;           // 24b sector address
    int        m_dest_drive;          // copy destination: chosen drive
    int        m_dest_platter;        // copy destination: chosen platter
    int        m_dest_start;          // copy destination: 24b sector address
};

#endif // _INCLUDE_IOCARD_DISK_H_

// vim: ts=8:et:sw=4:smarttab
