// this file implements the PrinterFrame class.
// it is the window that represents an instance of a view of a Printer,
// and handles all the user interaction.

// AREAS FOR IMPROVEMENT (in here and Printer)
// in roughly decreasing order of usefulness & sanity
// -- have a printer icon on the CrtFrame next to where the disk icons go?
//    It should have a few different states indicating:
//       (1) printer log is empty
//       (2) printer has something, but it hasn't changed since last viewed
//       (3) printer has something new since the last time it was viewed
//    Even if you aren't using the print and clear stuff, I think this is
//    useful information.
// -- currently, each formfeed character is replaced with the right number
//    of blank lines to advance to the top of the next page.  if the page
//    length is changed, then things are mucked up.  also, if we save the
//    print log to a file, it might be best if those original pagebreaks
//    were preserved.
//    one solution would be to leave the pagebreak character in the log file.
//    when the page length gets changed, the dummy empty lines would be
//    removed and then new ones generated based on the new pagelength.  when
//    the log gets saved to a file, the dummy lines aren't printed out.
// -- configuration is currently hardwired to be a generic printer.
//    should we have a list of printers that can be selected, each with
//    different attributes?  or is that overkill?
// -- real printers have linefeed, formfeed, and on-/off-line buttons.
//    is there any need to duplicate that here?
// -- double-wide character mode isn't supported.
//    I suppose we'd just keep the control characters in the strings and
//    as each line is generted, we'd check for those control characters
//    and break the text up into runs of normal and wide characters.
//    normal runs would go as currently they do (although hard tabs would
//    have to take into account that wide chars count double).  then for
//    the runs of wide characters, we'd print each run into a small memory
//    buffer that would then get converted to a wxImage, then we'd use the
//    Rescale() function to double its width, then we'd paste that back
//    into the output DC at the right position.
// -- overstrike isn't supported.
//    all that is required is to interpret the HEX(08) characters as
//    appropriate, and also to distinguish a carriage return as just
//    resetting the output coordinate instead of also forcing a line feed
//    (research a bit to see who does the injection of the line feed)
// -- the timing of the printer isn't emulated.  is there any reason why
//    someone would want that duplicated?  it would have to go into
//    IoCardPrinter.cpp.


// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <sstream>
using std::ostringstream;
#include <iomanip>
using std::hex;
using std::setw;
using std::setfill;

#include "Host.h"
#include "System2200.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiPrinter.h"
#include "UiPrinterFrame.h"
#include "UiPrinterConfigDlg.h"

#include "wx/printdlg.h"        // print preview interfaces
#include "wx/paper.h"           // pick up paper database
                                // (for translation of enums - see PaperSize)

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
    // menu items
    File_Close = wxID_EXIT,
    File_SaveAs = wxID_SAVEAS,
    File_PrintClear = 1,
    File_PrintPreview,
    File_Print,
    File_PrintSetup,
    File_PageSetup,

    Display_FontSize8,
    Display_FontSize10,
    Display_FontSize12,
    Display_FontSize14,
    Display_FontSize18,

    Display_Greenbar,

    Configure_Dialog,
};

#if USEMYPAPER

// map of enums for some of the paper sizes (wxPaperSize). Used for load/save to config file
// not all are implemented. See exPaperSize in defs.h for a complete list/
struct papersizemap_t {
    wxPaperSize papersizeval;
    string papersizename;
} papersizemap[] = {
    {wxPAPER_NONE,       "NONE" },         /*  Use specific dimensions */
    {wxPAPER_LETTER,     "LETTER" },       /*  Letter, 8 1/2 by 11 inches */
    {wxPAPER_LEGAL,      "LEGAL" },        /*  Legal, 8 1/2 by 14 inches */
    {wxPAPER_A4,         "A4" },           /*  A4 Sheet, 210 by 297 millimeters */
    {wxPAPER_A3,         "A3" },           /*  A3 sheet, 297 by 420 millimeters */
    {wxPAPER_10X14,      "10X14" },        /*  10-by-14-inch sheet */
    {wxPAPER_11X17,      "11x17" },        /*  11-by-17-inch sheet */
    {wxPAPER_FANFOLD_US, "US Std Fanfold"} /*  US Std Fanfold, 14 7/8 by 11 inches */   
};
const int PSMAX = sizeof(papersizemap)/sizeof(papersizemap_t);
#endif

