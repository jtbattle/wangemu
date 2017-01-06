// This file implements the Printer class.
// It contains two classes:
//      Printer: this class is the wxScrolledWindow that display a view
//                 of the printer and also contains the print stream
//      Printout: this class actually prints to the printer
// Logically, Printer represents the printer

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiPrinter.h"          // this module's defines
#include "UiPrinterFrame.h"     // parent's defines
#include "Host.h"               // for Config* functions

#include <algorithm>            // for std::max
#include <fstream>
using std::ofstream;

// ----------------------------------------------------------------------------
// static variables
// ----------------------------------------------------------------------------

static const int bar_h   = 3;   // height of greenbar
static const int hmargin = 3;   // number chars horizontal padding on page

// ----------------------------------------------------------------------------
// Printer
// ----------------------------------------------------------------------------

// connect the wxWindows events with the functions which process them
BEGIN_EVENT_TABLE(Printer, wxScrolledWindow)
    EVT_PAINT                   (Printer::OnPaint)
    EVT_ERASE_BACKGROUND        (Printer::OnEraseBackground)
    EVT_SIZE                    (Printer::OnSize)
    EVT_TIMER                   (-1, Printer::OnTimer)
END_EVENT_TABLE()


Printer::Printer(PrinterFrame *parent) :
        wxScrolledWindow(parent, -1, wxDefaultPosition, wxDefaultSize),
        m_parent(parent),
        m_printing_flag(false),
        m_fp_port(nullptr),
        m_scrpix_w(0),
        m_scrpix_h(0),
        m_chars_w(0),
        m_chars_h(0),
        m_fontsize(12),
        m_charcell_w(0),
        m_charcell_h(0),
        m_greenbar(true),
        m_marginleft(0),
        m_margintop(0),
        m_marginright(0),
        m_marginbottom(0),
        m_realprintername(""),
        m_orientation(wxPORTRAIT),
        m_paperid(wxPAPER_LETTER),          // default
        m_papername(""),
        m_paperbin(wxPRINTBIN_DEFAULT),     // default
        m_linelength(80),                   // default
        m_pagelength(66),                   // default
        m_autoshow(true),                   // default
        m_printasgo(true),                  // default
        m_portdirect(false),                // default
        m_portstring("LPT1"),               // default
        m_linebuf_len(0)
{
    // the means don't perform a screen-to-screen blit to effect
    // the scroll.  just let the app redraw everything.
    EnableScrolling(false, false);

    printClear();

    // create a timer to close LPT in a timely fashion
    m_portTimer.SetOwner(this);
    m_portTimer.Start(500);     // set firing interval -- 1/2 second
    m_portTimer.Stop();         // but we don't want it just yet
}

// free resources on destruction
Printer::~Printer()
{
    // if port is open, close it
    closePort();

    m_printstream.clear();
}

// ---- public methods ----

// set the point size of the text
void
Printer::setFontSize(const int size)
{
    wxClientDC dc(this);

    m_font = wxFont(size, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false);
    dc.SetFont( m_font );

    m_fontsize   = size;
    m_charcell_w = dc.GetCharWidth();
    m_charcell_h = dc.GetCharHeight();

    //set the number of rows in the view
    m_chars_h = m_scrpix_h / m_charcell_h;

    //set the number of columns in the view (fixed pitch font makes this easy)
    m_chars_w = m_scrpix_w/m_charcell_w;

    updateView();
}

void
Printer::setGreenbar(bool greenbar)
{
    m_greenbar = greenbar;
    invalidateAll();
}

bool
Printer::getGreenbar() const
{
    return m_greenbar;
}


void
Printer::setMargins(int left, int right, int top, int bottom)
{
    m_marginleft = left;
    m_marginright = right;
    m_margintop = top;
    m_marginbottom = bottom;
}

void
Printer::getMargins(int &left, int &right, int &top, int &bottom) const
{
    left = m_marginleft;
    right = m_marginright;
    top = m_margintop;
    bottom = m_marginbottom;
}

void
Printer::setOrientation(wxPrintOrientation orientation)
{
    m_orientation = orientation;
}

