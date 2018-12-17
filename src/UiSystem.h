// TheApp is where the emulator first "wakes up", in the OnInit() function.
// The OnIdle() event handler is the point where the core emulation runs,
// that is, emulated time passes.  The emulation doens't happen in this class;
// instead the OnIdle gets reflected into the core system2200 class.
//
// The rest of it is just a pachinko machine of events to redraw the screen
// and handle user interaction.
//
// Although not the most natural place, this class also provides the means
// for any window to add an About/Help set of menu items.

#ifndef _INCLUDE_UI_SYSTEM_H_
#define _INCLUDE_UI_SYSTEM_H_

#include "w2200.h"

#include "wx/wx.h"

// ==================== exported by UiSystem.cpp ====================

// a macro to cause the help menu to do something
#define HELP_MENU_EVENT_MAPPINGS() \
    EVT_MENU (TheApp::Help_Quickstart,      TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Configure,       TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Keyboard,        TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Menus,           TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Printer,         TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Script,          TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_DiskFactory,     TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_DiskCheat,       TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Website,         TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_Relnotes,        TheApp::OnHelp_Launcher) \
    EVT_MENU (TheApp::Help_About,           TheApp::OnHelp_About)

// Define a new application type, each program should derive a class from wxApp
class TheApp : public wxApp
{
public:
    // called by various frames to install the canonical help menu
    // these are menu IDs so that the OnHelp_Launcher() knows which item is chosen
    enum {
            Help_Quickstart = 200,
            Help_Configure,
            Help_Keyboard,
            Help_Menus,
            Help_Printer,
            Help_Script,
            Help_DiskFactory,
            Help_DiskCheat,
            Help_Website,
            Help_Relnotes,
            Help_About = wxID_ABOUT,
        };
    void OnHelp_Launcher(wxCommandEvent &event);
    void OnHelp_About(wxCommandEvent &event);

    // create a help menu, used for all frames that care
    static wxMenu* makeHelpMenu();

    CANT_ASSIGN_CLASS(TheApp);

private:
    // ---- event handlers ----

    // this one is called on application startup and is a good place for the app
    // initialization (doing it here and not in the ctor allows to have an error
    // return: if OnInit() returns false, the application terminates)
    bool OnInit() override;

    // like the name says
    int OnExit() override;

    // set the command line parsing options
    void OnInitCmdLine(wxCmdLineParser& parser) override;

    // after the command line has been parsed, decode what it finds
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

    // called whenever there is nothing else to do
    void OnIdle(wxIdleEvent &event);

    static void getGlobalDefaults();
    static void saveGlobalDefaults();

    DECLARE_EVENT_TABLE()
};

#endif // _INCLUDE_UI_SYSTEM_H_

// vim: ts=8:et:sw=4:smarttab