// map of enums for some of the paper bins (wxPrintBin). Used for load/save to config file
struct paperbinmap_t {
    wxPrintBin paperbinval;
    string paperbinname;
} paperbinmap[] = {
    {wxPRINTBIN_DEFAULT,        "DEFAULT" },
    {wxPRINTBIN_ONLYONE,        "ONLYONE" },
    {wxPRINTBIN_LOWER,          "LOWER" },
    {wxPRINTBIN_MIDDLE,         "MIDDLE" },
    {wxPRINTBIN_MANUAL,         "MANUAL" },
    {wxPRINTBIN_ENVELOPE,       "ENVELOPE" },
    {wxPRINTBIN_ENVMANUAL,      "ENVMANUAL" },
    {wxPRINTBIN_AUTO,           "AUTO" },
    {wxPRINTBIN_TRACTOR,        "TRACTOR" },
    {wxPRINTBIN_SMALLFMT,       "SMALLFMT" },
    {wxPRINTBIN_LARGEFMT,       "LARGEFMT" },
    {wxPRINTBIN_LARGECAPACITY,  "LARGECAPACITY" },
    {wxPRINTBIN_CASSETTE,       "CASSETTE" },
    {wxPRINTBIN_FORMSOURCE,     "FORMSOURCE" },
    {wxPRINTBIN_USER,           "USER" }
};
const int PBMAX = sizeof(paperbinmap)/sizeof(paperbinmap_t);

// ----------------------------------------------------------------------------
// PrinterFrame
// ----------------------------------------------------------------------------

// connect the wxWindows events with the functions which process them
BEGIN_EVENT_TABLE(PrinterFrame, wxFrame)

    EVT_MENU      (File_Close,            PrinterFrame::OnFileClose)
    EVT_MENU      (File_SaveAs,           PrinterFrame::OnFileSaveAs)
    EVT_MENU      (File_PrintClear,       PrinterFrame::OnPrintClear)
    EVT_MENU      (File_PrintPreview,     PrinterFrame::OnPrintPreview)
    EVT_MENU      (File_Print,            PrinterFrame::OnPrint)
    EVT_MENU      (File_PrintSetup,       PrinterFrame::OnPrintSetup)
    EVT_MENU      (File_PageSetup,        PrinterFrame::OnPageSetup)

    EVT_MENU      (Display_FontSize8,     PrinterFrame::OnFontSize)
    EVT_MENU      (Display_FontSize10,    PrinterFrame::OnFontSize)
    EVT_MENU      (Display_FontSize12,    PrinterFrame::OnFontSize)
    EVT_MENU      (Display_FontSize14,    PrinterFrame::OnFontSize)
    EVT_MENU      (Display_FontSize18,    PrinterFrame::OnFontSize)
    EVT_MENU      (Display_Greenbar,      PrinterFrame::OnDisplayGreenbar)

    EVT_MENU      (Configure_Dialog,      PrinterFrame::OnConfigureDialog)

    // non-menu event handlers
    EVT_MENU_OPEN (PrinterFrame::OnMenuOpen)
    EVT_CLOSE     (PrinterFrame::OnClose)

    // help menu items do whatever they need to do
    HELP_MENU_EVENT_MAPPINGS()

END_EVENT_TABLE()