wxPrintOrientation
Printer::getOrientation() const
{
    return m_orientation;
}

void
Printer::setPaperId(wxPaperSize paperid)
{
    m_paperid = paperid;
}

wxPaperSize
Printer::getPaperId() const
{
    return m_paperid;
}

void
Printer::setPaperName(const string &papername)
{
    m_papername = papername;
}

string
Printer::getPaperName() const
{
    return m_papername;
}

void
Printer::setBin(wxPrintBin paperbin)
{
    m_paperbin = paperbin;
}

wxPrintBin
Printer::getBin() const
{
    return m_paperbin;
}

void
Printer::setRealPrinterName(const string &name)
{
    m_realprintername = name;
}

string
Printer::getRealPrinterName() const
{
    return m_realprintername;
}

void
Printer::setPageAttributes(int linelength, int pagelength)
{
    m_linelength = linelength;
    m_pagelength = pagelength;
    updateView();
}


void
Printer::getPageAttributes(int &linelength, int &pagelength) const
{
    linelength = m_linelength;
    pagelength = m_pagelength;
}

void
Printer::getCellAttributes(int *cell_w, int *cell_h) const
{
    *cell_w = m_charcell_w;
    *cell_h = m_charcell_h;
}

// set autoshow attribute
void
Printer::setAutoshow(bool b)
{
    m_autoshow = b;
}


// get autoshow attribute
bool
Printer::getAutoshow() const
{
    return m_autoshow;
}

// set printasgo attribute
void
Printer::setPrintasgo(bool b)
{
    m_printasgo = b;
}


// get printasgo attribute
bool
Printer::getPrintasgo() const
{
    return m_printasgo;
}

// set portdirect attribute
void
Printer::setPortdirect(bool b)
{
    m_portdirect = b;
}

//get portdirect attribute
bool
Printer::getPortdirect() const
{
    return m_portdirect;
}

//portstring attribute
void
Printer::setPortstring(const string &name)
{
    m_portstring = name;
}

string
Printer::getPortstring() const
{
    return m_portstring;
}


// emit a character to the display
void
Printer::printChar(uint8 byte)
{
    byte &= 0x7F;

    if (m_portdirect) {
        lptChar(byte);
        return;
    }

    switch (byte) {

/*
The special Control Codes for the Printer are:
Function Hex Code
Description

ALARM HEX (07)
Generates an audible tone about two seconds in
duration in the speaker at the rear of the printer.

VERTICAL TAB HEX(0B)
Advances paper until the next hole in channel 5 of
the Vertical Format Unit paper tape is reached.

ELONGATED HEX(0E)
CHARACTER
Prints a line up to 66 characters as expanded
(double-width) characters. (See Section III.)

DELETE HEX (7F)
Clears buffer of characters sent before the '7F'

Some of the Wang printers were modified Selectrics and had a different
set of control codes. For instance, you could set and clear tabstops
just like a real Selectric so that printing CTRL-I (tab) would advance
to the next tab stop.
*/

        case 0x09:
            // HORIZONTAL TAB HEX(09)
            // Assumed to be hardcoded to tabstop of 8 characters
            do {
                if (m_linebuf_len >= m_linebuf_maxlen)
                    break;
                m_linebuf[m_linebuf_len++] = ' ';
            } while (m_linebuf_len%8 != 0);
            break;

        case 0x0A:
            // LINE FEED HEX(0A)
            // Advances paper one line.

            // Emit two lines to the printer.
            if (m_linebuf_len > 0)
                emitLine();
            emitLine();

            break;

        case 0x0C:
            // FORM FEED HEX(0C)
            // Advances paper until the next hole in channel 7
            // of the Vertical Format Unit paper tape is reached.

            // Emit line to the printer, add empty lines to advance stream to the next page.
            if (m_linebuf_len > 0)
                emitLine();
            formFeed();

            break;

        case 0x0D:
            // CARRIAGE RETURN HEX(0D)
            // Causes the line of characters stored in the printer
            // buffer to be printed. An automatic line feed
            // occurs after the line has been printed and the
            // print head returns to the left side of the printer
            // carrier.

            // Emit the line to the print stream.
            emitLine();

            break;

        default:        // just a character
            if ((byte >= 32) && (m_linebuf_len < m_linebuf_maxlen)) {
                // accumulate the partial line
                m_linebuf[m_linebuf_len++] = (char)byte;
            }
            break;
    }
}


