// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "IoCardDisk.h"
#include "IoCardKeyboard.h"     // to pick up core_* keyboard interface
#include "IoCardPrinter.h"
#include "TerminalState.h"
#include "Ui.h"                 // emulator interface
#include "UiCrt.h"
#include "UiCrtConfigDlg.h"
#include "UiCrtFrame.h"
#include "UiCrtStatusBar.h"
#include "UiDiskFactory.h"
#include "UiPrinterFrame.h"
#include "UiSystem.h"
#include "host.h"
#include "system2200.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

// ----------------------------------------------------------------------------
// resources
// ----------------------------------------------------------------------------

// the application icon
#include "wang.xpm"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
    // menu items
    File_Script = 1,
    File_Snapshot,
#if HAVE_FILE_DUMP
    File_Dump,
#endif
    File_Quit = wxID_EXIT,

    CPU_HardReset = File_Snapshot+3,
    CPU_WarmReset,
    CPU_ActualSpeed,
    CPU_UnregulatedSpeed,

    Disk_New,     // unique
    Disk_Inspect, // unique
    Disk_Format,  // unique
    Disk_Insert,  // \__ there are up to four disks per controller, up to one per IO slot
    Disk_Remove,  // /
    // ...
    Disk_LastRemove = Disk_Insert + 8*NUM_IOSLOTS-1,
    Disk_Realtime,
    Disk_UnregulatedSpeed,

    Configure_Dialog,
    Configure_Screen_Dialog,
    Configure_KeywordMode,
    Configure_SF_toolBar,
    Configure_Fullscreen,
    Configure_Stats,
    Configure_KB_Tie0,
    // ...
    Configure_KB_TieN = Configure_KB_Tie0 + NUM_IOSLOTS-1,

    // slots for printer windows
    Printer_0,
    // ...
    Printer_N = Printer_0 + NUM_IOSLOTS-1,

    Print_PrintAndClear,

    // other IDs
    TB_TOOLBAR,
    TB_SF0,  TB_SF1,  TB_SF2,  TB_SF3,
    TB_SF4,  TB_SF5,  TB_SF6,  TB_SF7,
    TB_SF8,  TB_SF9,  TB_SF10, TB_SF11,
    TB_SF12, TB_SF13, TB_SF14, TB_SF15,
    TB_EDIT,

    Timer_Frame,
    Timer_QSec,
};


// ========== Crt font styles ==========

static constexpr struct font_table_t {
    int         size;   // encoding for font as it appears in .ini file
    const char *name;   // descriptive string
} font_table[] = {
    {  1, "Dot-matrix Font 1:1" },
    {  2, "Dot-matrix Font 1:2" },
    {  3, "Dot-matrix Font 2:4" },
    {  8, "Font Size  8" },
    { 10, "Font Size 10" },
    { 12, "Font Size 12" },
    { 14, "Font Size 14" },
    { 18, "Font Size 18" },
    { 24, "Font Size 24" },
};
const int num_fonts = (sizeof(font_table) / sizeof(font_table_t));


// ========== Crt color schemes ==========

static constexpr struct colorscheme_t {
    unsigned char fg_r, fg_g, fg_b;     // foreground color
    unsigned char bg_r, bg_g, bg_b;     // background color
    const char   *help_label;           // string as it appears on statusbar
} colorscheme[] = {
    {
#ifdef __WXMAC__
      0x80, 0xFF, 0x80, // Mac has different gamma than a PC, I guess
#else
      0x80, 0xFF, 0x80,
#endif
      0x00, 0x00, 0x00,
      "Green phosphor" },

    { 0xFF, 0xFF, 0xFF,
      0x00, 0x00, 0x00,
      "White phosphor" },

    { 0xFF, 0xFF, 0xFF,
#ifdef __WXMAC__
      0x10, 0x10, 0x80, // Mac different gamma than PC, I guess
#else
      0x40, 0x40, 0xA0,
#endif
      "White on Blue" }
};

static const int num_colorschemes = sizeof(colorscheme) / sizeof(colorscheme_t);

// ----------------------------------------------------------------------------
// CrtFrame
// ----------------------------------------------------------------------------

// what features are visible when in full screen mode
#define FULLSCREEN_FLAGS (wxFULLSCREEN_NOBORDER  |   \
                          wxFULLSCREEN_NOCAPTION |   \
                          wxFULLSCREEN_NOSTATUSBAR)

// CrtFrame static members
CrtFrame* CrtFrame::m_primary_frame = nullptr;

