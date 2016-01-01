#ifndef _INCLUDE_IOCARD_KEYBOARD_H_
#define _INCLUDE_IOCARD_KEYBOARD_H_

#include "IoCard.h"

class Cpu2200;
class Scheduler;
class Timer;
class ScriptFile;

class IoCardKeyboard : public IoCard
{
public:
    // see the base class for the definition of the public functions

    // ----- common IoCard functions -----
    IoCardKeyboard(Scheduler &scheduler, Cpu2200 &cpu,
                   int baseaddr, int cardslot);
    ~IoCardKeyboard();

    vector<int> getAddresses() const;

    void  reset(int hard_reset=1);
    void  select();
    void  deselect();
    void  OBS(int val);
    void  CBS(int val);
    void  CPB(bool busy);

    // ----- IoCardKeyboard specific functions -----

    // returns 1 if the keyboard at the specified io_addr
    // is already processing a script
    static bool script_mode(int io_addr);

    // open up a script for keyboard input.  returns true on success.
    static bool invoke_script(const int io_addr, const string &filename);

    // this is called by UI when a key is entered by the user
    static void receiveKeystroke(int io_addr, int keycode);

    // various keyboard related flags
    enum { KEYCODE_SF   = 0x0100,  // special function key flag
           KEYCODE_HALT = 0x0200,  // user pressed the reset button on the keyboard
           KEYCODE_EDIT =    240   // the EDIT key
    };

private:
    // ---- card properties ----
    const string getDescription() const;
    const string getName() const;
    vector<int> getBaseAddresses() const;

    // timer callback function to put some required delay in script processing
    void tcbScript();

    // see if script has any chars to inject
    void script_poll();

    // test if any key is ready to accept
    void check_keyready();

    Scheduler  &m_scheduler;      // shared event scheduler
    Timer      *m_tmr_script;     // keystrokes are sent a few 10s of uS after !CPB
    Cpu2200    &m_cpu;            // associated CPU
    const int   m_baseaddr;       // the address the card is mapped to
    const int   m_slot;           // which slot the card is plugged into
    bool        m_selected;       // this card is being addressed
    bool        m_cpb;            // 1=CPU busy (not accepting IBS input)
    bool        m_key_ready;      // key_code is valid
    int         m_key_code;       // keycode of most recently received keystroke
    ScriptFile *m_script_handle;  // ID of which script stream we're processing

    CANT_ASSIGN_OR_COPY_CLASS(IoCardKeyboard);
};


// ---- some non-member functions ----
// so that core emu doesn't need to know about classes

// called by UI when a keystroke event occurs
void core_sysKeystroke(int io_addr, int keycode);

// this is how the UI opens a keyboard script file and feeds it to
// the keyboard with the associated io_addr.
bool core_invokeScript(const int io_addr, const string &filename);

// returns true if the keyboard at the specified io_addr is already
// processing a script
bool core_isKbInScriptMode(int io_addr);

#endif // _INCLUDE_IOCARD_KEYBOARD_H_

// vim: ts=8:et:sw=4:smarttab