// send a byte directly to the parallel port (windows only)
void
Printer::lptChar(uint8 byte)
{
    if (m_fp_port == nullptr) {
        // port is closed.  open it and set timer to close if port goes idle.
        assert(m_portstring != "");
        // UI_Info("opening %s", m_portstring.c_str());
        m_fp_port = fopen(m_portstring.c_str(), "wb");
        m_portTimer.Start();  // period was set in the constructor
    }

    fputc(byte, m_fp_port);
    m_printing_flag = true;
    if ( (byte == 0x0A) ||   // line feed
         (byte == 0x0C) ||   // page feed
         (byte == 0x0D))     // carriage return
        (void)fflush(m_fp_port);
}


// close the printing port
void
Printer::closePort()
{
    m_portTimer.Stop();

    if (m_fp_port != nullptr) {
        fclose(m_fp_port);
        m_fp_port = nullptr;
    }
}


// save the contents of the printer to a file
void
Printer::saveToFile()
{
    string fullpath;
    Host hst;
    int r = hst.fileReq(Host::FILEREQ_PRINTER, "Save printer log file", 0,
                         &fullpath);
    if (r == Host::FILEREQ_OK) {

        // save the file
        ofstream ofs(fullpath, ofstream::out   |  // we're writing
                               ofstream::trunc |  // discard any existing file
                               ofstream::binary); // just write what I ask
        if (!ofs.is_open()) {
            UI_Error("Couldn't write to file '%s'", fullpath.c_str());
            return;
        }

        const int maxn = m_printstream.size();

        for(int n = 0; n < maxn; n++) {
#ifdef __WXMSW__
            string tmpline(m_printstream[n] + "\r\n");
#else
            string tmpline(m_printstream[n] + "\n");
#endif
            int len = tmpline.length();
            ofs.write( tmpline.c_str(), len );
            if (!ofs.good()) {
                UI_Error("Error writing to line %d of '%s'", n+1, fullpath.c_str());
                ofs.close();
                return;
            }
        }
        ofs.close();
    }
}


// clear the printer contents
void
Printer::printClear()
{
    m_linebuf_len = 0;          // partially accumulated line
    m_printstream.clear();      // log of all complete lines
    scrollbarSet(0, 0, false);
    invalidateAll();
}

// return true of the print stream is empty
bool
Printer::isEmpty() const
{
    return m_printstream.empty();
}

// return the number of pages in the current copy of the printstream
int
Printer::numberOfPages() const
{
    const int num_rows = m_printstream.size();
    return ((num_rows + m_pagelength-1) / m_pagelength);  // round up
}

// create a page image
void
Printer::generatePrintPage(wxDC *dc, int pagenum, float vertAdjust)
{
    size_t length = m_linelength;
    int startRow = ((pagenum - 1) * m_pagelength);
    int startCol = 0;
    string line;

    dc->SetFont( m_font );

    // draw each row of the text
    for(int row = 0; row < m_pagelength; row++) {

        if (size_t(startRow + row) < m_printstream.size() ) {
            // the line exists
            line = m_printstream[startRow + row];
            line = line.substr(startCol, m_linelength - startCol);
        } else {
            // use empty line
            line = "";
        }

        line = line.erase(length);
        dc->DrawText(line, 0 ,row*m_charcell_h*vertAdjust);
    }
}


void
Printer::scrollbarSet(int xpos, int ypos, bool redraw)
{
    SetScrollbars(  m_charcell_w,               // pixels per scroll unit x
                    m_charcell_h,               // pixels per scroll unit y
                    m_linelength + 2*hmargin,   // number of units x
                    m_printstream.size(),       // number of units y,
                    xpos,                       // x position in scroll units
                    ypos,                       // y position in scroll units
                    !redraw);                   // redraw the screen
}