// constructor
PrinterFrame::PrinterFrame(const wxString& title, const int io_addr) :
        wxFrame((wxFrame *)nullptr, -1, title, wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_FRAME_STYLE | wxNO_FULL_REPAINT_ON_RESIZE),
        m_menuBar(nullptr),
        m_statusBar(nullptr),
        m_printer(nullptr),
        m_fontsize(12),
        m_printer_addr(-1),
        m_previewzoom(25),
        m_printData(nullptr),
        m_pageSetupData(nullptr)
{
#ifndef __WXMAC__
    // set the frame icon
    SetIcon(wxICON(wang));
#endif

    makeMenubar();      // create menubar
    makeStatusbar();    // create status bar

    m_printer_addr = io_addr;   // we use this later during configuration

    //Printer support
    m_printData = new wxPrintData;
    m_pageSetupData = new wxPageSetupDialogData;

    m_printer = new Printer(this);
    getDefaults();      // get configuration options, or supply defaults
    setMenuChecks();    // might need to disable some menu items

    setupRealPrinter();

    System2200().freezeEmu(false);
}


// destructor
PrinterFrame::~PrinterFrame()
{
    if (m_printer->getPrintasgo())
        printAndClear();        // print anything left in the printer
    delete m_printer;           m_printer = nullptr;
    delete m_pageSetupData;     m_pageSetupData = nullptr;
    delete m_printData;         m_printData = nullptr;
}


// create menubar
void
PrinterFrame::makeMenubar()
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(File_Close,         "Close\tCtrl+W",   "Close the printer view");
    menuFile->AppendSeparator();
    menuFile->Append(File_SaveAs,        "Save As\tCtrl+S", "Save the contents of the printer to a file");
    menuFile->AppendSeparator();
    menuFile->Append(File_PrintClear,    "Clear printer",   "Clear the printer");
    menuFile->Append(File_PrintPreview,  "Print preview",   "Preview the contents of the printer");
    menuFile->Append(File_Print,         "Print\tCtrl+P",   "Print the contents of the printer");  //PHETODO alt-p?

    wxMenu *menuDisplay = new wxMenu;
    menuDisplay->Append(Display_FontSize8,  "Font Size  8", "Set display font to  8 point", wxITEM_CHECK);
    menuDisplay->Append(Display_FontSize10, "Font Size 10", "Set display font to 10 point", wxITEM_CHECK);
    menuDisplay->Append(Display_FontSize12, "Font Size 12", "Set display font to 12 point", wxITEM_CHECK);
    menuDisplay->Append(Display_FontSize14, "Font Size 14", "Set display font to 14 point", wxITEM_CHECK);
    menuDisplay->Append(Display_FontSize18, "Font Size 18", "Set display font to 18 point", wxITEM_CHECK);
    menuDisplay->AppendSeparator();
    menuDisplay->Append(Display_Greenbar,   "&Greenbar", "Make greenbar virtual paper", wxITEM_CHECK);
 
    wxMenu *menuConfig = new wxMenu;
    menuConfig->Append(Configure_Dialog, "&Emulated printer setup...", "Change settings of emulated Wang printer");
    menuConfig->Append(File_PageSetup,   "&Real printer setup...",     "Change settings of real printer");

    // make the help menu (as if it isn't obvious below!)
    wxMenu *menuHelp = TheApp::makeHelpMenu();

    // now append the freshly created menu to the menu bar...
    m_menuBar = new wxMenuBar;
    m_menuBar->Append(menuFile,    "&File");
    m_menuBar->Append(menuDisplay, "&Display");
    m_menuBar->Append(menuConfig,  "&Configure");
    m_menuBar->Append(menuHelp,    "&Help");

    // ... and attach this menu bar to the frame
    SetMenuBar(m_menuBar);
}


// create statusbar
void
PrinterFrame::makeStatusbar()
{
    m_statusBar = CreateStatusBar();
}


