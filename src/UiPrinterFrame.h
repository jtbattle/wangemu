// This implements the window that displays the emulated printer paper,
// It directly handles menu and status bar duties, and passes off the rest
// to the Printer class.

#ifndef _INCLUDE_UI_PRINTER_FRAME_H_
#define _INCLUDE_UI_PRINTER_FRAME_H_

#include "w2200.h"
#include "wx/wx.h"
#include <wx/cmndata.h>

class Printer;

enum wxPrintBin;

class PrinterFrame : public wxFrame
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(PrinterFrame);
    // constructor
    PrinterFrame(const wxString &title, const int io_addr);

    // destructor
    ~PrinterFrame();

    // save/restore class-global options to/from config file
    static void saveGlobalDefaults();
    static void getGlobalDefaults();

    // translation of papersize (wxPaperSize) enum
    wxPaperSize paperSize(const std::string &papersize_name);
    std::string paperSize(wxPaperSize papersize_val) const;

    // translation of paperbin (wxPrintBin) enum
    wxPrintBin paperBin(const std::string &paperbin_name) const noexcept;
    std::string paperBin(wxPrintBin paperbin_val) const;

    // emit a character to the display
    void printChar(uint8 byte);

    // add Printer to list
    void addPrinter();

    // destroy a specific Printer
    void destroyWindow();

    // print the entire contents of the print stream, then clear it
    void printAndClear();

private:
    // ---- event handlers ----

    void OnFileClose(wxCommandEvent &event);
    void OnFileSaveAs(wxCommandEvent &event);
    void OnMenuOpen(wxMenuEvent &event);

    // select screen font size
    void OnFontSize(wxCommandEvent &event);

    // toggle greenbar or not
    void OnDisplayGreenbar(wxCommandEvent &event);

    // bring up configuration dialog
    void OnConfigureDialog(wxCommandEvent &event);

    // clear the printer contents
    void OnPrintClear(wxCommandEvent &event);

    // print preview
    void OnPrintPreview(wxCommandEvent &event);

    // print
    void OnPrint(wxCommandEvent &event);

    // print setup
    void OnPrintSetup(wxCommandEvent &event);

    // page setup
    void OnPageSetup(wxCommandEvent &event);

    // called when the window changes size
    void OnSize(wxSizeEvent &event);

    // called when the window is manually closed ("X" button, or sys menu)
    void OnClose(wxCloseEvent& WXUNUSED(event));

    // used to override the preview window's OnClose event
    void PP_OnClose(wxCloseEvent &event);

    // ---- utility functions ----

    void makeMenubar();
    void makeStatusbar();

    // save/get Printer options to the config file
    void saveDefaults();
    void getDefaults();

    void setupRealPrinter();    // set the printer settings based on current defaults
    void setMenuChecks();

    // pick one of N color schemes
    void setDisplayColorScheme(int n);

    // ---- data members ----

    const int    m_printer_addr;         // use to track configuration options

    wxMenuBar   *m_menubar   = nullptr;  // menubar on frame
    wxStatusBar *m_statusbar = nullptr;  // status bar on frame
    Printer     *m_printer   = nullptr;  // emulated Printer display window

    int          m_font_size    = 12;    // eg 8 for 8 pt, 12 for 12 pt, etc
    int          m_preview_zoom = 25;    // zoom factor for page preview

    std::unique_ptr<wxPrintData>           m_printData;
    std::unique_ptr<wxPageSetupDialogData> m_pageSetupData;
};

#endif // _INCLUDE_UI_PRINTER_FRAME_H_

// vim: ts=8:et:sw=4:smarttab