void
Printer::createStreamCopy()
{
    m_printstream_copy = m_printstream;
}


void
Printer::destroyStreamCopy()
{
    m_printstream_copy.clear();
}


// ---- private methods ----
void
Printer::OnPaint(wxPaintEvent &WXUNUSED(event))
{
    //PHE: Note: I should disable the print menu at this time
    // because if there  is a lot to draw here and this drawing
    // is interrupted because of printing, then the reference
    // pointer used in here will get messed up

    //PHE: freeze the emu?

    //have we scrolled?
    int firstCol, firstLine;
    GetViewStart(&firstCol, &firstLine);

    // scroll-wheeling up can produce negative offsets
    firstLine = std::max(firstLine, 0);
    firstCol  = std::max(firstCol,  0);

    // scroll-wheeling up can produce large offset
    const int num_rows = m_printstream.size();    // # rows in log
    if (num_rows < m_chars_h)
        firstLine = 0;

    wxPaintDC dc(this);
    drawScreen(dc, firstCol, firstLine);
    updateStatusbar();
}

void
Printer::OnSize(wxSizeEvent &event)
{
    int width, height;
    GetClientSize(&width, &height);

    m_scrpix_w = width;
    m_scrpix_h = height;

    //reset the number of rows in the view
    if (m_charcell_h != 0)              // the first time through, when font has not initialized, it is zero
        m_chars_h = height / m_charcell_h;
    if (m_charcell_w != 0)
        m_chars_w = width / m_charcell_w;

    m_scrpix_h = m_scrpix_h + m_charcell_h; // add one extra row to make scrolling work

    if (width > 0 && height > 0) {
        m_scrbits = wxBitmap(width, height, -1);     // same depth as display
        updateView();
    }

    event.Skip();       // do the rest of the OnSize processing
}

// If the port has been opened too long without activity, close it.
// If we have been printing to LPT recently, keep it open a while longer.
void
Printer::OnTimer(wxTimerEvent &WXUNUSED(event))
{
    if (m_printing_flag == false) {
        // if port is open, close it
        closePort();
        // UI_Info("Timer timed out; closed %s", m_portstring.c_str());
    } else {
        // keep port open.  timer re-arms automatically.
        m_printing_flag = false;  // will be set if we start printing before the timer expires
    }
}


// intercept this event to prevent flashing as the system so helpfully
// clears the window for us before generating the OnPaint event.
void
Printer::OnEraseBackground(wxEraseEvent &WXUNUSED(event))
{
    // do nothing
}

// refresh the entire window
void
Printer::drawScreen(wxDC& dc, int startCol, int startRow)
{
    // update the view image bitmap
    generateScreen(startCol, startRow);

    // blast the image to the screen
    dc.DrawBitmap(m_scrbits, 0,0);  // src, x,y
}