// constructor
CrtFrame::CrtFrame(const wxString& title,
                   const int io_addr,
                   const int term_num,
                   crt_state_t *crt_state) :
       wxFrame((wxFrame *)nullptr, -1, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_FRAME_STYLE | wxNO_FULL_REPAINT_ON_RESIZE),
    m_crt_addr(io_addr),
    m_term_num(term_num),
    m_smart_term(crt_state->screen_type == UI_SCREEN_2236DE),
    m_small_crt(crt_state->chars_w == 64),
    m_primary_crt(m_smart_term ? ((m_crt_addr == 0x00) && (m_term_num == 0))
                               :  (m_crt_addr == 0x05))
{
    if (m_primary_crt) {
        assert(!m_primary_frame);
        m_primary_frame = this;
    }

    // set the frame icon
    SetIcon(&wang_xpm[0]);

    makeMenubar();

    // create a status bar with two panes
    m_statusbar = new CrtStatusBar(this, m_smart_term, m_primary_crt);
    SetStatusBar(m_statusbar);
    SetStatusBarPane(1);        // use second pane for menu help strings

    // create toolbar
#if 0
    const long tb_style = wxNO_BORDER | wxHORIZONTAL | wxTB_FLAT | wxTB_TEXT
                        | wxTB_NOICONS
#else
    const long tb_style = wxNO_BORDER | wxHORIZONTAL | wxTB_FLAT;
#endif
    m_toolbar = CreateToolBar(tb_style, TB_TOOLBAR);
    initToolBar(m_toolbar);
    m_toolbar->Show(false); // can get changed in GetDefaults()

    m_crt = new Crt(this, crt_state);

    getDefaults();      // get configuration options, or supply defaults

    // if I don't do this before ShowFullScreen, bad things happen when
    // switching later from fullscreen to !fullscreen in some circumstances
    // wxMSW, wx2.5.2
    Show(true);

#ifndef __WXMAC__
    ShowFullScreen(m_fullscreen, FULLSCREEN_FLAGS);
#endif

    // track screen refresh rate, for kicks
    m_fps = 0;

    // only the primary has a status bar
    m_refresh_tmr     = std::make_unique<wxTimer>(this, Timer_Frame);
    m_quarter_sec_tmr = std::make_unique<wxTimer>(this, Timer_QSec);

    // it is hard to predict what the optimal refresh period
    // for a given system
    m_refresh_tmr->Start(30, wxTIMER_CONTINUOUS);       // ~30 fps
    m_quarter_sec_tmr->Start(250, wxTIMER_CONTINUOUS);  // 4 Hz

    // event routing table
    Bind(wxEVT_MENU, &CrtFrame::OnScript,   this, File_Script);
    Bind(wxEVT_MENU, &CrtFrame::OnSnapshot, this, File_Snapshot);
#if HAVE_FILE_DUMP
    Bind(wxEVT_MENU, &CrtFrame::OnDump,     this, File_Dump);
#endif
    Bind(wxEVT_MENU, &CrtFrame::OnQuit,     this, File_Quit);

    Bind(wxEVT_MENU, &CrtFrame::OnReset,    this, CPU_HardReset);
    Bind(wxEVT_MENU, &CrtFrame::OnReset,    this, CPU_WarmReset);
    Bind(wxEVT_MENU, &CrtFrame::OnCpuSpeed, this, CPU_ActualSpeed);
    Bind(wxEVT_MENU, &CrtFrame::OnCpuSpeed, this, CPU_UnregulatedSpeed);

    Bind(wxEVT_MENU, &CrtFrame::OnDiskFactory, this, Disk_New);
    Bind(wxEVT_MENU, &CrtFrame::OnDiskFactory, this, Disk_Inspect);
    Bind(wxEVT_MENU, &CrtFrame::OnDiskFormat,  this, Disk_Format);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &CrtFrame::OnDisk, this,
                            Disk_Insert, Disk_Insert+NUM_IOSLOTS*8-1);
    Bind(wxEVT_MENU, &CrtFrame::OnDiskSpeed, this, Disk_Realtime);
    Bind(wxEVT_MENU, &CrtFrame::OnDiskSpeed, this, Disk_UnregulatedSpeed);

    Bind(wxEVT_MENU, &CrtFrame::OnConfigureDialog,       this, Configure_Dialog);
    Bind(wxEVT_MENU, &CrtFrame::OnConfigureScreenDialog, this, Configure_Screen_Dialog);
    Bind(wxEVT_MENU, &CrtFrame::OnConfigureKeywordMode,  this, Configure_KeywordMode);
    Bind(wxEVT_MENU, &CrtFrame::OnConfigureSfToolbar,    this, Configure_SF_toolBar);
    Bind(wxEVT_MENU, &CrtFrame::OnDisplayFullscreen,     this, Configure_Fullscreen);
    Bind(wxEVT_MENU, &CrtFrame::OnConfigureStats,        this, Configure_Stats);
    Bind(wxEVT_COMMAND_MENU_SELECTED, &CrtFrame::OnConfigureKbTie, this,
                            Configure_KB_Tie0, Configure_KB_TieN);

    // printer window support
    Bind(wxEVT_COMMAND_MENU_SELECTED, &CrtFrame::OnPrinter, this,
                            Printer_0, Printer_N);
    Bind(wxEVT_MENU, &CrtFrame::OnPrintAndClear, this, Print_PrintAndClear);

    // toolbar event handler
    Bind(wxEVT_TOOL, &CrtFrame::OnToolBarButton, this, TB_SF0, TB_EDIT);

    // non-menu event handlers
    Bind(wxEVT_MENU_OPEN,    &CrtFrame::OnMenuOpen, this);
    Bind(wxEVT_CLOSE_WINDOW, &CrtFrame::OnClose,    this);
    Bind(wxEVT_TIMER,        &CrtFrame::OnTimer,    this, Timer_Frame);
    Bind(wxEVT_TIMER,        &CrtFrame::OnTimer,    this, Timer_QSec);
}


// indicate if this is device 005 or not
bool
CrtFrame::isPrimaryCrt() const noexcept
{
    return m_primary_crt;
}


// create menubar
#ifdef __WXMAC__
    #define ALT  "Ctrl"         /* this gets remapped to Cmd */
    #define ALT2 "Shift-Ctrl"   /* this gets remapped to Shift-Cmd */
#else
    #define ALT  "Alt"
    #define ALT2 "Shift-Alt"
#endif

void
CrtFrame::makeMenubar()
{
    wxMenu *menu_file = new wxMenu;
    if (m_primary_crt || m_smart_term) {
        menu_file->Append(File_Script,   "&Script...", "Redirect keyboard from a file");
    }
    menu_file->Append(File_Snapshot, "Screen &Grab...\t" ALT "+G", "Save an image of the screen to a file");
#if HAVE_FILE_DUMP
    if (m_primary_crt) {
        menu_file->Append(File_Dump,     "Dump Memory...", "Save an image of the system memory to a file");
    }
#endif
    menu_file->Append(File_Quit,     "E&xit\t" ALT "+X", "Quit the program");

    wxMenu *menu_cpu = nullptr;
    menu_cpu = new wxMenu;
    if (m_primary_crt) {
        menu_cpu->Append(CPU_HardReset, "Hard Reset CPU\t" ALT2 "+R", "Perform a power-up reset");
    }
    menu_cpu->Append(CPU_WarmReset, "Warm Reset CPU\t" ALT2 "+W", "Perform a state-preserving reset");
    if (m_primary_crt) {
        menu_cpu->AppendSeparator();
        menu_cpu->Append(CPU_ActualSpeed,      "&Actual Speed",      "Run emulation at speed of the actual machine", wxITEM_CHECK);
        menu_cpu->Append(CPU_UnregulatedSpeed, "&Unregulated Speed", "Run emulation at maximum speed", wxITEM_CHECK);
    }

    wxMenu *menu_disk = nullptr;
    if (m_primary_crt) {
        // nothing to do except add top -- it is added dynamically later
        menu_disk = new wxMenu;
    }

    // printer view
    wxMenu *menu_printer = nullptr;
    if (m_primary_crt && (system2200::getPrinterIoAddr(0) >= 0)) {
        // there is at least one printer
        menu_printer = new wxMenu;
        for (int i=0; ; i++) {
            const int io_addr = system2200::getPrinterIoAddr(i);
            if (io_addr < 0) {
                break;
            }
            wxString label;
            label.Printf("Show Printer /%03X", io_addr);
            wxString help;
            help.Printf("Show view for printer /%03X", io_addr);
            menu_printer->Append(Printer_0+i, label, help);
        }
        menu_printer->Append(Print_PrintAndClear, "Print and Clear All",   "Print and clear all printer logs");
    }

    wxMenu *menu_config = new wxMenu;
    if (m_primary_crt) {
        menu_config->Append(Configure_Dialog,     "&Configure System...",      "Change I/O settings");
    }
    menu_config->Append(Configure_Screen_Dialog,  "&Configure Screen...",      "Change display settings");
    if (m_smart_term) {
        menu_config->Append(Configure_KeywordMode,    "&Kaps lock\t" ALT "+K",  "Toggle keyboard keyword mode",        wxITEM_CHECK);
    } else {
        menu_config->Append(Configure_KeywordMode,    "&Keyword mode\t" ALT "+K",  "Toggle keyboard keyword mode",        wxITEM_CHECK);
    }
    menu_config->Append(Configure_SF_toolBar,     "SF key toolbar",            "Toggle special function key toolbar", wxITEM_CHECK);
    menu_config->Append(Configure_Fullscreen,     "Fullscreen\t" ALT "+Enter", "Toggle full screen display",          wxITEM_CHECK);
    if (m_primary_crt) {
        menu_config->Append(Configure_Stats,      "Statistics",                "Toggle statistics on statusbar",      wxITEM_CHECK);
    }
    if (system2200::getKbIoAddr(1) >= 0) {
        // there is more than one keyboard
        menu_config->AppendSeparator();
        for (int i=0; ; i++) {
            const int addr = system2200::getKbIoAddr(i);
            if (addr < 0) {
                break;
            }
            wxString label;
            wxString help;
            label.Printf("Tie keyboard to /%03X", addr);
            help.Printf("Tie keyboard to IO device /%03X", addr);
            menu_config->Append(Configure_KB_Tie0+i, label, help, wxITEM_CHECK);
        }
    }

    // make the help menu (as if it isn't obvious below!)
    wxMenu *menu_help = TheApp::makeHelpMenu(this);

    // now append the freshly created menu to the menu bar...
    m_menubar = new wxMenuBar;

    m_menubar->Append(menu_file, "&File");
    if (menu_cpu != nullptr) {
        m_menubar->Append(menu_cpu, "C&PU");
    }
    if (menu_disk != nullptr) {
        m_menubar->Append(menu_disk, "&Disk");
    }
    if (menu_printer != nullptr) {
        m_menubar->Append(menu_printer, "&Printer");
    }
    m_menubar->Append(menu_config, "&Configure");
    m_menubar->Append(menu_help, "&Help");

    // ... and attach this menu bar to the frame
    SetMenuBar(m_menubar);
}