// set up various properties of the real printer
void
PrinterFrame::setupRealPrinter()
{
#if 0
    // trigger a load of default information
    m_pageSetupData->SetDefaultInfo(false);
#endif

    // now set up specifics

    // margins
    int left, right, top, bottom;
    m_printer->getMargins(left, right, top, bottom);
    wxPoint point(left, top);
    wxPoint point2(right, bottom);
    m_pageSetupData->SetMarginTopLeft(point);
    m_pageSetupData->SetMarginBottomRight(point2);

    // real printer name
    m_printData->SetPrinterName(m_printer->getRealPrinterName());

    // page orientation
    m_printData->SetOrientation(m_printer->getOrientation());

    // paper id
    m_printData->SetPaperId(m_printer->getPaperId());

    // paper bin
    m_printData->SetBin(m_printer->getBin());
}


// this is called just before a menu is displayed.
// set the check status for each of the menu items.
// also dynamically disables/enables menu items,
void
PrinterFrame::setMenuChecks()
{
    // ----- display -----------------------------------
    bool greenbar = m_printer->getGreenbar();
    m_menuBar->Check(Display_Greenbar, greenbar);

    m_menuBar->Check(Display_FontSize8,  m_fontsize ==  8);
    m_menuBar->Check(Display_FontSize10, m_fontsize == 10);
    m_menuBar->Check(Display_FontSize12, m_fontsize == 12);
    m_menuBar->Check(Display_FontSize14, m_fontsize == 14);
    m_menuBar->Check(Display_FontSize18, m_fontsize == 18);
}


// save Printer options to the config file
void
PrinterFrame::saveDefaults()
{
    Host hst;

    ostringstream sg;
    sg << "ui/Printer-" << setw(2) << setfill('0') << hex << m_printer_addr;
    string subgroup(sg.str());

    int left, right, top, bottom;
    m_printer->getMargins(left, right, top, bottom);

    int plen, llen;
    m_printer->getPageAttributes(llen, plen);

    // save position and size
    hst.ConfigWriteWinGeom(this, subgroup);

    // save page attributes
    hst.ConfigWriteInt(subgroup,  "pagelength",       plen);
    hst.ConfigWriteInt(subgroup,  "linelength",       llen);
    hst.ConfigWriteInt(subgroup,  "fontsize",         m_fontsize);
    hst.ConfigWriteBool(subgroup, "greenbar",         m_printer->getGreenbar() );
    hst.ConfigWriteBool(subgroup, "autoshow",         m_printer->getAutoshow() );
    hst.ConfigWriteBool(subgroup, "printasgo",        m_printer->getPrintasgo() );
    hst.ConfigWriteBool(subgroup, "portdirect",       m_printer->getPortdirect() );
    hst.ConfigWriteStr(subgroup,  "portstring",       m_printer->getPortstring() );
    hst.ConfigWriteInt(subgroup,  "orientation",      static_cast<int>(m_printer->getOrientation()) );
    hst.ConfigWriteStr(subgroup,  "papername",        m_printer->getPaperName() );
    hst.ConfigWriteInt(subgroup,  "paperid",          m_printer->getPaperId() );
    hst.ConfigWriteInt(subgroup,  "paperbin",         m_printer->getBin() );
    hst.ConfigWriteInt(subgroup,  "marginleft",       left);
    hst.ConfigWriteInt(subgroup,  "marginright",      right);
    hst.ConfigWriteInt(subgroup,  "margintop",        top);
    hst.ConfigWriteInt(subgroup,  "marginbottom",     bottom);
    hst.ConfigWriteInt(subgroup,  "previewzoom",      m_previewzoom);
    hst.ConfigWriteStr(subgroup,  "realprintername",  m_printer->getRealPrinterName() );
}


