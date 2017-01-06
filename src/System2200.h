// ======================================================================
// System2200 contains the various components of the system:
//    scheduler
//    current cpu
//    config state
//
// It is responsible for connecting the various pieces together --
// making sure the world gets built in proper order on start up, that
// configuration changes cause the tear down and rebuild of the world,
// things get torn down at the end of the world cleanly (special attention
// for cleaning up singletons).  This unit knows what kind of card is
// plugged into each slot and what card corresponds to which address.
// When a CPU wants to perform I/O to a given address, this module routes
// the requests to the right place.
// ======================================================================

#ifndef _INCLUDE_SYSTEM2200_H_
#define _INCLUDE_SYSTEM2200_H_

#include "w2200.h"

class Cpu2200;
class IoCard;
class SysCfgState;
class Scheduler;

// this is a singleton
class System2200
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(System2200);
     System2200();
    ~System2200();

    // because everything is static, we have to be told when
    // the sim is really starting and ending.
    void initialize();  // T -0
    void cleanup();     // Armageddon

    // give access to components
    const SysCfgState& config() { return *m_config; }

    // called whenever there is free time.  it returns true
    // if it wants to be called back later when idle again
    bool onIdle();

    // set current system configuration -- may cause reset
    void setConfig(const SysCfgState &newcfg);

    // reset the whole system
    void reset(bool cold_reset);

    // simulate a few ms worth of instructions
    void emulateTimeslice(int ts_ms);  // timeslice in ms

    void regulateCpuSpeed(bool regulated);
    bool isCpuSpeedRegulated() const;

    // halt emulation
    static void freezeEmu(bool freeze) { m_freeze_emu = freeze; }

    // shut down the application
    static void terminate();

    // indicate that user wants to reconfigure the system
    static void reconfigure();

    // ---- I/O dispatch logic ----

    void cpu_ABS(uint8 byte);   // address byte strobe
    void cpu_OBS(uint8 byte);   // output byte strobe
    void cpu_CBS();             // handles CBS strobes
    void cpu_CPB(bool busy);    // notify selected card when CPB changes
    int  cpu_poll_IB5() const;  // the CPU can poll IB5 without any other strobe

    // ---- slot manager ----

    // returns false if the slot is empty, otherwise true.
    // returns card type index and io address via pointers.
    bool getSlotInfo(int slot, int *cardtype_idx, int *addr);

    // returns the IO address of the n-th keyboard (0-based).
    // if (n >= # of keyboards), returns -1.
    int getKbIoAddr(int n);

    // returns the IO address of the n-th printer (0-based).
    // if (n >= # of printers), returns -1.
    int getPrinterIoAddr(int n);

    // return the instance handle of the device at the specified IO address
    // used by IoCardKeyboard.c
    IoCard* getInstFromIoAddr(int io_addr) const;

    // given a slot, return the "this" element
    IoCard* getInstFromSlot(int slot);

    // find slot number of disk controller #n (starting with 0).
    // returns true if successful.
    bool findDiskController(const int n, int *slot) const;

    // find slot,drive of any disk controller with disk matching name.
    // returns true if successful.
    bool findDisk(const string &filename, int *slot, int *drive, int *io_addr);

private:
    // returns true if the slot contains a disk controller, 0 otherwise
    bool isDiskController(int slot) const;

    // save/restore mounted disks to/from ini file
    void saveDiskMounts(void);
    void restoreDiskMounts(void);

    // break down any resources currently committed
    void breakdown_cards(void);

    // singleton members, and a flag to know when to know first time
    static bool                         m_initialized;
    static std::shared_ptr<SysCfgState> m_config;       // active system config
    static std::shared_ptr<Scheduler>   m_scheduler;    // for coordinating events in the emulator
    static std::shared_ptr<Cpu2200>     m_cpu;          // the central processing unit
    static bool                         m_freeze_emu;   // toggle to prevent time advancing
    static bool                         m_do_reconfig;  // deferred request to reconfigure

    // help ensure an orderly shutdown
    static enum term_state_t
        { RUNNING, TERMINATING, TERMINATED } m_term_state;
    static void setTerminationState(term_state_t newstate)
        { m_term_state = newstate; }
    static term_state_t getTerminationState()
        { return m_term_state; }

    // -------------- I/O dispatch --------------
    struct iomap_t {
        IoCard *inst;       // pointer to card instance
        bool    ignore;     // if false, access generates a warning message
                            // if there is no device at that address
    };
    static struct iomap_t m_IoMap[256];         // pointer to card responding to given address
    static IoCard* m_cardInSlot[NUM_IOSLOTS];   // pointer to card in a given slot
    static int     m_IoCurSelected;             // address of most recent ABS

    // stuff related to regulating simulation speed:
    static bool  m_regulated_speed; // 1=try to match real speed, 0=run full out
    static bool  m_firstslice;      // has m_realtimestart been initialized?
    static int64 m_realtimestart;   // wall time of when sim started
    static int   m_real_seconds;    // real time elapsed

    static unsigned long m_simsecs; // number of actual seconds simulated time elapsed

    // amount of actual simulated time elapsed, in ms
    static int64 m_simtime;

    // amount of adjusted simulated time elapsed, in ms because of user events
    // (eg, menu selection), sim time is often interrupted and we don't
    // actually desire to catch up.  m_simtime is the actual number of
    // simulated slices, while this var has been fudged to account for those
    // violations of realtime.
    static int64 m_adjsimtime;

    // keep a rolling average of how fast we are running in case it is reported.
    // we want to report the running average over the last second of realtime
    // to prevent the average from updating like crazy when running many times
    // realtime.
    static const int perf_hist_size = 100;      // # of timeslices to track
    static     int64 perf_real_ms[100];         // realtime at start of each slice
    static       int perf_hist_len;             // number of entries written
    static       int perf_hist_ptr;             // next entry to write
};


// logging utility function for debug purposes
// exported by System2200.cpp
void dbglog(const char *fmt, ...);

#endif // _INCLUDE_SYSTEM2200_H_

// vim: ts=8:et:sw=4:smarttab
