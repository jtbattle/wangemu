/////////////////////////////////////////////////////////////////////////////
// UiSystem.cpp
// Modified from wxWindows sample program, drawing.cpp, by Robert Roebling.
//
// This file contains the entry point for the emulator, as well as the
// locus of the GUI logic.
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

struct crt_state_t;

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Cpu2200.h"
#include "host.h"
#include "system2200.h"
#include "Ui.h"
#include "UiSystem.h"
#include "UiSystemConfigDlg.h"
#include "UiCrtFrame.h"
#include "UiDiskCtrlCfgDlg.h"
#include "UiTermMuxCfgDlg.h"
#include "UiMyAboutDlg.h"
#include "UiPrinterFrame.h"

#include "wx/cmdline.h"         // req'd by wxCmdLineParser
#include "wx/filename.h"

// ============================================================================
// implementation
// ============================================================================

// Create a new application object: this macro will allow wxWindows to
// create the application object during program execution (it's better
// than using static object for many reasons) and also declares the
// accessor function wxGetApp() which will return the reference of the
// right type (i.e. TheApp and not wxApp)
IMPLEMENT_APP(TheApp)

BEGIN_EVENT_TABLE(TheApp, wxApp)
    EVT_IDLE (TheApp::OnIdle)
END_EVENT_TABLE()

// ----------------------------------------------------------------------------
// the application class
// ----------------------------------------------------------------------------

// `Main program' equivalent: the program execution "starts" here
bool
TheApp::OnInit()
{
#if 0
    // read wxToolBar notes to see why this is done
    if (wxTheApp->GetComCtl32Version() >= 600 && ::wxDisplayDepth() >= 32) {
        wxSystemOptions::SetOption("msw.remap", 2);
    }
#endif

    host::initialize();

    system2200::initialize();  // build the world
    system2200::reset(true);   // cold start

    // must call base class version to get command line processing
    // if false, the app terminates
    return wxApp::OnInit();
}


// this is called whenever the wxWidget event queue is empty,
// indicating there is no work pending.
void
TheApp::OnIdle(wxIdleEvent &event)
{
    if (system2200::onIdle()) {
        event.RequestMore(true);            // give more idle events
    }
}


// called just before quitting the entire app, but before wxWindows
// cleans up its internal resources.
// use this to clean up any allocated globals
int
TheApp::OnExit()
{
    // clean up, which includes saving .ini file
    host::terminate();

    return 0;
}


// set the command line parsing options
void
TheApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    // set default wx options
    wxApp::OnInitCmdLine(parser);

    parser.DisableLongOptions();        // -foo, not --foo

    // add options specific to this app
    parser.AddOption("s", "script", "script file to load on startup", wxCMD_LINE_VAL_STRING);
}


// after the command line has been parsed, decode what it finds
bool
TheApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    // let base class handle its defaults
    const bool OK = wxApp::OnCmdLineParsed(parser);

    if (OK) {
        // do my processing here
        // UI_Info("Command line items: %d", parser.GetParamCount());
        wxString filename;
        if (parser.Found("s", &filename)) {
            const int io_addr = 0x01;   // default keyboard device
            std::string fn(filename.c_str());
            system2200::kb_invokeScript(io_addr, -1, fn);
            bool success = true;  // FIXME: old invokeScript returned a boolean -- change new i/f to match?
            if (!success) {
                const char *s = filename.c_str();
                UI_Warn("Failed to open script '%s'", s);
            }
        }
    }

    return OK;
}


// ========================================================================
// Help/About functions
// ========================================================================

