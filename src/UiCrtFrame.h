// This class manages a window frame representing a CRT terminal.
// It handles menus, toolbar, and statusbar work.  The rest of of the job
// is fobbed off to the Crt class.

#ifndef _INCLUDE_UI_CRT_FRAME_H_
#define _INCLUDE_UI_CRT_FRAME_H_

#include "w2200.h"
#include "wx/wx.h"

class Crt;
class CrtStatusBar;
struct crt_state_t;

// Define a new frame type: this is going to be our main frame
class CrtFrame : public wxFrame
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(CrtFrame);
    // constructor
    CrtFrame(const wxString &title,
             const int io_addr,
             const int term_num,     // 0 if dumb, 1-4 if term mux
             crt_state_t *crt_state);

    // destructor
    ~CrtFrame();

    // make CRT the focus of further keyboard events
    void refocus();

    void setKeywordMode(bool b);
    bool getKeywordMode() const;

    // indicate if this is device 005 or not
    bool isPrimaryCrt() const noexcept;

    // the io address of the emulated keyboard associated with this window
    int getTiedAddr() const noexcept;
    int getTermNum() const noexcept;

    // request the CRT to be destroyed
    void destroyWindow();

    // something changed about the indicated drive -- refresh display
    static void diskEvent(int slot, int drive);

    // set simulation time for informative display
    static void setSimSeconds(int secs, float relative_speed);

    // create a bell (0x07) sound
    void ding();

    // set CRT font style and size
    static int         getNumFonts() noexcept;
    static int         getFontNumber(int idx) noexcept;
    static std::string getFontName(int idx);

    // set/get CRT font size
    void          setFontSize(int size);
    int           getFontSize() const noexcept;

    // pick one of N color schemes
    static int         getNumColorSchemes() noexcept;
    static std::string getColorSchemeName(int idx);
    void               setDisplayColorScheme(int n);
    int                getDisplayColorScheme() const noexcept;

    // values range from 0 to 100
    void setDisplayContrast(int n);
    void setDisplayBrightness(int n);
    int  getDisplayContrast() const noexcept;
    int  getDisplayBrightness() const noexcept;

    bool getTextBlinkPhase() const noexcept;
    bool getCursorBlinkPhase() const noexcept;

    // mechanics of carrying out format for a given filename
    // must be public so statusbar can use it
    void doFormat(const std::string &filename);

    // inspect the named disk.  if it is mounted in a drive, disconnect the
    // filehandle before performing the operation so that when the emulator
    // resumes, it will be forced to reopen the file, picking up any changes
    // made to the disk metadata.
    void doInspect(const std::string &filename);

private:
    // ---- event handlers ----

    void OnScript(wxCommandEvent &event);
    void OnSnapshot(wxCommandEvent &event);
    void OnDump(wxCommandEvent &event);
    void OnQuit(wxCommandEvent &event);
    void OnMenuOpen(wxMenuEvent &event);

    void OnReset(wxCommandEvent &event);
    void OnCpuSpeed(wxCommandEvent &event);

    // menu handler for disk drives
    void OnDiskFactory(wxCommandEvent &event);
    void OnDiskFormat(wxCommandEvent& WXUNUSED(event));
    void OnDisk(wxCommandEvent &event);

    // toggle fullscreen or not
    void OnDisplayFullscreen(wxCommandEvent &event);

    // bring up configuration dialog
    void OnConfigureDialog(wxCommandEvent &event) noexcept;

    // bring up screen configuration dialog
    void OnConfigureScreenDialog(wxCommandEvent &event);

    // toggle the keyword entry mode
    void OnConfigureKeywordMode(wxCommandEvent &event);

    // toggle the special function key toolbar
    void OnConfigureSfToolbar(wxCommandEvent &event);

    // toggle the "display statistics on status bar" control
    void OnConfigureStats(wxCommandEvent &event);

    // associate keyboard input on a CRT with a specific io address
    void OnConfigureKbTie(wxCommandEvent &event);

    // open view for a particular printer
    void OnPrinter(wxCommandEvent &event);

    // print and clear all printers
    void OnPrintAndClear(wxCommandEvent &event);

    // called when a toolbar button is clicked
    void OnToolBarButton(wxCommandEvent &event);

    // called when the A/a status bar button is pressed
    void OnButton(wxCommandEvent &event);

    // called when the window changes size
    void OnSize(wxSizeEvent &event);

    // called when the window is manually closed ("X" button, or sys menu)
    void OnClose(wxCloseEvent& WXUNUSED(event));

    // handle timed display refresh
    void OnTimer(wxTimerEvent &event);

    // ---- utility functions ----

    // create menubar
    void makeMenubar(bool primary_crt, bool smart_term);

    // return the primary CRTframe (/005)
    static CrtFrame *getPrimaryFrame() noexcept;

    // save/get CRT options to the config file
    void saveDefaults();
    void getDefaults();

    // control whether simulation speed shows up on statusbar
    void setShowStatistics(bool show=true);
    bool getShowStatistics() const noexcept;

    void setMenuChecks(const wxMenu *menu);

    // populate the toolbar
    void initToolBar(wxToolBar *tb);

    // refresh a window
    void refreshWindow();

    // ---- data members ----

    wxMenuBar    *m_menuBar;
    CrtStatusBar *m_statBar;
    wxToolBar    *m_toolBar;

    Crt             *m_crt;       // emulated CRT display window

    bool m_fullscreen;          // currently fullscreen or not
    bool m_showstats;           // show timing statistics

    int  m_colorsel;            // index of selected color scheme
    int  m_fontsize[2];         // [1]=fullscreen, [0]=not fullscreen

    int  m_crt_addr;            // used to track configuration options
    int  m_term_num;            // 0 for dumb terms, 1-4 for muxed terms
    bool m_primary_crt;         // true for main crt

    int  m_assoc_kb_addr;       // io address of associated keyboard

#if BIG_BUTTONS
    // holds the icons for the toolbar buttons
    wxBitmap m_sfkeyIcons[17];
#endif

    // triggers display refresh for all windows
    // once upon a time these were static timers, but when the emulation
    // would get reconfigured, the even system would end up constructing
    // the new window before the old one was destroyed.  as a result, the
    // destructor would stop the (static) timer that the new window had
    // just initiated.
    wxTimer *m_RefreshTimer;    // triggers to cause a screen update
    wxTimer *m_QSecTimer;       // 4 Hz event for blink & frames/sec calc
    int      m_blink_phase;
    int      m_fps;             // most recent frames/sec count

    // the one privileged CRT (/005 display, or term 1 of MXD at 0x00)
    static CrtFrame *m_primaryFrame;

    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_FRAME_H_

// vim: ts=8:et:sw=4:smarttab