// get Printer options from the config file, supplying reasonable defaults
void
PrinterFrame::getDefaults()
{
    Host hst;
    string valstr;
    int v;
    bool b;

    // pick up screen color scheme
    ostringstream sg;
    sg << "ui/Printer-" << setw(2) << setfill('0') << hex << m_printer_addr;
    string subgroup(sg.str());

    // pick up screen location and size
    wxRect default_geom(50,50,690,380);
    hst.ConfigReadWinGeom(this, subgroup, &default_geom);

    // pick up screen font size
    b = hst.ConfigReadInt(subgroup, "fontsize", &v);
    m_fontsize = (b && (v >= 8) && (v <= 28)) ? v : 12;
    m_printer->setFontSize(m_fontsize);

    hst.ConfigReadBool(subgroup, "greenbar", &b, true);
    m_printer->setGreenbar(b);

    // pick up page attributes
    int plen, llen;
    (void)hst.ConfigReadInt(subgroup, "pagelength", &plen, 66);
    (void)hst.ConfigReadInt(subgroup, "linelength", &llen, 80);
    m_printer->setPageAttributes(llen, plen);

    // pick up autoshow attribute
    hst.ConfigReadBool(subgroup, "autoshow", &b, true);
    m_printer->setAutoshow(b);

    // pick up printasgo attribute
    hst.ConfigReadBool(subgroup, "printasgo", &b, false);
    m_printer->setPrintasgo(b);

    // pick up portdirect attribute
    hst.ConfigReadBool(subgroup, "portdirect", &b, false);
    m_printer->setPortdirect(b);

    // pick up portstring attribute
    string portstring("LPT1");
    b = hst.ConfigReadStr(subgroup, "portstring", &portstring);
    m_printer->setPortstring(portstring);

    // pick up page margins
    int left, right, top, bottom;
    (void)hst.ConfigReadInt(subgroup, "margintop",    &top,    50);
    (void)hst.ConfigReadInt(subgroup, "marginbottom", &bottom, 50);
    (void)hst.ConfigReadInt(subgroup, "marginleft",   &left,   50);
    (void)hst.ConfigReadInt(subgroup, "marginright",  &right,  50);
    m_printer->setMargins(left, right, top, bottom);

    // pick up page preview zoom factor
    (void)hst.ConfigReadInt(subgroup, "previewzoom", &m_previewzoom, 70);

    // pick up orientation
    int orientation;
    (void)hst.ConfigReadInt(subgroup, "orientation", &orientation, wxPORTRAIT);
    m_printer->setOrientation(static_cast<wxPrintOrientation>(orientation));
 
    // pick up paper id
    // we don't actually use the paperid that was saved to the config file but
    // instead we recalc the paperid from the papername. The papername is what
    // is important the paperid is an enum that might change at some point.
    wxPaperSize paperid = wxPAPER_NONE;
    b = hst.ConfigReadStr(subgroup, "papername", &valstr);
    if (b)
        paperid = PaperSize(valstr);
    string papername(PaperSize(paperid));
    if (papername.empty()) {
        // we did not find a match. use none
        paperid = wxPAPER_NONE;
        papername = PaperSize(paperid);
    }
    m_printer->setPaperId(paperid);
    m_printer->setPaperName(papername);

    // pick up paper bin
    wxPrintBin paperbin = wxPRINTBIN_DEFAULT;
    b = hst.ConfigReadStr(subgroup, "paperbin", &valstr);
    if (b)
        paperbin = PaperBin(valstr);
    m_printer->setBin(paperbin);

    // pick up printer name
    string printername("");
    b = hst.ConfigReadStr(subgroup, "realprintername", &printername);
    m_printer->setRealPrinterName(printername);
}

// translate a character pagesize to the appropriate enum value for wxPaperSize
wxPaperSize 
PrinterFrame::PaperSize(string pagesizename)
{
    // translate char to wxPaperSize
#if USEMYPAPER
    for (int i=0; i< PSMAX; i++) {
        if (papersizemap[i].papersizename == pagesizename) {
            return papersizemap[i].papersizeval;
        }
    }

    return wxPAPER_NONE;  // default
#else
    return wxThePrintPaperDatabase->ConvertNameToId(pagesizename);
#endif
}