wxMenu*
TheApp::makeHelpMenu()
{
    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(Help_Quickstart,  "&Quickstart",      "Information for new users of WangEmu");
    menuHelp->Append(Help_Configure,   "&Configuration",   "Information about configuring the emulator");
    menuHelp->Append(Help_Keyboard,    "&Keyboard",        "Information about how the 2200 keyboard is mapped onto yours");
    menuHelp->Append(Help_Menus,       "&Menus",           "Information about the emulator menu system");
    menuHelp->Append(Help_Printer,     "&Printer",         "Information about the using the emulated printer");
    menuHelp->Append(Help_Script,      "&Script",          "Information about loading files into the emulator");
    menuHelp->Append(Help_DiskFactory, "Disk &Factory",    "Information about creating and inspecting virtual disks");
    menuHelp->Append(Help_DiskCheat,   "&Disk Cheatsheet", "Information about cataloging disks, loading and saving files");
    menuHelp->Append(Help_Website,     "&Website",         "Open a browser to the emulator's web site");
    menuHelp->AppendSeparator();
    menuHelp->Append(Help_Relnotes,    "&Release notes...", "Detailed notes about this release");
    menuHelp->Append(Help_About,       "&About...",         "Information about the program");

    return menuHelp;
}


void
TheApp::OnHelp_Launcher(wxCommandEvent &event)
{
    wxString helpfile;
    bool absolute = false;
    switch (event.GetId()) {
        case Help_Quickstart:   helpfile = "quickstart.html";      break;
        case Help_Configure:    helpfile = "configure.html";       break;
        case Help_Keyboard:     helpfile = "keyboard.html";        break;
        case Help_Menus:        helpfile = "menus.html";           break;
        case Help_Printer:      helpfile = "printer.html";         break;
        case Help_Script:       helpfile = "script.html";          break;
        case Help_DiskFactory:  helpfile = "disk_factory.html";    break;
        case Help_DiskCheat:    helpfile = "disk_cheatsheet.html"; break;
        case Help_Relnotes:     helpfile = "relnotes.txt";         break;
        case Help_Website:      helpfile = "http://www.wang2200.org/"; absolute = true; break;
        default:
            assert(false);
            return;
    }

    wxString sep(wxFileName::GetPathSeparator());
    if (helpfile.EndsWith(".html")) {
        // look in html/ subdirectory
        helpfile = "html" + sep + helpfile;
    }

    wxString target_file = (absolute) ? helpfile
                                      : ("file:" + sep + sep +
                                         host::getAppHome() +
                                         sep + helpfile);
#ifdef __WXMSW__
    // wxLaunchDefaultBrowser()'s argument used to use windows-style paths,
    // ie, backslash as a path separator.  however, at some point between
    // 2008 and 2014, that changed; now it takes canonical URL paths.
    target_file.Replace(wxT("\\"), wxT("/"));
#endif

    ::wxLaunchDefaultBrowser(target_file);
}


void
TheApp::OnHelp_About(wxCommandEvent& WXUNUSED(event))
{
    MyAboutDlg dlg(nullptr);
    dlg.ShowModal();
}


// ============================================================================
// alert messages
// ============================================================================

// shared code
static bool
UI_AlertMsg(long style, std::string title, const char *fmt, va_list &args)
{
    char buff[1000];
    vsnprintf(&buff[0], sizeof(buff), fmt, args);

    std::string info(&buff[0]);
    wxMessageDialog dialog(nullptr, info, title, style);
    const int rv = dialog.ShowModal();
    return (rv == wxID_YES);
}

// error icon
void
UI_Error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    UI_AlertMsg(wxICON_ERROR, "Error", fmt, args);
    va_end(args);
}

// exclamation icon
void
UI_Warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    UI_AlertMsg(wxICON_EXCLAMATION, "Warning", fmt, args);
    va_end(args);
}

void
UI_Info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    UI_AlertMsg(wxICON_INFORMATION, "Information", fmt, args);
    va_end(args);
}


// get a YES/NO confirmation.  return true for yes.
bool
UI_Confirm(const char *fmt, ...)
{
    const long style = wxYES | wxNO | wxNO_DEFAULT | wxICON_EXCLAMATION;
//  const long style = wxYES | wxNO | wxNO_DEFAULT | wxICON_QUESTION;

    va_list args;
    va_start(args, fmt);
    const bool rv = UI_AlertMsg(style, "Question", fmt, args);
    va_end(args);

    return rv;
}


// ========================================================================
// interface between core and UI routines
//
// The core emulator calls these functions to interact with the GUI.
// These wrappers just invoke the appropriate instance.
// ========================================================================