// update the pixmap of the screen image
// if greenbar mode is on, the layout is like this:
//    |<three chars of white>|<N chars of green or white>|<three chars of white>|
//    the page alternates three lines of white, three of green, ...
void
Printer::generateScreen(int startCol, int startRow)
{
    // width of virtual paper, in pixels
    const int page_w = m_charcell_w * (m_linelength + 2*hmargin);

    // amount of background to the left/right of virtual page in viewport
    const int left_bg_w  = std::max(0, (m_scrpix_w - page_w)/2);
    const int right_bg_w = std::max(0, m_scrpix_w - left_bg_w);

    // left edge of paper relative to the viewport (can be negative)
    const int left_edge = (-startCol * m_charcell_w)    // if scrolled left
                        + left_bg_w;                    // if viewport > page_w

    // right edge of paper relative to the viewport, exclusive
    const int right_edge = left_edge + page_w - 1;

    // this is the number of characters to skip at the start of each row
    const int skip_chars = std::max((startCol - hmargin), 0);

    // this code assumes that if the viewport is wider than the page,
    // there won't be any startCol offset
    assert( (startCol == 0) || (left_edge < 0) );

    wxMemoryDC imgDC(m_scrbits);

    // draw page background to white
    {
        imgDC.SetPen(*wxWHITE_PEN);
        imgDC.SetBrush(*wxWHITE_BRUSH);
        imgDC.DrawRectangle( left_edge, 0,              // origin
                             page_w, m_scrpix_h );      // width, height
    }

    // draw greyed out region on the right and left
    // (right_bg_w >= left_bg_w) so only need to check right_bg_w
    if (right_bg_w >= 0) {
        imgDC.SetPen(*wxGREY_PEN);
        imgDC.SetBrush(*wxGREY_BRUSH);
        imgDC.DrawRectangle( 0, 0,              // origin
                             left_bg_w,         // width
                             m_scrpix_h );      // height
        imgDC.DrawRectangle( right_edge, 0,     // origin
                             right_bg_w,        // width
                             m_scrpix_h );      // height

        // draw black edge to paper for emphasis
        imgDC.SetPen(*wxBLACK_PEN);
        imgDC.DrawLine( left_edge-1, 0,         // origin
                        left_edge-1,            // width
                        m_scrpix_h);            // height
        imgDC.DrawLine( right_edge-1, 0,        // origin
                        right_edge-1,           // width
                        m_scrpix_h);            // height

        imgDC.SetPen(wxNullPen);
        imgDC.SetBrush(wxNullBrush);
    }

    // if greenbar mode, draw green rounded rectangles
    if (m_greenbar) {

        wxColor lightGreen = wxColour(0xB0,0xFF,0xB0);
        wxColor darkGreen  = wxColour(0x00,0x80,0x00);
        wxBrush rectfill(lightGreen);
        wxPen   rectoutline(darkGreen);
        imgDC.SetPen(rectoutline);
        imgDC.SetBrush(rectfill);

        int bar_2h = bar_h*2;   // twice height of greenbar
        int first_greenbar = ((startRow                )/bar_2h)*bar_2h + bar_h;
        int last_greenbar  = ((startRow + m_chars_h + 1)/bar_2h)*bar_2h + bar_h;

        for(int bar = first_greenbar; bar <= last_greenbar; bar += bar_2h) {
            int yoff = (bar - startRow) * m_charcell_h;
            int xoff = left_edge + hmargin* m_charcell_w - m_charcell_w/2;  //  \ expand it 1/2 char
            int width  = m_linelength * m_charcell_w     + m_charcell_w;    //  / on each side
            int height = bar_h * m_charcell_h;
            double radius = m_charcell_w * 0.5;
            imgDC.DrawRoundedRectangle(xoff,yoff, width,height, radius);
        }

        imgDC.SetPen(wxNullPen);
        imgDC.SetBrush(wxNullBrush);
    }

    // draw page breaks
    if (1) {
        wxColor gray = wxColour(0x80, 0x80, 0x80);
        wxPen breakpen(gray, 1, wxUSER_DASH);
        wxDash dashArray[] = { 2, 5 };  // pixels on, pixels off
        breakpen.SetDashes(2, dashArray);
        imgDC.SetPen(breakpen);

        int first_break = ((startRow                )/m_pagelength)*m_pagelength;
        int last_break  = ((startRow + m_chars_h + 1)/m_pagelength)*m_pagelength;
        if (startRow == 0)
            first_break += m_pagelength;        // skip first break
        for(int brk = first_break; brk <= last_break; brk += m_pagelength) {
            int x_off = left_edge;
            int y_off = m_charcell_h * (brk - startRow);
            int x_end = left_edge + page_w;
            imgDC.DrawLine( x_off, y_off,       // from (x,y)
                            x_end, y_off);      // to   (x,y)
        }

        imgDC.SetPen(wxNullPen);
    }

    // draw each row of the text
    {
        imgDC.SetBackgroundMode(wxTRANSPARENT); // in case of greenbar mode
        imgDC.SetTextBackground(*wxWHITE);      // moot if greenbar mode
        imgDC.SetTextForeground(*wxBLACK);      // always
        imgDC.SetFont(m_font);

        const int num_rows = m_printstream.size();
        string line;

        for(int row = 0; row < m_chars_h + 1; row++) {

            if (startRow + row < num_rows) {
                // the line exists
                line = m_printstream[startRow + row];
                size_t nchars = (m_linelength+hmargin) - skip_chars;
                if ((skip_chars > 0) || (nchars < line.length())) {
                    // chop off any chars to the left of the display or
                    // to the right of the right edge of the virtual paper
                    line = line.substr(skip_chars, nchars);
                }
                int x_off = left_bg_w + m_charcell_w * std::max(hmargin - startCol, 0);
                int y_off = m_charcell_h * row;
                imgDC.DrawText(line, x_off, y_off);
            }

        } // for(row)
    }

    imgDC.SelectObject(wxNullBitmap);   // release m_scrbits
}

