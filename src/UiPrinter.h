// This represents the paper surface that gets drawn upon.
// It is managed by PrinterFrame.

#ifndef _INCLUDE_UI_PRINTER_H_
#define _INCLUDE_UI_PRINTER_H_

#include "w2200.h"
#include "wx/wx.h"
#include "wx/print.h"   // printing support

class PrinterFrame;

class Printer: public wxScrolledWindow
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Printer);
    explicit Printer(PrinterFrame *parent);
    ~Printer();

    // ---- setters/getters ----
    void setFontSize(const int size);

    void setGreenbar(bool greenbar=true);
    bool getGreenbar() const noexcept;

    void setMargins(int  left, int  right, int  top, int  bottom) noexcept;
    void getMargins(int &left, int &right, int &top, int &bottom) const noexcept;

    void               setOrientation(wxPrintOrientation orientation) noexcept;
    wxPrintOrientation getOrientation() const noexcept;

    void        setPaperId(wxPaperSize paperid) noexcept;
    wxPaperSize getPaperId() const noexcept;

    void        setPaperName(const std::string &papername);
    std::string getPaperName() const;

    void       setBin(wxPrintBin paperbin) noexcept;
    wxPrintBin getBin() const noexcept;

    void        setRealPrinterName(const std::string &name);
    std::string getRealPrinterName() const;

    void setPageAttributes(int  linelength, int  pagelength);
    void getPageAttributes(int &linelength, int &pagelength) const noexcept;

    void getCellAttributes(int &cell_w, int &cell_h) const noexcept;

    void setAutoshow(bool b) noexcept;
    bool getAutoshow() const noexcept;

    void setPrintasgo(bool b) noexcept;
    bool getPrintasgo() const noexcept;

    void setPortdirect(bool b) noexcept;
    bool getPortdirect() const noexcept;

    void        setPortstring(const std::string &name);
    std::string getPortstring() const;

    // ---- other functions ----
    // redraw entire screen
    void invalidateAll()  { Refresh(false); }

    // return a pointer to the screen image
    wxBitmap *grabScreen();

    // emit a character to the display
    void printChar(uint8 byte);

    // save the printer contents to a file
    void saveToFile();

    // clear the printer contents
    void printClear();

    // return true if the print stream is empty
    bool isEmpty() const noexcept;

    // return the number of pages in the current copy of the printstream
    int numberOfPages() const noexcept;

    // create a print page image
    void generatePrintPage(wxDC *dc, int pagenum, float vertAdjust);

    // redraw the scrollbars
    void scrollbarSet(int xpos, int ypos, bool redraw);

    // the print dialog is modeless, so a copy of the stream needs to be made
    // and used for the print dialog. These two functions are used to manage the copy
    void createStreamCopy();
    void destroyStreamCopy() noexcept;

private:
    // ---- event handlers ----
    void OnPaint(wxPaintEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnEraseBackground(wxEraseEvent &event) noexcept;
    void OnTimer(wxTimerEvent &event);

    // ---- utility functions ----

    // send a byte directly to the parallel port (windows only)
    void lptChar(uint8 byte);

    // close the parallel port (windows only)
    void closePort();

    // refresh the screen display
    void drawScreen(wxDC &dc, int startCol, int startRow);

    // update the bitmap of the screen image
    void generateScreen(int startCol, int startRow);

    // add a line to the print stream
    void emitLine();

    // add lines to print stream to account for form feed
    void formFeed();

    // check redraw of screen and set scrollbars
    void updateView();

    // update the statusbar text
    void updateStatusbar();

    // ---- member variables ----
    PrinterFrame *m_parent;             // who owns us

    // for support of direct printing to parallel port
    bool        m_printing_flag;
    FILE       *m_fp_port;

    // this holds the dimensions of the visible area we have to draw on,
    // which is entirely independent of the logical Printer dimensions.
    int         m_scrpix_w;             // display dimension, in pixels
    int         m_scrpix_h;             // display dimension, in pixels

    wxBitmap    m_scrbits;              // image of the display

    int         m_chars_w;              // screen dimension, in characters
    int         m_chars_h;              // screen dimension, in characters

    wxFont      m_font;                 // font in use
    int         m_fontsize;             // size of font (in points)
    int         m_charcell_w;           // width of one character cell
    int         m_charcell_h;           // height of one character cell

    bool        m_greenbar;             // paper display format

    // margins
    int         m_marginleft;
    int         m_margintop;
    int         m_marginright;
    int         m_marginbottom;

    std::string m_realprintername;      // name of the real printer
    wxPrintOrientation m_orientation;   // page orientation (wxPORTRAIT or wxLANDSCAPE)
    wxPaperSize m_paperid;              // paper id (wxPAPER_LETTER, etc.)
    std::string m_papername;            // the name of the paperid
    wxPrintBin  m_paperbin;             // paper bin (wxPRINTBIN_DEFAULT, etc.)

    int         m_linelength;           // the line length of the logical printer
    int         m_pagelength;           // the page length of the logical printer
    bool        m_autoshow;             // indicates if view should automatically when new print stream is received
    bool        m_printasgo;            // indicates if each page of the stream should be printed automatically, then cleared, as it fills up
    bool        m_portdirect;           // indicates printing directly to parallel port (windows only). All other printing settings are ignored
                                        //   and output is dumped directly to the port
    std::string m_portstring;           // contains the name of the parallel port (windows only)
    wxTimer     m_portTimer;            // control closing of LPT port

    // place to accumulate characters until we have a full line.
    // a char array is used instead of a wxString to avoid tons
    // of wxString reallocations.
#define m_linebuf_maxlen (256)
    int         m_linebuf_len;          // number of characters in buffer
    char        m_linebuf[m_linebuf_maxlen+1];  // accumulates line to print

    std::vector<std::string> m_printstream;       // represents the entire print stream
    std::vector<std::string> m_printstream_copy;  // this is a copy that is used for printing purposes
};


class Printout : public wxPrintout
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Printout);
    Printout(const wxString &title, std::shared_ptr<Printer> printer);

    // ---- event handlers ----
    bool OnPrintPage(int page) override;
    bool OnBeginDocument(int startPage, int endPage) override;

    bool HasPage(int page) noexcept override;
    void GetPageInfo(int *minPage, int *maxPage,
                     int *selPageFrom, int *selPageTo) noexcept override;

private:
    std::shared_ptr<Printer> m_printer;
};

#endif _INCLUDE_UI_PRINTER_H_

// vim: ts=8:et:sw=4:smarttab