// this is called just before a menu is displayed.
// set the check status for each of the menu items.
// also dynamically disables/enables menu items,
void
CrtFrame::setMenuChecks(const wxMenu *menu)
{
    // ----- file --------------------------------------
    const bool script_running = system2200::isScriptModeActive(m_assoc_kb_addr, m_term_num);
    m_menubar->Enable(File_Script, !script_running);

    // ----- cpu ---------------------------------------
    if (isPrimaryCrt()) {
        const bool regulated = system2200::isCpuSpeedRegulated();
        m_menubar->Check(CPU_ActualSpeed,       regulated);
        m_menubar->Check(CPU_UnregulatedSpeed, !regulated);
    }

    // ----- disk --------------------------------------
    // dynamically generate the menu each time.
    // we qualify this one and regenerate it only if we must.
    int disk_menu_pos = m_menubar->FindMenu("Disk");
    if (isPrimaryCrt() && (disk_menu_pos >= 0)
        && (menu == m_menubar->GetMenu(disk_menu_pos))) {
        wxMenu *disk_menu = m_menubar->GetMenu(disk_menu_pos);
        const int items = disk_menu->GetMenuItemCount();

        // the entire Disk menu used to be recreated and replaced each time,
        // but that caused problems on wxMAC, so now instead all the menu
        // items get removed and replaced each time.
        for (int i=items-1; i>=0; i--) {
            wxMenuItem *item = disk_menu->FindItemByPosition(i);
            disk_menu->Delete(item);
        }

        // see if there are any disk controllers
        for (int controller=0; ; controller++) {
            int slot = 0, io_addr = 0;
            if (!system2200::findDiskController(controller, &slot)) {
                break;
            }
            const bool ok = system2200::getSlotInfo(slot, nullptr, &io_addr);
            assert(ok);
            for (int d=0; d < 4; d++) {
                const int stat = IoCardDisk::wvdDriveStatus(slot, d);
                if (!(stat & IoCardDisk::WVD_STAT_DRIVE_EXISTENT)) {
                    break;
                }
                const char drive_ch = ((d & 1) == 0) ? 'F' : 'R';
                const int  addr_off = ((d & 2) == 0) ? 0x00 : 0x40;
                const int  eff_addr = io_addr + addr_off;
                if ((stat & IoCardDisk::WVD_STAT_DRIVE_OCCUPIED) != 0) {
                    wxString str1, str2;
                    str1.Printf("Drive %c/%03X: Remove", drive_ch, eff_addr);
                    str2.Printf("Remove the disk from drive %d, unit /%03X", d, eff_addr);
                    disk_menu->Append(Disk_Remove+8*slot+2*d, str1, str2, wxITEM_CHECK);
                } else {
                    wxString str1, str2;
                    str1.Printf("Drive %c/%03X: Insert", drive_ch, eff_addr);
                    str2.Printf("Insert a disk into drive %d, unit /%03X", d, eff_addr);
                    disk_menu->Append(Disk_Insert+8*slot+2*d, str1, str2, wxITEM_CHECK);
                }
            }
            disk_menu->AppendSeparator();
        }
        disk_menu->Append(Disk_New,     "&New Disk...",     "Create virtual disk");
        disk_menu->Append(Disk_Inspect, "&Inspect Disk...", "Inspect/modify virtual disk");
        disk_menu->Append(Disk_Format,  "&Format Disk...",  "Format existing virtual disk");

        const bool disk_realtime = system2200::isDiskRealtime();
        disk_menu->AppendSeparator();
        disk_menu->Append(Disk_Realtime,         "Realtime Disk Speed",  "Emulate actual disk timing",             wxITEM_CHECK);
        disk_menu->Append(Disk_UnregulatedSpeed, "Unregulated Speed",    "Make disk accesses as fast as possible", wxITEM_CHECK);
        disk_menu->Check(Disk_Realtime,          disk_realtime);
        disk_menu->Check(Disk_UnregulatedSpeed, !disk_realtime);
    }

    // ----- configure ---------------------------------
    int ConfigMenuPos = m_menubar->FindMenu("Configure");
    if (menu == m_menubar->GetMenu(ConfigMenuPos)) {
        m_menubar->Check(Configure_KeywordMode, getKeywordMode());
        m_menubar->Check(Configure_SF_toolBar,  m_toolbar->IsShown());
        if (isPrimaryCrt()) {
            m_menubar->Check(Configure_Stats,   getShowStatistics());
        }
        if (system2200::getKbIoAddr(1) >= 0) {
            // there is more than one keyboard
            for (int i=0; ; i++) {
                const int addr = system2200::getKbIoAddr(i);
                if (addr < 0) {
                    break;
                }
                m_menubar->Check(Configure_KB_Tie0+i, (m_assoc_kb_addr == addr));
            }
        }
    }
}