// emit current buffer to the print stream
void
Printer::emitLine()
{
    m_linebuf[m_linebuf_len] = (char)0x00;
    m_printstream.push_back(string(m_linebuf));
    m_linebuf_len = 0;

    if (m_autoshow)
        m_parent->Show(true);   //show the printer window if it is off (this should be a

    updateView();

    if (m_printasgo) {
        if ((m_printstream.size() % m_pagelength) == 0)
            // we just added the last line on a page. orint it.
            m_parent->printAndClear();
    }
}

// emit lines to simulate a form feed
void
Printer::formFeed()
{
    int linesToAdd = m_pagelength - (m_printstream.size() % m_pagelength);
    for(int i = 0; i < linesToAdd; i++) {
        // call emit line to that current line buffer is flushed
        // and to make sure page oriented functions such as "print as you go" are invoked
        // if necessary
        emitLine();
    }
    updateView();
}

// check redraw of screen, set scrollbars, update statusbar text
void
Printer::updateView()
{
    // we need to determine here if the screen needs to be redrawn.
    // if this line number in the stream is within m_chars_h of the
    // first visible line, we need to redraw.  otherwise not.
    int first_visible_row, first_visible_char;
    GetViewStart(&first_visible_char, &first_visible_row);
    const int endrow = first_visible_row + m_chars_h;   // last row on screen
    const int num_rows = m_printstream.size();          // # rows in log

    if (num_rows <= m_chars_h) {
        // the entire print state fits on screen
        scrollbarSet(0, 0, true);
    } else if (num_rows == endrow+1) {
        // if the new row is one off the end,
        // scroll to make sure the new row is still visible
        scrollbarSet(first_visible_char, first_visible_row+1, true);
    } else {
        // the newly added row is off the portion we are looking at.
        // call ScrollbarSet to make sure the scrollbar is updated,
        // but keep the top row unchanged
        scrollbarSet(first_visible_char, first_visible_row, true);
    }

    updateStatusbar();
    invalidateAll();
}


// update the statusbar text
void
Printer::updateStatusbar()
{
#if __WXMAC__
    // on the mac, the code in here causes a paint refresh, which calls this again and we get in an infinite loop
    return;
#endif
    int first_visible_row, first_visible_char;
    GetViewStart(&first_visible_char, &first_visible_row);
    const int num_rows = m_printstream.size();      // # rows in log

    // update statusbar text
    // the current line/page reported are based on the last visible
    // on the page.  it gets a little weird in that the last visible
    // line is ambiguous in the case of partial rows.
    const int num_pages = numberOfPages();
    const int last_line = std::min(first_visible_row + m_chars_h+1, num_rows);
    const int cur_page = (last_line + m_pagelength-1) / m_pagelength;
    string msg("Page " + std::to_string(cur_page) +
               " of " + std::to_string(num_pages) +
               " (line " + std::to_string(last_line) +
               " of " + std::to_string(num_rows) + ")");
    m_parent->SetStatusText(msg);
}

// ----------------------------------------------------------------------------
// Printout
// ----------------------------------------------------------------------------

// constructor
Printout::Printout(wxChar *title, std::shared_ptr<Printer> printer) :
        wxPrintout(title),
        m_printer(printer)
{
}