// translate an enum pagesize to the appropriate string name
string 
PrinterFrame::PaperSize(wxPaperSize papersizeval) const
{
    // translate char to wxPaperSize
#if USEMYPAPER
    for (int i=0; i< PSMAX; i++) {
        if (papersizemap[i].papersizeval == papersizeval) {
            return papersizemap[i].papersizename;
        }
    }

    return "LETTER"; // default
#else
    return string(wxThePrintPaperDatabase->ConvertIdToName(papersizeval));
#endif
}

// translate a character pagesize to the appropriate enum value for wxPaperSize
wxPrintBin 
PrinterFrame::PaperBin(string paperbinname) const
{
    // translate char to wxPrintBin
    for (int i=0; i< PBMAX; i++) {
        if (paperbinmap[i].paperbinname == paperbinname) {
            return paperbinmap[i].paperbinval;
        }
    }

    return wxPRINTBIN_DEFAULT;  // default
}

// translate an enum pagesize to the appropriate string name
string 
PrinterFrame::PaperBin(wxPrintBin paperbinval) const
{
    // translate char to wxPaperSize
    for (int i=0; i< PBMAX; i++) {
        if (paperbinmap[i].paperbinval == paperbinval) {
            return paperbinmap[i].paperbinname;
        }
    }

    return "DEFAULT"; // default
}

// ---- event handlers ----

// called when File/Close is selected
void
PrinterFrame::OnFileClose(wxCommandEvent& WXUNUSED(event))
{
    Show(false);  // hide the window for now
}

// save the contents of the virtual printer
void
PrinterFrame::OnFileSaveAs(wxCommandEvent& WXUNUSED(event))
{
    System2200 sys;
    sys.freezeEmu(true);
    m_printer->saveToFile();
    sys.freezeEmu(false);
}

void
PrinterFrame::OnPrintClear(wxCommandEvent& WXUNUSED(event))
{
    System2200 sys;
    sys.freezeEmu(true);
    m_printer->printClear();
    sys.freezeEmu(false);
}


// called when the window is manually closed ("X" button, or sys menu)
void
PrinterFrame::PP_OnClose(wxCloseEvent &event)
{
    const string subgroup("ui/printpreview");
    Host().ConfigWriteWinGeom(this, subgroup, false);

    wxPreviewControlBar *controlBar = ((wxPreviewFrame*)this)->GetControlBar();
    PrinterFrame *parent = (PrinterFrame*)GetParent();
    parent->m_previewzoom = controlBar->GetZoomControl();

    event.Skip();
}


void
PrinterFrame::OnPrintPreview(wxCommandEvent& WXUNUSED(event))
{
    System2200 sys;
    sys.freezeEmu(true);

    //Pass two printout objects: for preview, and possible printing
    wxPrintDialogData printDialogData(*m_printData);
    wxPrintPreview *preview = new wxPrintPreview(
                                new Printout (_T(""), m_printer),   // preview
                                new Printout (_T(""), m_printer),   // printout
                                &printDialogData);
    if (!preview->IsOk()) {
        delete preview;
        wxMessageBox("There was a problem previewing.\n"
                     "Perhaps your current printer is not set correctly?",
                     "Previewing", wxOK);
        return;
    }

    string preview_title("Print Preview");
    wxPreviewFrame *frame = new wxPreviewFrame(preview, this, preview_title,
                                    wxPoint(100, 100),  // default position
                                    wxSize(600, 650));  // default size

    // works in 2.5.4 and subsequent releases
    frame->Connect( wxID_ANY, wxEVT_CLOSE_WINDOW,
                    wxCloseEventHandler(PrinterFrame::PP_OnClose) );

    const string subgroup("ui/printpreview");
    wxRect default_geom(100,100,600,650);
    Host().ConfigReadWinGeom(frame, subgroup, &default_geom, false);

    frame->Initialize();

    // it appears that these both have to be set -- setting one doesn't
    // automatically refresh the other
    frame->GetControlBar()->SetZoomControl(m_previewzoom);
    preview->SetZoom(m_previewzoom);

    frame->Show();

    sys.freezeEmu(false);
}