// create the 16 SF keys and the EDIT key on the toolbar
void
CrtFrame::initToolBar(wxToolBar *tb)
{
#ifdef __WXMAC__
    // OSX requires that toolbar icons be 32x32. the wxwidgets api allows
    // declaring a different size, but in the end it gets truncated and/or
    // stretched to 32x32, and the results aren't pretty.  this is combatted
    // two ways.  first, shorter strings are used; second, we keep trying
    // smaller fonts until one meets the requirements.
    for (int font_size=14; font_size>=8; font_size--) {
#else
    const int font_size = 8;
#endif

    wxFont key_font(font_size, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    assert(key_font != wxNullFont);
    //key_font.SetNoAntiAliasing();

    wxMemoryDC mem_dc;
    mem_dc.SetFont(key_font);
    wxBitmap tmpbm(1, 1);
    mem_dc.SelectObject(tmpbm); // wxmac requires it even before GetTextExtent()
    wxCoord textH, textW;
    mem_dc.GetTextExtent("SF15", &textW, &textH); // widest label w/o big buttons
#if BIG_BUTTONS
    std::string sf_labels[17] = {
        "", "", "", "", // SF0-3
        "", "", "", "", // SF4-7
#ifdef __WXMAC__
        "Erase",        // SF8
        "Del",          // SF9
        "Ins",          // SF10
        "---->",        // SF11
        "->",           // SF12
        "<-",           // SF13
        "<----",        // SF14
        "Rcl",          // SF15
#else
                        // SF0-3 pc   mac  mac  mac
                        // SF4-7 8pt  12pt 11pt 10pt
        "Erase",        // SF8   27   32   29   27
        "Delete",       // SF9   31   38   35   32
        "Insert",       // SF10  26   33   31   28
        "---->",        // SF11  18   27   25   22
        "->",           // SF12   9   13   12   11
        "<-",           // SF13   9   13   12   11
        "<----",        // SF14  18   27   25   22
        "Recall",       // SF15  30   33   31   28
#endif
        "",             // EDIT
    };

    if (m_smart_term) {
        sf_labels[4] = "End";
        sf_labels[5] = "v";
        sf_labels[6] = "^";
        sf_labels[7] = "Begin";
    }

    // see if any of the labels is wider than the SFxx string
    for (auto const &lab : sf_labels) {
        int width, height;
        mem_dc.GetTextExtent(lab, &width, &height);
        if (width > textW) {
            textW = width;
        }
        if (height > textH) {
            textH = height;
        }
    }
#endif  // BIG_BUTTONS

#ifdef __WXMAC__
    if (textW > 32) {
        continue;       // try next smaller font size
    }
    const int buttonH(32);
    const int buttonW(32);
#else
  #if BIG_BUTTONS
    const int buttonH(2*textH);
  #else
    const int buttonH(textH);
  #endif
    const int buttonW(textW);
#endif

    tb->SetToolBitmapSize(wxSize(buttonW, buttonH));

    wxColor fg, bg;
    fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
    bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

    wxBitmap img(buttonW, buttonH, -1);
    wxPen fg_pen(fg, 1, wxPENSTYLE_SOLID);
    wxPen bg_pen(bg, 1, wxPENSTYLE_SOLID);

    mem_dc.SelectObject(img);
    mem_dc.SetBrush(wxBrush(bg, wxBRUSHSTYLE_SOLID));
    mem_dc.SetBackgroundMode(wxSOLID);
    mem_dc.SetTextForeground(fg);
    mem_dc.SetTextBackground(bg);
    mem_dc.SelectObject(wxNullBitmap);

    for (int i=0; i < 17; i++) {
        wxString label, tooltip;
        if (i < 16) {
            label.Printf("SF%d", i);
            tooltip.Printf("Special Function key %d", i);
        } else {
            tb->AddSeparator();
            label = "EDIT";
            tooltip = "EDIT key";
        }

        // print a horizontally centered label on the button bitmap
        m_sf_key_icons[i] = wxBitmap(buttonW, buttonH, -1);
        mem_dc.SelectObject(m_sf_key_icons[i]);
        mem_dc.SetPen(bg_pen);
        mem_dc.DrawRectangle(0, 0, buttonW, buttonH); // clear it

        mem_dc.GetTextExtent(label, &textW, &textH);
        int label_x_offset = (buttonW - textW) >> 1;  // center it
#if !BIG_BUTTONS
        mem_dc.DrawText(label, label_x_offset, 0);
#else // BIG_BUTTONS
        // there are two rows of text on the button.  the button can be viewed as
        //           |  gap
        //           |  textH  (upper text)
        //  buttonH  |  gap
        //           |  textH  (lower text)
        //           |  gap
        const int gap = (buttonH - 2*textH) / 3;
        const int upper_text_y = gap;
        const int lower_text_y = buttonH - gap - textH;
        assert(gap >= 0);  // always 0 on msw; >0 on osx

        // this is the lower text on the label (SF%d, or "EDIT")
#if __WXMAC__
        mem_dc.DrawText(label, label_x_offset, lower_text_y);
#else
        mem_dc.DrawText(label, label_x_offset, lower_text_y);
#endif

        // now draw the upper text
        mem_dc.GetTextExtent(sf_labels[i], &textW, &textH);
        label_x_offset = (buttonW - textW) >> 1;  // center it

        // for some s.f. keys, put the edit mode legend above
    #if GRAPHIC_ARROWS
        bool vertical = false;  // false=horizontal
        int shaft_ticks = 0;    // number of separators
        int arrow_dir   = 0;    // arrow direction (+1 = right or down)
        switch (i) {
            case 5: // down arrow
                vertical = true;
                arrow_dir = +1;
                break;
            case 6: // up arrow
                vertical = true;
                arrow_dir = -1;
                break;
            case 11: // ---->
                shaft_ticks = 4;
                arrow_dir = +1;
                break;
            case 12: // ->
                shaft_ticks = 1;
                arrow_dir = +1;
                break;
            case 13: // <-
                shaft_ticks = 1;
                arrow_dir = -1;
                break;
            case 14: // <----
                shaft_ticks = 4;
                arrow_dir = -1;
                break;
            default:
                break;
        }
        if (m_smart_term && vertical) {

            wxPen pen(fg, 1, wxPENSTYLE_SOLID);
            const int shaft_len    = buttonH/2;
            const int shaft_x      = buttonW/2;
            const int shaft_beg_y  = buttonH/4 - 2*(arrow_dir * shaft_len)/7;
            const int shaft_end_y  = buttonH/4 + 2*(arrow_dir * shaft_len)/7;
            const int head_delta_y = -arrow_dir * shaft_len/4;
            const int head_delta_x =              shaft_len/4; // make it 45 degrees

            // draw shaft
            mem_dc.SetPen(fg_pen);
            mem_dc.DrawLine(shaft_x, shaft_beg_y, shaft_x, shaft_end_y);

            // draw arrowhead
            mem_dc.DrawLine(shaft_x, shaft_end_y,
                            shaft_x + head_delta_x,
                            shaft_end_y + head_delta_y);
            mem_dc.DrawLine(shaft_x, shaft_end_y,
                            shaft_x - head_delta_x,
                            shaft_end_y + head_delta_y);

        } else if (shaft_ticks > 0) {

            wxPen dash_pen(fg, 1, wxPENSTYLE_SOLID);
            const wxDash dashes[] = { 4, 2 };
            int shaft_len = buttonW/5;
            if (shaft_ticks > 1) {
#if __WXMSW__
                // the USER_DASH style doesn't seem to work as I'd expect
                // (as of wxWidgets 2.6.0)
                dash_pen.SetStyle(wxPENSTYLE_USER_DASH);
                dash_pen.SetDashes(2, &dashes[0]);
#endif
                // four dashes, three spaces
                shaft_len = (shaft_ticks  )*dashes[0]
                          + (shaft_ticks-1)*dashes[1];
            }

            const int shaft_y      = gap + textH/2;
            const int shaft_beg_x  = buttonW/2   - (arrow_dir * shaft_len)/2;
            const int shaft_end_x  = shaft_beg_x + (arrow_dir * shaft_len);
            const int head_delta_x = -arrow_dir * dashes[0];
            const int head_delta_y =              dashes[0]; // make it 45 degrees

            // draw shaft
            mem_dc.SetPen(dash_pen);
            mem_dc.DrawLine(shaft_beg_x, shaft_y, shaft_end_x, shaft_y);

            // draw arrowhead
            mem_dc.SetPen(fg_pen);
            mem_dc.DrawLine(shaft_end_x, shaft_y,
                            shaft_end_x + head_delta_x,
                            shaft_y + head_delta_y);
            mem_dc.DrawLine(shaft_end_x, shaft_y,
                            shaft_end_x + head_delta_x,
                            shaft_y - head_delta_y);
        } else
    #endif  // GRAPHIC_ARROWS
        {
            mem_dc.DrawText(sf_labels[i], label_x_offset, upper_text_y);
        }
#endif  // BIG_BUTTONS

        // N.B.: apparently the tooltip doesn't appear on the osx port
        tb->AddTool(TB_SF0+i, label, m_sf_key_icons[i], tooltip);
        mem_dc.SelectObject(wxNullBitmap);
    } // for (i)
    #ifdef __WXMAC__
        break;  // we found a font size that works
    } // for (font_size)
    #endif

    tb->Realize();
}


static std::string
makeCrtIniGroup(bool smart_term, int io_addr, int term_num)
{
    std::ostringstream sg;
    if (smart_term) {
        // eg: ui/MXD-00-1/...  (MXD at addr 00, terminal 1)
        // note: internall term_num is 0-based, but the ini is 1-based because
        // the wang documentation calls the terminals 1 to 4.
        sg << "ui/MXD-CRT-" << std::setw(2) << std::setfill('0') << std::hex << io_addr
                            << "-" << std::setw(1) << (term_num+1);
    } else {
        // eg: ui/CRT-05/...  (dumb crt at addr 05)
        sg << "ui/CRT-" << std::setw(2) << std::setfill('0') << std::hex << io_addr;
    }
    return sg.str();
}


// save Crt options to the config file
void
CrtFrame::saveDefaults()
{
    std::string subgroup = makeCrtIniGroup(m_smart_term, m_crt_addr, m_term_num);

    // save screen color
    host::configWriteInt(subgroup, "colorscheme", getDisplayColorScheme());

    // save font choice
    host::configWriteInt(subgroup, "fontsize",  m_font_size[0]);
    host::configWriteInt(subgroup, "fontsize2", m_font_size[1]);

    // save contrast/brightness
    host::configWriteInt(subgroup, "contrast",   m_crt->getDisplayContrast());
    host::configWriteInt(subgroup, "brightness", m_crt->getDisplayBrightness());

    // save keyword mode
    host::configWriteBool(subgroup, "keywordmode", getKeywordMode());

    // save tied keyboard io address
    std::ostringstream tfoo;
    tfoo << "0x" << std::setw(2) << std::setfill('0') << std::uppercase << std::hex << m_assoc_kb_addr;
    std::string foo(tfoo.str());
    host::configWriteStr(subgroup, "tied_keyboard", foo);

    // save position and size
    if (!m_fullscreen) {
        host::configWriteWinGeom(this, subgroup);
    }

    // save statistics display mode
    host::configWriteBool(subgroup, "timingstats", getShowStatistics());

    // save toolbar status
    host::configWriteBool(subgroup, "toolbar", m_toolbar->IsShown());

    // save fullscreen status
    host::configWriteBool(subgroup, "fullscreen", m_fullscreen);
}


// get Crt options from the config file, supplying reasonable defaults
void
CrtFrame::getDefaults()
{
    std::string subgroup = makeCrtIniGroup(m_smart_term, m_crt_addr, m_term_num);

    // pick up keyword mode (A/a vs Keyword/A)
    bool b;
    host::configReadBool(subgroup, "keywordmode", &b, false);
    setKeywordMode(b);

    // pick up tied keyboard io address
    int default_kb_addr = 0x01;
    if (m_smart_term) {
        switch (m_crt_addr) {
            case 0x00: default_kb_addr = 0x01; break;
            case 0x40: default_kb_addr = 0x41; break;
            case 0x80: default_kb_addr = 0x81; break;
            case 0xC0: default_kb_addr = 0xC1; break;
            default: assert(false);
        }
    }
    int v = 0;
    b = host::configReadInt(subgroup, "tied_keyboard", &v);
    if (b && (v >= 0x00) && (v <= 0xFF)) {
        m_assoc_kb_addr = v;
    }  else {
        m_assoc_kb_addr = default_kb_addr;
    }

    // make sure that old mapping still makes sense
    bool found = false;
    for (int i=0; i < NUM_IOSLOTS; i++) {
        if (system2200::getKbIoAddr(i) == m_assoc_kb_addr) {
            found = true;
            break;
        }
    }
    if (!found) {
        m_assoc_kb_addr = default_kb_addr;
    }

    // pick up statistics display mode
    bool show_stats;
    host::configReadBool(subgroup, "timingstats", &show_stats, false);
    setShowStatistics(show_stats);

    // save toolbar status
    bool show_toolbar;
    host::configReadBool(subgroup, "toolbar", &show_toolbar, false);
    m_toolbar->Show(show_toolbar);

    // pick up screen location and size
    wxRect default_geom(50, 50, 680, 380);  // assume 64x16; x,y,w,h
    if (!m_small_crt) {
        default_geom = {50, 50, 840, 560};  // 80x24
    }
    host::configReadWinGeom(this, subgroup, &default_geom);

    // pick up fullscreen status
    host::configReadBool(subgroup, "fullscreen", &m_fullscreen, false);

    // this must be done before changing the color scheme
    host::configReadInt(subgroup, "contrast", &v, 100);
    m_crt->setDisplayContrast(v);
    host::configReadInt(subgroup, "brightness", &v, 0);
    m_crt->setDisplayBrightness(v);

    host::configReadInt(subgroup, "colorscheme", &v, 0);
    if ((v >= 0) && (v < num_colorschemes)) {
        setDisplayColorScheme(v);
    } else {
        setDisplayColorScheme(0);
    }

    // pick up screen font size
    m_font_size[0] = m_font_size[1] = 2; // default
    b = host::configReadInt(subgroup, "fontsize", &v);
    if (b && ((v >=1 && v <= 3) || (v >= 8 && v <= 28))) {
        m_font_size[0] = v;
    }
    host::configReadInt(subgroup, "fontsize2", &v);
    if (b && ((v >=1 && v <= 3) || (v >= 8 && v <= 28))) {
        m_font_size[1] = v;
    }

    m_crt->setFontSize(m_font_size[m_fullscreen]);
}


// make Crt the focus of further keyboard events
void
CrtFrame::refocus()
{
    m_crt->SetFocus();
}


// set simulation time for informative display
void
CrtFrame::setSimSeconds(int secs, float relative_speed)
{
    CrtFrame *pf = getPrimaryFrame();
    if (pf == nullptr) {
        return;
    }

    wxString str;
#if 0
    // too nerdy?
    wxString format = (relative_speed >= 10.0f) ?
                      "Sim time: %d seconds, %3.0fx, %d fps"
                    : "Sim time: %d seconds, %3.1fx, %d fps";
    str.Printf(format, secs, relative_speed, pf->m_fps);
#else
    wxString format = (relative_speed >= 10.0f) ?
                      "Sim time: %d seconds, %3.0fx"
                    : "Sim time: %d seconds, %3.1fx";
    str.Printf(format, secs, relative_speed);
#endif
    if (pf->getShowStatistics()) {
        pf->m_statusbar->SetStatusMessage(std::string(str));
    } else {
        pf->m_statusbar->SetStatusMessage("");
    }
}


// 2336: there is 2b counter
//        text   cursor
//   00:  norm     on
//   01:  bright   on
//   10:  norm     on
//   11:  bright   off
bool
CrtFrame::getTextBlinkPhase() const noexcept
{
    return (m_blink_phase & 1) == 1;
}


bool
CrtFrame::getCursorBlinkPhase() const noexcept
{
    // I believe the 2236 had a 50% duty cycle,
    // but the 2336 definitely has a 75% duty cycle
    return (m_blink_phase < 3);
}


// create a bell (0x07) sound
void
CrtFrame::ding()
{
    m_crt->ding();
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------

// tell the emulator to accept keystrokes from a file
void
CrtFrame::OnScript(wxCommandEvent& WXUNUSED(event))
{
    std::string full_path;
    const int r = host::fileReq(host::FILEREQ_SCRIPT, "Script to execute", true, &full_path);
    if (r == host::FILEREQ_OK) {
        // tell the core emulator to redirect keyboard input from a file
        system2200::invokeKbScript(m_assoc_kb_addr, m_term_num, full_path);
    }
}


// do a screen capture to a named filed
void
CrtFrame::OnSnapshot(wxCommandEvent& WXUNUSED(event))
{
    // get the name of a file to execute
    std::string full_path;

    const int r = host::fileReq(host::FILEREQ_GRAB, "Filename of image", false, &full_path);
    if (r == host::FILEREQ_OK) {
        const wxBitmap* bitmap = m_crt->grabScreen();
        assert(bitmap != nullptr);
        bitmap->SaveFile(wxString(full_path), wxBITMAP_TYPE_BMP);
    }
}


#if HAVE_FILE_DUMP
// do a screen capture to a named filed
void
CrtFrame::OnDump(wxCommandEvent& WXUNUSED(event))
{
    // get the name of a file to execute
    std::string full_path;
    int r = host::fileReq(host::FILEREQ_GRAB, "Name of file to save to", false, &full_path);

    if (r == host::FILEREQ_OK) {
        dumpRam(full_path);
    }
}
#endif


// called when File/Exit is selected
void
CrtFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    system2200::terminate(); // shut down all windows and exit
}


void
CrtFrame::OnReset(wxCommandEvent &event)
{
    switch (event.GetId()) {
        default:
            assert(false);
        case CPU_HardReset:
            system2200::reset(true);  // hard reset
            break;
        case CPU_WarmReset:
            // route it through the keyboard handler because the MXD
            // filters out resets which aren't from terminal #1
            system2200::dispatchKeystroke(getTiedAddr(), m_term_num,
                                          IoCardKeyboard::KEYCODE_RESET);
            break;
    }
}


void
CrtFrame::OnCpuSpeed(wxCommandEvent &event)
{
    system2200::regulateCpuSpeed((event.GetId() == CPU_ActualSpeed));
}


void
CrtFrame::OnDiskFactory(wxCommandEvent &event)
{
    std::string filename;
    if (event.GetId() == Disk_Inspect) {
        if (host::fileReq(host::FILEREQ_DISK, "Virtual Disk Name", true, &filename) !=
                          host::FILEREQ_OK) {
            return;     // canceled
        }
    }

    doInspect(filename);
}


// inspect the named disk.  if it is mounted in a drive, disconnect the
// filehandle before performing the operation so that when the emulator
// resumes, it will be forced to reopen the file, picking up any changes
// made to the disk metadata.
void
CrtFrame::doInspect(const std::string &filename)
{
    system2200::freezeEmu(true);    // halt emulation

    int slot, drive;
    const bool in_use = system2200::findDisk(filename, &slot, &drive, nullptr);
    if (in_use) {
        // close filehandles to the specified drive
        IoCardDisk::wvdFlush(slot, drive);
    }

    DiskFactory dlg(this, filename);
    dlg.ShowModal();

    system2200::freezeEmu(false);   // run emulation
}


void
CrtFrame::OnDiskFormat(wxCommandEvent& WXUNUSED(event))
{
    std::string filename;
    if (host::fileReq(host::FILEREQ_DISK, "Virtual Disk Name", true, &filename) !=
                      host::FILEREQ_OK) {
        return; // cancelled
    }

    doFormat(filename);
}


void
CrtFrame::doFormat(const std::string &filename)
{
    system2200::freezeEmu(true);    // halt emulation

    bool wp;
    bool ok = IoCardDisk::wvdGetWriteProtect(filename, &wp);
    if (ok) {
        int slot, drive, io_addr;
        const bool in_use = system2200::findDisk(filename, &slot, &drive, &io_addr);

        wxString prompt = "";
        if (in_use) {
            prompt = wxString::Format("Warning: this disk is in use at /%03X, drive %d.\n\n",
                                        io_addr, drive);
        }
        if (wp) {
            prompt += "Warning: write protected disk!\n\n";
        }

        prompt += "Formatting will lose all disk contents.\n"
                  "Do you really want me to format the disk?";

        if (UI_confirm(prompt.c_str())) {
            if (in_use) {
                // close filehandles to the specified drive
                IoCardDisk::wvdFlush(slot, drive);
            }
            ok = IoCardDisk::wvdFormatFile(filename);
        }
    }

    if (!ok) {
        UI_error("Error: operation failed");
    }

    system2200::freezeEmu(false);   // run emulation
}


void
CrtFrame::OnDisk(wxCommandEvent &event)
{
    // each controller manages two drives, each has three possible actions
    const int menu_id = event.GetId();
    const int slot  =  (menu_id - Disk_Insert) / 8;
    const int drive = ((menu_id - Disk_Insert) % 8) / 2;
    const int type  =  (menu_id - Disk_Insert) % 2;
    // UI_info("Got disk menu: slot=%d, drive=%d, action=%d", slot, drive, type);

    bool ok = true;
    switch (type) {

        case 0: // insert disk
        {   std::string full_path;
            if (host::fileReq(host::FILEREQ_DISK, "Disk to load", true, &full_path) ==
                              host::FILEREQ_OK) {
                int drive2, io_addr2;
                const bool b = system2200::findDisk(full_path, nullptr, &drive2, &io_addr2);
                const int eff_addr = io_addr2 + ((drive2 < 2) ? 0x00 : 0x40);
                if (b) {
                    UI_warn("Disk already in drive %c /%03x", "FRFR"[drive2], eff_addr);
                    return;
                }
                ok = IoCardDisk::wvdInsertDisk(slot, drive, full_path);
            }
        }   break;

        case 1: // remove disk
            ok = IoCardDisk::wvdRemoveDisk(slot, drive);
            break;

#if 0
        case 2: // format disk
            if (UI_confirm("Do you really want me to format the disk in drive %d?", drive)) {
                ok = IoCardDisk::wvd_format(slot, drive);
            }
            break;
#endif

        default:
            ok = false;
            assert(false);
            break;
    }

    if (!ok) {
        UI_error("Error: operation failed");
    }
}


void
CrtFrame::OnDiskSpeed(wxCommandEvent &event)
{
    const bool realtime = (event.GetId() == Disk_Realtime);
    system2200::setDiskRealtime(realtime);
}


void
CrtFrame::OnDisplayFullscreen(wxCommandEvent& WXUNUSED(event))
{
    m_fullscreen = !m_fullscreen;
    ShowFullScreen(m_fullscreen, FULLSCREEN_FLAGS);
    m_crt->setFontSize(m_font_size[m_fullscreen]);
}


// called when the window is manually closed ("X" button, or sys menu)
void
CrtFrame::OnClose(wxCloseEvent& WXUNUSED(event))
{
    system2200::freezeEmu(true);
    system2200::terminate(); // shut down all windows and exit
}


// update all displays
void
CrtFrame::OnTimer(wxTimerEvent &event)
{
    if (event.GetId() == Timer_Frame) {
        m_crt->refreshWindow(); // ask screen to update
    } else if (event.GetId() == Timer_QSec) {
        // count frames each second to display FPS figure
        m_blink_phase = (m_blink_phase == 3) ? 0 : (m_blink_phase+1);
        if (m_blink_phase == 0) {
            m_fps = m_crt->getFrameCount();
            m_crt->setFrameCount(0);
        }
        // there might be blinking text or blinking cursor
        m_crt->setDirty();
    }
}

void
CrtFrame::OnConfigureDialog(wxCommandEvent& WXUNUSED(event)) noexcept
{
    system2200::reconfigure();
}


void
CrtFrame::OnConfigureScreenDialog(wxCommandEvent& WXUNUSED(event))
{
    wxString title;
    if (m_smart_term) {
        title.Printf("MXD/%02X, Term#%d Configuration", m_crt_addr, m_term_num+1);
    } else {
        title.Printf("Display /%3X Configuration", m_crt_addr);
    }

    std::string subgroup = makeCrtIniGroup(m_smart_term, m_crt_addr, m_term_num);

    system2200::freezeEmu(true);    // halt emulation

    CrtConfigDlg dlg(this, title, subgroup);
    dlg.ShowModal();

    system2200::freezeEmu(false);   // run emulation
}


void
CrtFrame::OnConfigureKeywordMode(wxCommandEvent& WXUNUSED(event))
{
    const bool state = m_statusbar->getKeywordMode();
    m_statusbar->setKeywordMode(!state);
}


void
CrtFrame::OnConfigureSfToolbar(wxCommandEvent& WXUNUSED(event))
{
    const bool state = m_toolbar->IsShown();
    m_toolbar->Show(!state);
    SendSizeEvent();
}


void
CrtFrame::OnConfigureStats(wxCommandEvent& WXUNUSED(event))
{
    if (isPrimaryCrt()) {
        const bool showing = getShowStatistics();
        setShowStatistics(!showing);
    }
}


void
CrtFrame::OnConfigureKbTie(wxCommandEvent &event)
{
    const int id = event.GetId();
    assert((id >= Configure_KB_Tie0) && (id <= Configure_KB_TieN));

    const int idx = id - Configure_KB_Tie0;
    const int new_addr = system2200::getKbIoAddr(idx);
    assert(new_addr >= 0);

    m_assoc_kb_addr = new_addr;
}


void
CrtFrame::OnPrinter(wxCommandEvent &event)
{
    const int id = event.GetId();
    assert((id >= Printer_0) && (id <= Printer_N));

    // map chosen device to an I/O address
    const int idx = id - Printer_0;
    const int io_addr = system2200::getPrinterIoAddr(idx);
    const IoCard *inst = system2200::getInstFromIoAddr(io_addr);
    assert(inst != nullptr);

    // get the printer controller card handle
    const IoCardPrinter *card = dynamic_cast<const IoCardPrinter*>(inst);
    assert(card != nullptr);
    PrinterFrame *prt_wnd = card->getGuiPtr();
    assert(prt_wnd != nullptr);

    prt_wnd->Show(true);
    prt_wnd->Raise();
}


// print all printer contents, and then clear all printers
void
CrtFrame::OnPrintAndClear(wxCommandEvent& WXUNUSED(event))
{
    // loop through each printer and ask it to print and clear its contents.
    // the clear should only be invoked if the print was successful,
    // otherwise a warning message should be displayed.
    if (system2200::getPrinterIoAddr(0) >= 0) {

        // there is at least one printer
        for (int i=0; ; i++) {
            const int io_addr = system2200::getPrinterIoAddr(i);
            if (io_addr < 0) {
                break; // no more printers
            }

            // map device I/O address to card handle
            const IoCard *inst = system2200::getInstFromIoAddr(io_addr);
            assert(inst != nullptr);
            const IoCardPrinter *card = dynamic_cast<const IoCardPrinter*>(inst);
            assert(card != nullptr);

            // fetch associated gui window pointer and use it
            PrinterFrame *prt_wnd = card->getGuiPtr();
            assert(prt_wnd != nullptr);
            prt_wnd->printAndClear();
        }
    }
}


// print all printer contents, and then clear all printers
void
CrtFrame::OnToolBarButton(wxCommandEvent &event)
{
    const int id = event.GetId();
    const bool shift = ::wxGetKeyState(WXK_SHIFT);

    const int sf = IoCardKeyboard::KEYCODE_SF;
    const int keycode = (id == TB_EDIT) ? (sf | IoCardKeyboard::KEYCODE_EDIT)
                      : (shift)         ? (sf | (id - TB_SF0 + 16))
                                        : (sf | (id - TB_SF0));

    system2200::dispatchKeystroke(getTiedAddr(), m_term_num, keycode);
}


void
CrtFrame::OnMenuOpen(wxMenuEvent &event)
{
    setMenuChecks(event.GetMenu());
}


// -------- allow discovery of possible font styles --------

int
CrtFrame::getNumFonts() noexcept
{
    return num_fonts;
}


// allow discovery of allowed values
// as idx ranges from 0 to n, return the font size constant.
int
CrtFrame::getFontNumber(int idx) noexcept
{
    assert(idx >= 0 && idx < num_fonts);
    return font_table[idx].size;
}


// as idx ranges from 0 to n, return the font name string.
std::string
CrtFrame::getFontName(int idx)
{
    assert(idx >= 0 && idx < num_fonts);
    return std::string(font_table[idx].name);
}

// -------- allow discovery of possible color schemes --------

int
CrtFrame::getNumColorSchemes() noexcept
{
    return num_colorschemes;
}


// as idx ranges from 0 to n, return the font name string.
std::string
CrtFrame::getColorSchemeName(int idx)
{
    assert(idx >= 0 && idx < num_colorschemes);
    return colorscheme[idx].help_label;
}

// -------- Crt display set/get --------

void
CrtFrame::setFontSize(int size)
{
    m_font_size[m_fullscreen] = size;
    // pass it through
    m_crt->setFontSize(size);
}


int
CrtFrame::getFontSize() const noexcept
{
    return m_font_size[m_fullscreen];
}


void
CrtFrame::setDisplayColorScheme(int n)
{
    assert(n >= 0);
    assert(n <  num_colorschemes);

    wxColor fg = wxColor(colorscheme[n].fg_r, colorscheme[n].fg_g, colorscheme[n].fg_b);
    wxColor bg = wxColor(colorscheme[n].bg_r, colorscheme[n].bg_g, colorscheme[n].bg_b);
    m_crt->setColor(fg, bg);
    // this is required if we are using deep font bitmaps to store the fontmap
    m_crt->setFontSize(m_font_size[m_fullscreen]);
    m_colorsel = n;
}


int
CrtFrame::getDisplayColorScheme() const noexcept
{
    return m_colorsel;
}


void
CrtFrame::setDisplayContrast(int n)
{
    m_crt->setDisplayContrast(n);
}


int
CrtFrame::getDisplayContrast() const noexcept
{
    return m_crt->getDisplayContrast();
}


void
CrtFrame::setDisplayBrightness(int n)
{
    m_crt->setDisplayBrightness(n);
}


int
CrtFrame::getDisplayBrightness() const noexcept
{
    return m_crt->getDisplayBrightness();
}


void
CrtFrame::setShowStatistics(bool show)
{
    m_show_stats = show;
    CrtFrame *pf = getPrimaryFrame();
    if ((pf != nullptr) && isPrimaryCrt()) {
        if (getShowStatistics()) {
            pf->m_statusbar->SetStatusMessage("(Performance statistics will appear here)");
        } else {
            pf->m_statusbar->SetStatusMessage("");
        }
    }
}


bool
CrtFrame::getShowStatistics() const noexcept
{
    return m_show_stats;
}


// set the keyword state from the statbar
void
CrtFrame::setKeywordMode(bool b)
{
    m_statusbar->setKeywordMode(b);
}


bool
CrtFrame::getKeywordMode() const
{
    return m_statusbar->getKeywordMode();
}


// the io address of the emulated keyboard associated with this window
int
CrtFrame::getTiedAddr() const noexcept
{
    return m_assoc_kb_addr;
}


int
CrtFrame::getTermNum() const noexcept
{
    return m_term_num;
}

// ----------------------------------------------------------------------------
//   Crt frame management functions
// ----------------------------------------------------------------------------

// remove self from the list of CRTs
void
CrtFrame::destroyWindow()
{
    saveDefaults();     // save config options

    // if this is the primary frame, forget about it now
    if (m_primary_frame == this) {
        m_primary_frame = nullptr;
    }

    // close this window (system may defer it for a while)
    Destroy();
}


// One distinguished CRT which has all the controls, eg, kind of the superuser.
// This primary Crt has the ability to change the system configuration, and to
// change which disk images are in use. Non-primary CRTs can change only local
// properties, like CRT phosphor color and font.
CrtFrame *
CrtFrame::getPrimaryFrame() noexcept
{
    return m_primary_frame;
}


// if the display has changed, updated it
void
CrtFrame::refreshWindow()
{
    // pass it through
    m_crt->refreshWindow();
}


// ----------------------------------------------------------------------------
//   when various disk state changes occur, pass them on to the
//   status bar of each Crt window.
// ----------------------------------------------------------------------------

// called when something changes about the specified disk
void
CrtFrame::diskEvent(int slot, int drive)
{
    CrtFrame *pf = getPrimaryFrame();
    if (pf != nullptr) {
        pf->m_statusbar->diskEvent(slot, drive);
    }
}

// vim: ts=8:et:sw=4:smarttab