// ---- Crt wrappers ----

// called at the start of time to create the actual display
CrtFrame*
UI_displayInit(const int screen_type, const int io_addr, const int term_num,
               crt_state_t *crt_state)
{
    const int cputype = system2200::config().getCpuType();
    const char *cpustr = (cputype == Cpu2200::CPUTYPE_2200B)   ? "2200B"
                       : (cputype == Cpu2200::CPUTYPE_2200T)   ? "2200T"
                       : (cputype == Cpu2200::CPUTYPE_VP)      ? "2200VP"
                       : (cputype == Cpu2200::CPUTYPE_MVP)     ? "2200MVP"
                       : (cputype == Cpu2200::CPUTYPE_MVPC)    ? "2200MVP-C"
                       : (cputype == Cpu2200::CPUTYPE_MICROVP) ? "MicroVP"
                                                               : "unknown cpu";

    char *dispstr = "unknown";
    switch (screen_type) {
        case UI_SCREEN_64x16:  dispstr = "64x16"; break;
        case UI_SCREEN_80x24:  dispstr = "80x24"; break;
        case UI_SCREEN_2236DE: dispstr = "2236DE"; break;
        default: assert(false);
    }

    wxString title;
    if (screen_type != UI_SCREEN_2236DE) {
        // old style display
        title.Printf("Wang %s %s CRT /0%02X", cpustr, dispstr, io_addr);
    } else {
        // smart terminal mux
        // internally, term_num is 0-indexed, but in Wang documentation,
        // the terminal number is 1-based
        title.Printf("MXD/%02X Term#%d Wang %s %s",
                        io_addr, term_num+1, cpustr, dispstr);
    }

    // Create the main application window
    CrtFrame *crtptr = new CrtFrame(title, io_addr, term_num, crt_state);
    assert(crtptr != nullptr);
    return crtptr;
}


// called before the display gets shut down
void
UI_displayDestroy(CrtFrame *wnd)
{
    assert(wnd != nullptr);
    wnd->destroyWindow();
}


// create a bell (0x07) sound for the given terminal
void UI_displayDing(CrtFrame *wnd)
{
    assert(wnd != nullptr);
    wnd->ding();
}


// inform the UI how far along the simulation is in emulated time
void
UI_setSimSeconds(unsigned long seconds, float relative_speed)
{
    CrtFrame::setSimSeconds(seconds, relative_speed);
}


void
UI_diskEvent(int controller, int drive)
{
    CrtFrame::diskEvent(controller, drive);
}


// ---- printer wrappers ----

// called at the start of time to create the actual display
PrinterFrame*
UI_printerInit(int io_addr)
{
    char title[32];
    sprintf(&title[0], "Wang Printer /%03X", io_addr);
    return new PrinterFrame(&title[0], io_addr);
}


// called before the display gets shut down
void
UI_printerDestroy(PrinterFrame *wnd)
{
    assert(wnd != nullptr);
    wnd->destroyWindow();
}


// this is called only from IoCardPrinter::OBS()
// emit a character to the display
void
UI_printerChar(PrinterFrame *wnd, uint8 byte)
{
    assert(wnd != nullptr);
    wnd->printChar(byte);
}

// ---- system configuration wrapper ----

// launch the system configuration dialog, which might eventually
// call back into system2200.setConfig() function.
void
UI_SystemConfigDlg()
{
    SystemConfigDlg(nullptr).ShowModal();
}


// configure a card
// FIXME: don't have two unique functions; there should be one
// function that can figure out which dialog to invoke.
void
UI_ConfigureCard(DiskCtrlCfgState *cfg)
{
    assert(cfg != nullptr);
    DiskCtrlCfgDlg(nullptr, *cfg).ShowModal();
}

void
UI_ConfigureCard2(TermMuxCfgState *cfg)
{
    assert(cfg != nullptr);
    TermMuxCfgDlg(nullptr, *cfg).ShowModal();
}

// vim: ts=8:et:sw=4:smarttab
