// ======================================================================
// system2200 contains the various components of the system:
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

class IoCard;
class SysCfgState;

using clkCallback = std::function<int()>;
using  kbCallback = std::function<void(int)>;

// fixed services related to the overall simulation
namespace system2200
{
    // because everything is static, we have to be told when
    // the sim is really starting and ending.
    void initialize();  // Time=0
    void cleanup();     // Armageddon

    // shut down the application
    void terminate() noexcept;

    // (un)register a callback function which advances with the clock
    void registerClockedDevice(const clkCallback &cb);
    void unregisterClockedDevice(const clkCallback &cb) noexcept;

    // set current system configuration -- may cause reset
    void setConfig(const SysCfgState &new_cfg);

    // give access to components
    const SysCfgState& config() noexcept;

    // indicate that user wants to reconfigure the system
    void reconfigure() noexcept;

    // reset the whole system
    void reset(bool cold_reset);

    // change/query the simulation speed
    void regulateCpuSpeed(bool regulated) noexcept;
    bool isCpuSpeedRegulated() noexcept;

    // change/query the simulation speed
    void setDiskRealtime(bool realtime) noexcept;
    bool isDiskRealtime() noexcept;

    // temporarily halt emulation
    void freezeEmu(bool freeze) noexcept;

    // called whenever there is free time.  it returns true
    // if it wants to be called back later when idle again
    bool onIdle();

    // simulate a few ms worth of instructions
    void emulateTimeslice(int ts_ms);  // timeslice in ms

    // ---- I/O dispatch logic ----

    void dispatchAbsStrobe(uint8 byte);  // address byte strobe
    void dispatchObsStrobe(uint8 byte);  // output byte strobe
    void dispatchCbsStrobe(uint8 byte);  // control byte strobes
    void dispatchCpuBusy(bool busy);     // notify selected card when CPB changes
    int  cpuPollIB();                    // the CPU can poll IB without any other strobe

    // ---- keyboard input routing ----

    // register a handler for a key event to a given keyboard terminal
    void registerKb(int io_addr, int term_num, const kbCallback &cb);
    void unregisterKb(int io_addr, int term_num);

    // send a key event to the specified keyboard/terminal
    void dispatchKeystroke(int io_addr, int term_num, int keyvalue);

    // request the contents of a file to be fed in as a keyboard stream
    void invokeKbScript(int io_addr, int term_num,
                        const std::string &filename);

    // indicates if a script is currently active on a given terminal
    bool isScriptModeActive(int io_addr, int term_num);

    // return how many terminals at this io_addr have active scripts
    int numActiveScripts(int io_addr) noexcept;

    // when invoked on a terminal in script mode, causes key callback to be
    // invoked with the next character from the script.  it returns true if
    // a script supplied a character.
    bool pollScriptInput(int io_addr, int term_num);

    // ---- slot manager ----

    // returns false if the slot is empty, otherwise true.
    // returns card type index and io address via pointers.
    bool getSlotInfo(int slot, int *cardtype_idx, int *addr) noexcept;

    // returns the IO address of the n-th keyboard (0-based).
    // if (n >= # of keyboards), returns -1.
    int getKbIoAddr(int n) noexcept;

    // returns the IO address of the n-th printer (0-based).
    // if (n >= # of printers), returns -1.
    int getPrinterIoAddr(int n) noexcept;

    // return the instance handle of the device at the specified IO address
    // used by IoCardKeyboard.c
    IoCard* getInstFromIoAddr(int io_addr) noexcept;

    // given a slot, return the "this" element
    IoCard* getInstFromSlot(int slot) noexcept;

    // find slot number of disk controller #n (starting with 0).
    // returns true if successful.
    bool findDiskController(int n, int *slot) noexcept;

    // find slot,drive of any disk controller with disk matching name.
    // returns true if successful.
    bool findDisk(const std::string &filename, int *slot, int *drive, int *io_addr);

    // ---- legal cpu system configurations ----

    struct cpuconfig_t {
        int              cpu_type;            // Cpu2200::CPUTYPE_* value
        const char      *label;               // human-readable label
        std::vector<int> ram_size_options;    // list of legal RAM configurations in KB
        std::vector<int> ucode_size_options;  // list of legal ucode sizes, in words
        bool             has_oneshot;         // does it have the timeslice one-shot?
    };

    extern const std::vector<cpuconfig_t> m_cpu_configs;
    const cpuconfig_t* getCpuConfig(const std::string &config_name) noexcept;
    const cpuconfig_t* getCpuConfig(int config_id) noexcept;

};  // namespace system2200

#endif // _INCLUDE_SYSTEM2200_H_

// vim: ts=8:et:sw=4:smarttab