bool
Printout::OnPrintPage(int page)
{
    wxDC *dc = GetDC();
    if (dc) {
        // for now I have two approaches.
        // The first approach scales the virtual pageit to fit the real page.
        // The second approach matches the fonts as close as possible and
        // does not use margins.

        // first, get some key parameters from Printer
        size_t length;
        int llen, plen;
        m_printer->getPageAttributes(llen, plen);
        int cell_w, cell_h;
        m_printer->getCellAttributes(&cell_w, &cell_h);
        length = llen;

        // device units margin
        int marginleft, marginright, margintop, marginbottom;
        m_printer->getMargins(marginleft, marginright, margintop, marginbottom);

#if 1
        //approach 1
        float maxX = llen*cell_w;
        float maxY = plen*cell_h;

        // Add the margin to the graphic size
        maxX += (marginleft + marginright);
        maxY += (margintop + marginbottom);

        // Get the size of the DC in pixels
        // The size varies. For printing, it will be the real printer. For preview, it will
        // be the graphic area we see on the screen, based on the zoom factor.
        int w, h;
        dc->GetSize(&w, &h);

        // Calculate a suitable scaling factor
        float scaleX=(float)(w/maxX);
        float scaleY=(float)(h/maxY);

        // use x or y scaling factor, whichever fits on the DC
//      float actualScale = std::min(scaleX, scaleY);

#if 0 // this should be a configuration parameter
        // calculate the position on the DC for centering the graphic
        float posX = (float)((w - (maxX*actualScale))/2.0);
        float posY = (float)((h - (maxY*actualScale))/2.0);
#else
        // no centering
        float posX = marginleft;
        float posY = margintop;
#endif

        //set the scale and origin
        float vertAdjust;
#if 0
        dc->SetUserScale(actualScale, actualScale);
        vertAdjust = 1.0;
#else
        // we want to set things up so that the width is correct, and then
        // we will create a spacing factor for the vertical dimension
        dc->SetUserScale(scaleX, scaleX);
        vertAdjust = scaleY / scaleX;
#endif
        dc->SetDeviceOrigin((long)posX, (long)posY);

#else
        //approach 2
        // This scales the DC so that the printout roughly represents the
        // the screen scaling.

        // Get the logical pixels per inch of screen and printer
        int ppiScreenX, ppiScreenY;
        GetPPIScreen(&ppiScreenX, &ppiScreenY);
        int ppiPrinterX, ppiPrinterY;
        GetPPIPrinter(&ppiPrinterX, &ppiPrinterY);

        float scaleX = (float)((float)ppiPrinterX/(float)ppiScreenX);
        float scaleY = (float)((float)ppiPrinterY/(float)ppiScreenY);

        // Now we have to check in case our real page size is reduced
        // (e.g. because we're drawing to a print preview memory DC)
        int pageWidth, pageHeight;
        int w, h;
        dc->GetSize(&w, &h);
        GetPageSizePixels(&pageWidth, &pageHeight);

        // If printer pageWidth == current DC width, then this doesn't
        // change. But w might be the preview bitmap width, so scale down.
        float overallScaleX = scaleX * (float)(w/(float)pageWidth);
        float overallScaleY = scaleY * (float)(h/(float)pageHeight);
        dc->SetUserScale(overallScaleX, overallScaleY);
        // FIXME: Need to set device origin for approach 2 as well
        // For now assume 0,0
        dc->SetDeviceOrigin(0, 0);
#endif //approach 2
        m_printer->generatePrintPage(dc, page, vertAdjust);
        dc->SetDeviceOrigin(0,0);
        dc->SetUserScale(1.0, 1.0);

        return true;
    } else

    return false;
}

bool
Printout::HasPage(int page)
{
    return (page <= m_printer->numberOfPages());
}

bool
Printout::OnBeginDocument(int startPage, int endPage)
{
    if (!wxPrintout::OnBeginDocument(startPage, endPage))
        return false;
    return true;
}

void
Printout::GetPageInfo(int *minPage, int *maxPage, int *selPageFrom, int * selPageTo)
{
    *minPage = 1;
    *maxPage = m_printer->numberOfPages();
    *selPageFrom = 1;
    *selPageTo = m_printer->numberOfPages();
}

// vim: ts=8:et:sw=4:smarttab