void
PrinterFrame::OnPrint(wxCommandEvent& WXUNUSED(event))
{
    System2200 sys;
    sys.freezeEmu(true);

    wxPrintDialogData printDialogData(* m_printData);
    wxPrinter printer(& printDialogData);

    Printout printout(_T(""), m_printer);
    if (!printer.Print(this, &printout, true)) {
        if (wxPrinter::GetLastError() == wxPRINTER_ERROR)
            wxMessageBox("There was a problem printing.\n"
                         "Perhaps your current printer is not set correctly?",
                         "Printing", wxOK);
        else
            wxMessageBox("Printing cancelled", "Printing", wxOK);
    } else {
        (*m_printData) = printer.GetPrintDialogData().GetPrintData();
    }

    sys.freezeEmu(false);
}


void
PrinterFrame::OnPrintSetup(wxCommandEvent& WXUNUSED(event))
{
    System2200 sys;
    sys.freezeEmu(true);

    wxPrintDialogData printDialogData(*m_printData);
    wxPrintDialog printerDialog(this, &printDialogData);

// SetSetupDialog(bool) deprecated since v2.5.4:
//  printerDialog.GetPrintDialogData().SetSetupDialog(true);
    printerDialog.ShowModal();

    (*m_printData) = printerDialog.GetPrintDialogData().GetPrintData();

    sys.freezeEmu(false);
}


void
PrinterFrame::OnPageSetup(wxCommandEvent& WXUNUSED(event))
{
    (*m_pageSetupData) = *m_printData;
    wxPageSetupDialog pageSetupDialog(this, m_pageSetupData);
    pageSetupDialog.ShowModal();

    (*m_printData) = pageSetupDialog.GetPageSetupData().GetPrintData();
    (*m_pageSetupData) = pageSetupDialog.GetPageSetupData();

    // (re)set margins
    wxPoint point, point2;
    point = m_pageSetupData->GetMarginTopLeft();
    point2 = m_pageSetupData->GetMarginBottomRight();
    m_printer->setMargins(point.x, point2.x, point.y, point2.y);

    // (re)set page orientation
    m_printer->setOrientation(m_printData->GetOrientation());

    // (re)set paper id
    m_printer->setPaperId(m_printData->GetPaperId());

    // (re)set paper name
    string papername(PaperSize(m_printer->getPaperId()));
    if (papername.empty()) {
        // we did not find a match. use letter, and issue warning
        m_printer->setPaperId(wxPAPER_NONE);
        papername = PaperSize(wxPAPER_NONE);
        //UI_Warn("Paper Type is not supported. Assuming None.");
    }
    m_printer->setPaperName(papername);

    // (re)set paper bin
    m_printer->setBin(m_printData->GetBin());

        // (re)set printer name
    m_printer->setRealPrinterName(string(m_printData->GetPrinterName()));

    // commit changes to config file
    saveDefaults();
}


void
PrinterFrame::OnFontSize(wxCommandEvent &event)
{
    int size;
    switch (event.GetId()) {
        case Display_FontSize8:  size =  8; break;
        case Display_FontSize10: size = 10; break;
        case Display_FontSize12: size = 12; break;
        case Display_FontSize14: size = 14; break;
        case Display_FontSize18: size = 18; break;
        default:
            // the user must have edited the ini file and added a bogus
            // font size specifier.  default to something sane.
            size = 12;
            break;
    }
    m_fontsize = size;
    m_printer->setFontSize(size);
}


void
PrinterFrame::OnDisplayGreenbar(wxCommandEvent& WXUNUSED(event))
{
    bool greenbar = m_printer->getGreenbar();
    m_printer->setGreenbar(!greenbar);
}


