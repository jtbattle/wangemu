// we don't want any of the core emulator to directly touch any of the
// GUI includes or functions.
//
// in the cases where the core needs to talk to the gui, we have a
// non-member function that the core can call, and that function is
// just a thunk into the GUI-verse.

#ifndef _INCLUDE_UI_H_
#define _INCLUDE_UI_H_

#include "w2200.h"

class DiskCtrlCfgState;
class CrtFrame;
class PrinterFrame;
struct crt_state_t;

// =============================================================
// exported by UI
// =============================================================

// called at the start of time to create the actual display
enum ui_screen_t {
    UI_SCREEN_64x16,
    UI_SCREEN_80x24,
    UI_SCREEN_2236DE
};
CrtFrame* UI_displayInit(const int screen_type,
                         const int io_addr, const int term_num,
                         crt_state_t *crt_state);

// called before the display gets shut down
void UI_displayDestroy(CrtFrame *wnd);

// create a bell (0x07) sound for the given terminal
void UI_displayDing(CrtFrame *wnd);

// inform the UI how far along the simulation is in emulated time
void UI_setSimSeconds(unsigned long seconds, float relative_speed);

// the state of the specified disk drive has changed
void UI_diskEvent(int slot, int drive);

// ---- printer interface ----

// printer view
PrinterFrame* UI_printerInit(int io_addr);

// called before the printer view gets shut down
void UI_printerDestroy(PrinterFrame *wnd);

// emit a character to the printer
void UI_printerChar(PrinterFrame *wnd, uint8 byte);

// ---- system configuration ----

// launch the system configuration dialog, which might eventually
// call back into System2200.setConfig() function.
void UI_SystemConfigDlg();

// configure a disk controller
void UI_ConfigureCard(DiskCtrlCfgState *cfg);

// ---- general status notification ----

// send an error/warning to the user that requires an action to close
void UI_Error(const char *fmt, ...);
void UI_Warn(const char *fmt, ...);
void UI_Info(const char *fmt, ...);

// get a YES/NO confirmation.  return true for yes.
bool UI_Confirm(const char *fmt, ...);

#endif // _INCLUDE_UI_H_

// vim: ts=8:et:sw=4:smarttab