// called when the window is manually closed ("X" button, or sys menu)
void
PrinterFrame::OnClose(wxCloseEvent& WXUNUSED(event))
{
    Show(false);  //simply hide the window
}


void
PrinterFrame::OnConfigureDialog(wxCommandEvent& WXUNUSED(event))
{
    // The validators defined in the dialog implementation bind controls
    // and variables together. Values are transferred between them behind
    // the scenes, so here we don't have to query the controls for their
    // values.
    System2200 sys;
    sys.freezeEmu(true);    // halt emulation

    PrinterDialogDataTransfer *data = new PrinterDialogDataTransfer();
    assert(data != nullptr);

    //set data values here
    int linelength, pagelength;
    m_printer->getPageAttributes(linelength, pagelength);

    data->m_string_linelength = std::to_string(linelength);
    data->m_string_pagelength = std::to_string(pagelength);
    data->m_checkbox_autoshow = m_printer->getAutoshow();
    data->m_checkbox_printasgo = m_printer->getPrintasgo();
    data->m_checkbox_portdirect = m_printer->getPortdirect();
    data->m_choice_portstring = m_printer->getPortstring();

    PrinterConfigDlg dialog(this, "Printer Configuration", data);

    // When the dialog is displayed, validators automatically transfer
    // data from variables to their corresponding controls.
    if (dialog.ShowModal() == wxID_OK) {
        // 'OK' was pressed, so controls that have validators are
        // automatically transferred to the variables we specified
        // when we created the validators.
        long longllength = std::stol(string(data->m_string_linelength));
        long longplength = std::stol(string(data->m_string_pagelength));

        m_printer->setPageAttributes(longllength, longplength);
        m_printer->setAutoshow(data->m_checkbox_autoshow);
        m_printer->setPrintasgo(data->m_checkbox_printasgo);
        m_printer->setPortdirect(data->m_checkbox_portdirect);
        m_printer->setPortstring(string(data->m_choice_portstring));

#if 0
        // PHE: clear the printer log because any previous printing
        // which used hex(0C) for a formfeed is no longer valid
        m_printer->PrintClear();
        // JTB: this is true, but this shortcoming is small compared
        //      to the inconvenience of having the print log wiped
        //      due to a configuration change.  this can be revisited...
#endif
    }

    delete data;

    sys.freezeEmu(false);   // run emulation
}


void
PrinterFrame::OnMenuOpen(wxMenuEvent& WXUNUSED(event))
{
    setMenuChecks();
}


// other class functions ...

// emit a character to the display
void
PrinterFrame::printChar(uint8 byte)
{
    // send it to the Printer
    m_printer->printChar(byte);
}


// destroy a specific printer view
// system may defer destruction for a while until it is safe
void
PrinterFrame::destroyWindow()
{
    saveDefaults();
    Destroy();
}

// print the contents of the stream, then clear it (if successful)
void
PrinterFrame::printAndClear()
{
    if (m_printer->isEmpty()) 
        return;

    System2200 sys;
    sys.freezeEmu(true);

    // remember where the focus was so we can restore it
    wxWindow *winHasFocus = FindFocus();

    wxPrintDialogData printDialogData(* m_printData);
    printDialogData.SetToPage(m_printer->numberOfPages());
    wxPrinter printer(& printDialogData);

    Printout printout(_T(""), m_printer);
    if (!printer.Print(this, &printout, false)) {  
        if (wxPrinter::GetLastError() == wxPRINTER_ERROR)
            wxMessageBox("There was a problem printing.\n"
                        "Perhaps your current printer is not set correctly?",
                        "Printing", wxOK);
        else
            assert(false);    // this should never happen
    } else {
        //printing was ok. now clear the stream.
        m_printer->printClear();
    }

    // restore the focus
    winHasFocus->SetFocus();
    winHasFocus = nullptr;

    sys.freezeEmu(false);
}
