// This file implements the Crt class.
// Logically, it implements the character generator and CRT tube.
// It is used by the CrtFrame class.

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between Ui* wxgui modules
#include "UiCrt.h"              // this module's defines
#include "UiCrtErrorDlg.h"      // error code decoder
#include "UiCrtFrame.h"         // this module's owner
//#include "System2200.h"       // needed if dbglog() is in use

#include <wx/sound.h>           // "beep!"

#define USE_STRETCH_BLIT 0

// ----------------------------------------------------------------------------
// Crt
// ----------------------------------------------------------------------------

// connect the wxWindows events with the functions which process them
BEGIN_EVENT_TABLE(Crt, wxWindow)
    EVT_ERASE_BACKGROUND (Crt::OnEraseBackground)
    EVT_PAINT            (Crt::OnPaint)
    EVT_KEY_DOWN         (Crt::OnKeyDown)
    EVT_CHAR             (Crt::OnChar)
    EVT_SIZE             (Crt::OnSize)
    EVT_LEFT_DCLICK      (Crt::OnLeftDClick)
END_EVENT_TABLE()

static char id_string[] = "*2236DE R2016 19200BPS 8+O (USA)";

// debugging utility
wxLog *g_logger = nullptr;

Crt::Crt(CrtFrame *parent, int screen_type) :
    wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize),
    m_parent(parent),
    m_screen_type(screen_type),
    m_chars_w((m_screen_type == UI_SCREEN_64x16) ? 64 : 80),
    m_chars_h((m_screen_type == UI_SCREEN_64x16) ? 16 : 24),
    m_chars_h2((m_screen_type == UI_SCREEN_2236DE) ? 25 : m_chars_h),
    m_display{0x00},
    m_fontsize(FONT_MATRIX12),
    m_fontdirty(true),          // must be regenerated
    m_charcell_w(1),            // until SetFontSize overrides.  this prevents
    m_charcell_h(1),            // problems when m_scrbits is first allocated.
    m_charcell_sx(1),
    m_charcell_sy(1),
    m_charcell_dy(1),
    m_FGcolor( wxColor(0xff,0xff,0xff) ),
    m_BGcolor( wxColor(0x00,0x00,0x00) ),
    m_display_contrast(100),
    m_display_brightness(0),
    m_scrpix_w(0),
    m_scrpix_h(0),
    m_RCscreen(wxRect(0,0,0,0)),
    m_curs_x(0),
    m_curs_y(0),
    m_frame_count(0),
    m_dirty(true),
    m_attr{0},
    m_raw_cnt(0),
    m_raw_buf{0},
    m_box_bottom{false},
    m_attrs(0),
    m_attr_on(false),
    m_attr_temp(false),
    m_attr_under(false),
    m_beep(nullptr)
{
    if (0) {
        g_logger = new wxLogWindow(m_parent, "Logging window", true, false);
    }

    create_beep();
    if (!m_beep && false) {
        UI_Warn("Emulator was unable to create the beep sound.\n"
                "HEX(07) will produce the host bell sound.");
    }

    scr_clear();

    reset(true);
}


// free resources on destruction
Crt::~Crt()
{
    wxDELETE(g_logger);
}

// reset the crt
// hard_reset=true means
//     power on reset
// hard_reset=false means
//     user pressed the SHIFT-RESET sequence or
//     the terminal received a reset sequence
void
Crt::reset(bool hard_reset)
{
    bool smart_term = (m_screen_type == UI_SCREEN_2236DE);

    // dumb CRT controllers don't independently get reset;
    // the host CPU tells it to clear.  smart terminals
    // will independently clear the screen even if the serial
    // line is disconnected.
    if (hard_reset || smart_term) {
        // dumb/smart terminal state:
        m_curs_x     = 0;
        m_curs_y     = 0;
        m_curs_attr  = cursor_attr_t::CURSOR_ON;
        m_dirty      = true;  // must regenerate display

        // smart terminal state:
        m_raw_cnt    = 0;
        m_input_cnt  = 0;

        m_attrs      = char_attr_t::CHAR_ATTR_BRIGHT; // implicitly primary char set
        m_attr_on    = false;
        m_attr_temp  = false;
        m_attr_under = false;
    }

    // smart terminals echo the ID string only at power on
    if (hard_reset && smart_term) {
        char *idptr = &id_string[1];  // skip the leading asterisk
        while (*idptr) {
            processChar3(*idptr);
            idptr++;
        }
        processChar3(0x0D);
        processChar3(0x0A);
    }
}

void
Crt::setFontDirty(bool dirty)
{
    m_fontdirty = dirty;
    m_dirty |= dirty;

    if (dirty) {
        // invalidate all, not just the text area, to ensure borders get redrawn
        invalidateAll();
    }
}

bool
Crt::isFontDirty() const
{
    return m_fontdirty;
};


// set the point size of the text as well as the font style.
void
Crt::setFontSize(const int size)
{
    m_fontsize = size;
    setFontDirty();
}

int
Crt::getFontSize() const
{
    return m_fontsize;
}


// set display attributes
void
Crt::setDisplayContrast(int n)
{
    m_display_contrast = n;
    setFontDirty();
}

void
Crt::setDisplayBrightness(int n)
{
    m_display_brightness = n;
    setFontDirty();
}

// set the display fg/bg colors
void
Crt::setColor(wxColor FG, wxColor BG)
{
    m_FGcolor = FG;
    m_BGcolor = BG;
    setFontDirty();
}


// if the display has changed, updated it
void
Crt::refreshWindow()
{
    if (isDirty()) {
#if USE_STRETCH_BLIT
        // FIXME: needed for stretchblit mode until I redo border stuff
        invalidateAll();
#else
        invalidateText();
#endif
        setDirty(false);
    }
}


// return a pointer to the screen image
wxBitmap*
Crt::grabScreen()
{
    generateScreen();   // update the screen image bitmap
    return &m_scrbits;   // return a pointer to the bitmap
}


void
Crt::OnPaint(wxPaintEvent &WXUNUSED(event))
{
    wxPaintDC dc(this);

#if 0 // for debugging
    int vX, vY, vW, vH;
    wxRegionIterator upd(GetUpdateRegion());
    while (upd) {
        vX = upd.GetX();
        vY = upd.GetY();
        vW = upd.GetW();
        vH = upd.GetH();
        upd++;
    }
#endif

    generateScreen();   // update the screen image bitmap
#if USE_STRETCH_BLIT
    wxMemoryDC memDC(m_scrbits);
    dc.StretchBlit(
            0,                       // xdest
            0,                       // ydest
            m_scrpix_w,              // dstWidth
            m_scrpix_h,              // dstHeight
            &memDC,                  // source
            0,                       // xsrc
            0,                       // ysrc
            m_RCscreen.GetWidth(),   // srcWidth
            m_RCscreen.GetHeight()   // srcHeight
    );
    memDC.SelectObject(wxNullBitmap);
#else
    dc.DrawBitmap( m_scrbits, m_RCscreen.GetX(), m_RCscreen.GetY() );

    // draw borders around active text area.
    // if we are doing an incremental update, supposedly
    // this all gets clipped against the damaged region,
    // so we don't actually draw it if it isn't necessary.
    {
        int left     = m_RCscreen.GetLeft();
        int top      = m_RCscreen.GetTop();
        int bottom   = m_RCscreen.GetBottom() + 1;
        int right    = m_RCscreen.GetRight() + 1;
// hmm, I was wondering how the bottom & right edges were treated.
// dumping various m_RCscreen.GetFoo() calls, I got this example:
//     top=12, bottom=300, height=289.
// Thus, height isn't (bottom-top).  the bottom returned appears to be
// included in the height.  when drawing, though, I think it isn't included.
// When I set up m_RCscreen, I set top and height.  thus, the use of
// Bottom (and Right) are suspect here.  bottom and right are inclusive
// of the active text area.  I fix that by adding 1 to bottom and right.
        int bottom_h = (m_scrpix_h - bottom);
        int right_w  = (m_scrpix_w - right);

        wxColor bg(intensityToColor(0.0f));
        dc.SetBrush(wxBrush(bg, wxBRUSHSTYLE_SOLID));
        dc.SetPen(wxPen(bg, 1, wxPENSTYLE_SOLID));

        if (top > 0) {  // top border is required
            dc.DrawRectangle(0,0, m_scrpix_w,top);
        }
        if (bottom_h > 0) {     // bottom border is required
            dc.DrawRectangle(0,bottom, m_scrpix_w,bottom_h);
        }
        if (left > 0) { // left border is required
            dc.DrawRectangle(0,top, left,bottom-top);
        }
        if (right_w) {  // right border is required
            dc.DrawRectangle(right,top, right_w,bottom-top);
        }

        dc.SetPen(wxNullPen);
        dc.SetBrush(wxNullBrush);
    }
#endif

    setFrameCount( getFrameCount() + 1 );
}


void
Crt::OnSize(wxSizeEvent &event)
{
    int width, height;
    GetClientSize(&width, &height);

    m_scrpix_w = width;
    m_scrpix_h = height;

    recalcBorders();
    invalidateAll();

    event.Skip();
}


// catch and ignore erasebackground events so we don't get redraw
// flicker on window resize events
void
Crt::OnEraseBackground(wxEraseEvent &WXUNUSED(event))
{
    // do nothing
}

// the user has double clicked on the screen.
// see if the line contains a Wang BASIC error code, and if so
// pop open a help message describing the error in more detail.
void
Crt::OnLeftDClick(wxMouseEvent &event)
{
    wxPoint pos     = event.GetPosition();      // client window coordinates
    wxPoint abs_pos = ClientToScreen(pos);      // absolute screen coordinates

    if (m_charcell_w == 0 || m_charcell_h == 0) {
        return;
    }

    int cell_x = (pos.x - m_RCscreen.GetX()) / m_charcell_w;
    int cell_y = (pos.y - m_RCscreen.GetY()) / m_charcell_h;

    if (cell_x < 0 || cell_x > m_chars_w) {
        return;
    }
    if (cell_y < 0 || cell_y > m_chars_h) {
        return;
    }

    // scan line to see if it is an error line.  Although most errors
    // are of the form:
    //   Wang BASIC: <spaces>^ERR <number><spaces>
    //            or <spaces>^ERR =<number><spaces>
    //   BASIC-2:    <spaces>^ERR <letter><number><spaces>
    // some are not.  Some have arbitrary garbage on the line before
    // the appearance of the "^ERR ..." string.
    char *p = reinterpret_cast<char *>(&m_display[cell_y * m_chars_w]); // first char of row
    char *e = reinterpret_cast<char *>(p + m_chars_w);          // one past final char of row

    // scan entire line looking for first appearance of one of these forms.
    // Wang BASIC:
    //    ^ERR <number><spaces>
    //    ^ERR =<number><spaces>
    // BASIC-2:
    //    ^ERR (<letter>|'=')*<number>+
    // this scanner is lax and scans for both with this pattern:
    for( ; p < e-7; p++) {

        if (strncmp(p, "^ERR ", 5) != 0) {
            continue;
        }
        char *pp = p + 5;

        std::string errcode;

        // check for optional initial letter or '='
        if ((*pp >= 'A' && *pp <= 'Z') || (*pp == '=')) {
            errcode = *pp++;
        }

        // make sure there is at least one digit
        if (!isdigit(*pp)) {
            continue;
        }

        // grab the number
        while ((pp < e) && isdigit(*pp)) {
            errcode += *pp++;
        }

    #if 0
        // launch an HTML browser and look up the error code
        std::string helpfile = "errors.html#Code-" + errcode;
        ::wxLaunchDefaultBrowser(helpfile);
    #else
        // pop open a dialog with the relevant information
        // this is a *lot* faster than launching a browser.
        abs_pos.y += m_charcell_h;      // move it down a row to not obscure the err
        explainError(errcode, abs_pos);
    #endif
    }

#ifdef REVIEW_ALL_ERROR_MESSAGES
    if (false) {
        for(int ec=0; ec<100; ec++) {
            wxString ecs;
            ecs.Printf("%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        ExplainError("=1", abs_pos);
        ExplainError("=2", abs_pos);
        ExplainError("=3", abs_pos);
    } else {
        int ec;
        for(ec=1; ec<9; ec++) {
            wxString ecs;
            ecs.Printf("A%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=10; ec<29; ec++) {
            wxString ecs;
            ecs.Printf("S%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=32; ec<59; ec++) {
            wxString ecs;
            ecs.Printf("P%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=60; ec<69; ec++) {
            wxString ecs;
            ecs.Printf("C%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=70; ec<77; ec++) {
            wxString ecs;
            ecs.Printf("X%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=80; ec<89; ec++) {
            wxString ecs;
            ecs.Printf("D%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for(ec=190; ec<199; ec++) {
            wxString ecs;
            ecs.Printf("%03d", ec);
            ExplainError(ecs, abs_pos);
        }
    }
#endif

    SetFocus(); // recapture focus
}


// do the dialog part of the error message decoder
void
Crt::explainError(const std::string &errcode, const wxPoint &orig)
{
    // launch it as a popup
    CrtErrorDlg dlg(this, errcode, wxPoint(orig.x, orig.y));
    (void)dlg.ShowModal();
}


// any time the screen size changes or the font options change,
// we recalculate where the active part of the screen.
void
Crt::recalcBorders()
{
    // figure out where the active drawing area is
    int width  = m_charcell_w*m_chars_w;
    int height = m_charcell_h*m_chars_h2;
    int orig_x = (width  < m_scrpix_w) ? (m_scrpix_w-width)/2  : 0;
    int orig_y = (height < m_scrpix_h) ? (m_scrpix_h-height)/2 : 0;

    assert(width >= 0 && width < 4096 && height >= 0 && height < 4096);

    m_RCscreen.SetX(orig_x);
    m_RCscreen.SetY(orig_y);
    m_RCscreen.SetWidth(width);
    m_RCscreen.SetHeight(height);

    // resize the bitmap for the new screen dimensions.
    // we can skip the malloc when the user simply changes the screen
    // size, or changes the font color, contrast, or brightness.
    // only a font change requires a new wxBitmap.
    if ( !m_scrbits.IsOk() || (m_scrbits.GetWidth()  != width)  ||
                              (m_scrbits.GetHeight() != height) ) {
#if !(__WXMAC__) && DRAW_WITH_RAWBMP
        m_scrbits = wxBitmap(width, height, 24);
#else
        m_scrbits = wxBitmap(width, height, wxBITMAP_SCREEN_DEPTH);
#endif
    }
}


void
Crt::setCursorX(int x)
    { m_curs_x = x; }

void
Crt::setCursorY(int y)
    { m_curs_y = y; }

// advance the cursor in y
void
Crt::adjustCursorY(int delta)
{
    m_curs_y += delta;
    if (m_curs_y >= m_chars_h) {
        m_curs_y = m_chars_h-1;
        if (m_screen_type != UI_SCREEN_2236DE) {
            m_curs_x = 0;   // yes, scrolling has this effect
        }
        scr_scroll();
    } else if (m_curs_y < 0) {
        m_curs_y = m_chars_h-1;     // wrap around
    }
}

// move cursor left or right
void
Crt::adjustCursorX(int delta)
{
    m_curs_x += delta;
    if (m_curs_x >= m_chars_w) {
        m_curs_x = 0;
    } else if (m_curs_x < 0) {
        m_curs_x = m_chars_w - 1;
    }
}


// clear the display; home the cursor
void
Crt::scr_clear()
{
    char fillval = ' ';

    for(int y=0; y<m_chars_h; y++) {
    for(int x=0; x<m_chars_w; x++) {
        scr_write_char(x, y, fillval);
    }}

    if (m_screen_type == UI_SCREEN_2236DE) {
        uint8 attr = 0;
        // take care of the 25th row
        for(int x=0; x<m_chars_w; x++) {
            scr_write_char(x, 24, fillval);
        }
        for(int y=0; y<25; y++) {
        for(int x=0; x<80; x++) {
            scr_write_attr(x, y, attr);
        }}
    }

    setCursorX(0);
    setCursorY(0);
}


// scroll the contents of the screen up one row, and fill the new
// row with blanks.
void
Crt::scr_scroll()
{
    uint8 *d  = m_display;                     // first char of row 0
    uint8 *s  = d + m_chars_w;                 // first char of row 1
    uint8 *d2 = d + m_chars_w*(m_chars_h2-1);  // first char of last row

    // scroll up the data
    (void)memcpy(d, s, (m_chars_h2-1)*m_chars_w);
    // blank the last line
    (void)memset(d2, ' ', m_chars_w);

    // cake care of the attribute plane
    if (m_screen_type == UI_SCREEN_2236DE) {
        d  = (uint8*)m_attr;                // first char of row 0
        s  = d + m_chars_w;                 // first char of row 1
        d2 = d + m_chars_w*(m_chars_h2-1);  // first char of last row
        uint8 attr_fill = 0;

        // scroll up the data
        (void)memcpy(d, s, (m_chars_h2-1)*m_chars_w);
        // blank the last line
        (void)memset(d2, attr_fill, m_chars_w);
    }
}


// send a byte to the display controller.
// for dumb terminals, there are a handful control codes which have
// side effects, but most characters are simply poked into the current
// cursor location and the cursor advances.
//
// But the 2236DE recognizes certain bytes as escapes which trigger
// a command sequence or more complex behavior.  At the top level there
// is the unpacking of compressed character sequence (run length encoded).

// first level: FB-escape sequences (mostly char run decompression)
// some of the escape sequence information was gleaned from this document:
//     2536DWTerminalAndTerminalControllerProtocol.pdf
void
Crt::processChar(uint8 byte)
{
//dbglog("Crt::processChar(0x%02x), m_raw_cnt=%d\n", byte, m_raw_cnt);
    if (m_screen_type != UI_SCREEN_2236DE) {
        processChar3(byte);
        return;
    }
    //wxLogMessage("processChar(0x%02x)", byte);

    if ((m_raw_cnt == 0) && (byte == 0xFB)) {
        // start a FB ... sequence
        m_raw_buf[0] = 0xFB;
        m_raw_cnt = 1;
        return;
    }
    if (m_raw_cnt == 0) {
        // pass it through
        processChar2(byte);
        return;
    }
    // keep accumulating command bytes
    assert(m_raw_cnt < sizeof(m_raw_buf));
    m_raw_buf[m_raw_cnt++] = byte;

    // it is a character run sequence: FB nn cc
    // where nn is the repetition count, and cc is the character
    if (m_raw_cnt == 3) {
//dbglog("Decompress run: cnt=%d, chr=0x%02x\n", m_raw_buf[1], m_raw_buf[2]);
        for(int i=0; i<m_raw_buf[1]; i++) {
            processChar2(m_raw_buf[2]);
        }
        m_raw_cnt = 0;
        return;
    }

    // this must be the case we are considering at this point
    assert(m_raw_cnt == 2);

    // FB nn, where 0x02 < count < 0x60, is a three byte sequence
    if ((0x02 < m_raw_buf[1]) && (m_raw_buf[1] < 0x60)) {
        return;
    }

    // FB nn, where nn >= 0x60 represents (nn-0x60) spaces in a row
    if ((0x60 <= m_raw_buf[1]) && (m_raw_buf[1] <= 0xBF)) {
//dbglog("Decompress spaces: cnt=%d\n", m_raw_buf[1]-0x60);
        for(int i=0; i<m_raw_buf[1]-0x60; i++) {
            processChar2(0x20);
        }
        m_raw_cnt = 0;
        return;
    }

    // check for delay sequence: FB Cn
    if ((0xC1 <= m_raw_buf[1]) && (m_raw_buf[1] <= 0xC9)) {
        int delay_ms = 1000 * ((int)m_raw_buf[1] - 0xC0) / 6;
        // FIXME: doing this requires creating a flow control mechanism
        // to block further input, and creating a timer to expire N/6 seconds
//dbglog("Delay sequence: cnt=%d\n", m_raw_buf[1]);
        delay_ms = delay_ms;  // defeat linter
        m_raw_cnt = 0;
        return;
    }

    // check for literal 0xFB byte: FB D0
    if (m_raw_buf[1] == 0xD0) {
//dbglog("Literal 0xFB byte\n");
        processChar2(0xFB);
        m_raw_cnt = 0;
        return;
    }

    // disable cursor blink (FB F8)
    // turning off blink does not re-enable the cursor
    if (m_raw_buf[1] == 0xF8) {
        if (m_curs_attr == cursor_attr_t::CURSOR_BLINK) {
            m_curs_attr = cursor_attr_t::CURSOR_ON;
        }
        m_raw_cnt = 0;
        return;
    }

    // enable cursor blink (FB FC)
    // if the cursor was off to begin with, it remains off
    if (m_raw_buf[1] == 0xFC) {
        if (m_curs_attr == cursor_attr_t::CURSOR_ON) {
            m_curs_attr = cursor_attr_t::CURSOR_BLINK;
        }
        m_raw_cnt = 0;
        return;
    }

    // FIXME: immediate commands
    // FB F0: route subsequent data to CRT
    // FB F1: route subsequent data to Printer
    // FB F2: restart terminal (see pdf page 3)
    // FB F6: reset CRT only
    // holy cow: "Immediate commands may even be transmitted in the middle
    //            of a non-immediate command sequence"
    //           <escape><count> (normally the repeat char arrives next)
    //           <escape><switch to printer><printer data...><escape><switch to CRT>
    //           <repeat character>

    // TODO: what should happen with illegal sequences?
    // for now, I'm passing them through
//dbglog("Unexpected sequence: 0x%02x 0x%02x\n", m_raw_buf[0], m_raw_buf[1]);
    processChar2(m_raw_buf[0]);
    processChar2(m_raw_buf[1]);
    m_raw_cnt = 0;
}


// second level command stream interpretation, for 2236DE type
void
Crt::processChar2(uint8 byte)
{
    assert(m_screen_type == UI_SCREEN_2236DE);
    //wxLogMessage("processChar2(0x%02x), m_input_cnt=%d", byte, m_input_cnt);

    assert( m_input_cnt < sizeof(m_input_buf) );

    if (m_input_cnt == 0) {
        switch (byte) {
        case 0x02:  // character attribute, draw/erase box
            m_input_buf[0] = byte;
            m_input_cnt = 1;
            return;
        case 0x0D:  // carriage return
            // this has the side effect of disabling attributes if in temp 0E mode
            m_attr_temp = false;
            processChar3(0x0D);
            return;
        case 0x0E:  // enable attributes
            m_attr_on   = false;  // after 04 xx yy 0E, 0E changes to temp mode?
            m_attr_temp = true;
//          UI_Info("attrs: 0E --> %02x, %d", m_attrs, m_attr_under);
            return;
        case 0x0F:  // disable attributes
            m_attr_on   = false;
            m_attr_temp = false;
//          UI_Info("attrs: 0F");
            return;
        default:
            // pass through
            processChar3(byte);
            return;
        }
    }

    // accumulate this byte on the current command string
    m_input_buf[m_input_cnt++] = byte;

    // look for completed 02 ... command sequences
    assert(m_input_cnt > 0 && m_input_buf[0] == 0x02);

    // cursor blink enable: 02 05 0F
    if (m_input_cnt == 3 && m_input_buf[1] == 0x05
                         && m_input_buf[2] == 0x0F) {
        m_curs_attr = cursor_attr_t::CURSOR_BLINK;
        m_input_cnt = 0;
        return;
    }
    // unrecognized 02 05 xx sequence
    if (m_input_cnt == 3 && m_input_buf[1] == 0x05) {
        // TODO: just ignore?
        m_input_cnt = 0;
        return;
    }

    // normal character set: 02 02 00 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02
                         && m_input_buf[2] == 0x00
                         && m_input_buf[3] == 0x0F) {
// wxLogMessage("esc: 02 02 00 0F --> ALT disabled");
//      UI_Info("esc: 02 02 00 0F --> ALT disabled");
        m_attrs &= ~(char_attr_t::CHAR_ATTR_ALT);
        m_input_cnt = 0;
        return;
    }
    // alternate character set: 02 02 02 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02
                         && m_input_buf[2] == 0x02
                         && m_input_buf[3] == 0x0F) {
// wxLogMessage("esc: 02 02 02 0F --> ALT enabled");
//      UI_Info("esc: 02 02 02 0F --> ALT enabled");
        m_attrs |= (char_attr_t::CHAR_ATTR_ALT);
        m_input_cnt = 0;
        return;
    }
    // unrecognized 02 02 xx yy sequence
    if (m_input_cnt == 4 && m_input_buf[1] == 0x02) {
        // TODO: just ignore?
        m_input_cnt = 0;
        return;
    }

    // defined attributes, possibly enabling them: 02 04 xx yy {0E|0F}
    if (m_input_cnt == 3 && m_input_buf[1] == 0x04) {
        if (m_input_buf[2] != 0x00 && m_input_buf[2] != 0x02 &&
            m_input_buf[2] != 0x04 && m_input_buf[2] != 0x0B) {
            m_input_cnt = 0;  // TODO: ignore?
        }
        return;
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x04) {
        if (m_input_buf[3] != 0x00 && m_input_buf[3] != 0x02 &&
            m_input_buf[3] != 0x04 && m_input_buf[3] != 0x0B) {
            m_input_cnt = 0;  // TODO: ignore?
        }
        return;
    }
    if (m_input_cnt == 5 && m_input_buf[1] == 0x04) {
        m_input_cnt = 0;
        if (m_input_buf[4] != 0x0E && m_input_buf[4] != 0x0F) {
            return;  // TODO: ignore?
        }
        m_attrs &= ~( char_attr_t::CHAR_ATTR_BRIGHT |
                      char_attr_t::CHAR_ATTR_BLINK  |
                      char_attr_t::CHAR_ATTR_INV    );
        m_attr_under = false;
        if (m_input_buf[2] == 0x02 || m_input_buf[2] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_BRIGHT);
        }
        if (m_input_buf[2] == 0x04 || m_input_buf[2] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_BLINK);
        }
        if (m_input_buf[3] == 0x02 || m_input_buf[3] == 0x0B) {
            m_attrs |= (char_attr_t::CHAR_ATTR_INV);
        }
        if (m_input_buf[3] == 0x04 || m_input_buf[3] == 0x0B) {
            m_attr_under = true;
        }
        m_attr_on   = (m_input_buf[4] == 0x0E);
        m_attr_temp = false;
//      UI_Info("attrs: 02 04 %02x %02x %02x --> %02x, %d", m_input_buf[2], m_input_buf[3], m_input_buf[4], m_attrs, m_attr_under);
        return;
    }

    // return self-ID string: 02 08 09 0F
    if (m_input_cnt == 4 && m_input_buf[1] == 0x08
                         && m_input_buf[2] == 0x09
                         && m_input_buf[3] == 0x0F) {
        m_input_cnt = 0;
        // FIXME: implement this, using id_string
        UI_Info("terminal self-ID query not yet emulated");
        return;
    }
    // 02 08 xx yy is otherwise undefined
    if (m_input_cnt == 4 && m_input_buf[1] == 0x08) {
        m_input_cnt = 0;
        return;  // TODO: ignore it?
    }

    // draw/erase box mode
    // the implementation is hinky: keep the box mode prefix then erase the
    // most recent box mode "verb" after performing it, since the total box
    // command string can be very long, and there is no need to keep all of it.
    if (m_input_cnt == 3 && m_input_buf[1] == 0x0B) {
        if (m_input_buf[2] != 0x02 && m_input_buf[2] != 0x0B) {
            // must start either 02 0B 02, or 02 0B 0B
            m_input_cnt = 0;
            return;  // TODO: ignore it?
        }
        // this flag is used during 08 sub-commands
        // we draw the bottom only if we've seen a previous 0B command
        m_box_bottom = false;
        return;
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x0B) {
        bool box_draw = (m_input_buf[2] == 0x02);
        m_input_cnt--;  // drop current command byte
        switch (byte) {
            case 0x08: // move left
                // draw top left  side of char below old position
                // draw top right side of char below new position
                if (m_box_bottom) {
                    setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_LEFT, +1);
                }
                adjustCursorX(-1);
                if (m_box_bottom) {
                    setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_RIGHT, +1);
                }
                return;
            case 0x09: // move right
                // draw top right side at old position
                // draw top left  side at new position
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_RIGHT);
                adjustCursorX(+1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_LEFT);
                return;
            case 0x0A: // move down one line and draw vertical line
                adjustCursorY(+1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                return;
            case 0x0B: // draw vertical line at current position
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                m_box_bottom = true;  // subsequent 08 must draw bottom
                return;
            case 0x0C: // draw vertical line at current position
                adjustCursorY(-1);
                setBoxAttr(box_draw, char_attr_t::CHAR_ATTR_VERT);
                return;
            case 0x0F: // end of box mode
                m_input_cnt = 0;
                return;
            default:
                // TODO: just ignore error and drop out of box mode?
                m_input_cnt = 0;
                return;
        }
    }

    // reinitialize terminal: 02 0D 0C 03 0F
    // 2336DW_InteractiveTerminalUserManual.700-7636.11-82.pdf says this:
    //   1. Clears the screen, homes the cursor and turns the cursor on
    //   2. Selects normal intensity characters
    //   3. Selects bright as attribute to be activated by HEX(0E)
    //   4. Select the default character set for that version of terminal
    if (m_input_cnt == 3 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] != 0x0C) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
    if (m_input_cnt == 4 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] == 0x0C
                         && m_input_buf[3] != 0x03) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
    if (m_input_cnt == 5 && m_input_buf[1] == 0x0D
                         && m_input_buf[2] == 0x0C
                         && m_input_buf[3] == 0x03
                         && m_input_buf[4] == 0x0F) {
        m_input_cnt = 0;
        scr_clear();
        reset(false);
        return;
    }

    if (m_input_cnt >= 5) {
        m_input_cnt = 0;
        return; // TODO: just ignore it?
    }
}


// lowest level command stream interpretation
void
Crt::processChar3(uint8 byte)
{
    // true of old 64x16 display controller, which had only 7b buffer
    // byte &= 0x7F;

    switch (byte) {

        case 0x00: case 0x02: case 0x04:
        case 0x0B: case 0x0E: case 0x0F:
            // ignored
            break;

        case 0x01:      // home screen
            setCursorX(0);
            setCursorY(0);
            break;

        case 0x03:      // clear screen
            scr_clear();
            break;

        case 0x05:      // enable cursor
            m_curs_attr = cursor_attr_t::CURSOR_ON;
            break;

        case 0x06:      // disable cursor
            m_curs_attr = cursor_attr_t::CURSOR_OFF;
            break;

        case 0x07:      // bell
            if (!m_beep) {
                wxBell();
            } else {
                m_beep->Stop();
                m_beep->Play(wxSOUND_ASYNC);
            }
            break;

        case 0x08:      // cursor left
            adjustCursorX(-1);
            break;

        case 0x09:      // horizontal tab
            adjustCursorX(+1);
            break;

        case 0x0A:      // linefeed (go down one column)
            adjustCursorY(+1);
            break;

        case 0x0C:      // reverse index (cursor up)
            adjustCursorY(-1);
            break;

        case 0x0D:      // go to first column
            setCursorX(0);
            break;

        default:        // just a character
            assert(byte >= 0x10);
#if 0
            // this is true for the old 64x16 display generator, not 80x24
            if (0x10 <= byte && byte <= 0x1F) {
                byte = byte + 0x40;     // 0x10 aliases into 0x50, etc.
            }
#endif
            bool use_alt_char = (byte >= 0x80)
                             && (m_attrs & char_attr_t::CHAR_ATTR_ALT);

            bool use_underline = ((byte >= 0x90) && !use_alt_char)
                              || (m_attr_under && (m_attr_on || m_attr_temp));

            byte = (byte & 0x7F)
                 | ((use_underline) ? 0x80 : 0x00);

            scr_write_char(m_curs_x, m_curs_y, byte);

            // update char attributes in screen buffer
            int old = m_attr[m_chars_w*m_curs_y + m_curs_x]
                    & ( char_attr_t::CHAR_ATTR_LEFT  |
                        char_attr_t::CHAR_ATTR_RIGHT |
                        char_attr_t::CHAR_ATTR_VERT  );

            int attr_mask = 0;
            if (!m_attr_on && !m_attr_temp) {
                attr_mask |= (char_attr_t::CHAR_ATTR_BLINK)
                          |  (char_attr_t::CHAR_ATTR_BRIGHT)
                          |  (char_attr_t::CHAR_ATTR_INV);
            }
            if (!use_alt_char) {
                attr_mask |= (char_attr_t::CHAR_ATTR_ALT);
            }

            scr_write_attr(m_curs_x, m_curs_y, old | (m_attrs & ~attr_mask));
            adjustCursorX(+1);
            break;
    }
    setDirty();
}

// RIFF WAV file format
//  __________________________
// | RIFF WAVE Chunk          |
// |   groupID  = 'RIFF'      |
// |   riffType = 'WAVE'      |
// |    __________________    |
// |   | Format Chunk     |   |
// |   |   ckID = 'fmt '  |   |
// |   |__________________|   |
// |    __________________    |
// |   | Sound Data Chunk |   |
// |   |   ckID = 'data'  |   |
// |   |__________________|   |
// |__________________________|
//
// although it is legal to have more than one data chunk, this
// program assumes there is only one.

const uint32 riffID = ('R' << 24) | ('I' << 16) | ('F' << 8) | ('F' << 0);
const uint32 waveID = ('W' << 24) | ('A' << 16) | ('V' << 8) | ('E' << 0);
typedef struct {
    uint32      groupID;        // should be 'RIFF'
    uint32      riffBytes;      // number of bytes in file after this header
    uint32      riffType;       // should be 'WAVE'
} RIFF_t;

const uint32 fmtID = ('f' << 24) | ('m' << 16) | ('t' << 8) | (' ' << 0);
typedef struct {
    uint32      chunkID;        // should be 'fmt '
    int32       chunkSize;      // not including first 8 bytes of header
    int16       formatTag;      // data format
    uint16      channels;       // number of audio channels
    uint32      sampleRate;     // samples/sec
    uint32      bytesPerSec;    // samples/sec * #channels * bytes/sample
    uint16      blockAlign;     // # bytes per (sample*channels)
    uint16      bitsPerSample;
} FormatChunk_t;

const uint32 dataID = ('d' << 24) | ('a' << 16) | ('t' << 8) | ('a' << 0);
typedef struct {
    uint32      chunkID;        // must be 'data'
    int32       chunkSize;      // not including first 8 bytes of header
//  unsigned char data[];       // everything that follows
} DataChunk_t;


// most of the data fields are stored little endian, but the ID tags
// are big endian.
#define LE16(v) wxUINT16_SWAP_ON_BE(v)
#define LE32(v) wxUINT32_SWAP_ON_BE(v)
#define BE16(v) wxUINT16_SWAP_ON_LE(v)
#define BE32(v) wxUINT32_SWAP_ON_LE(v)

// create a beep sound which chr(0x07) produces
void
Crt::create_beep()
{
    RIFF_t        RiffHdr;
    FormatChunk_t FormatHdr;
    DataChunk_t   DataHdr;

    const int sample_rate   = 16000;
    const int samples_naive = sample_rate * 8/60;    // 8 vblank intervals
    const int samples       = (samples_naive & ~3);  // multiple of 4

    int total_bytes = sizeof(RIFF_t)
                    + sizeof(FormatChunk_t)
                    + sizeof(DataChunk_t)
                    + 1*samples;  // 1 byte per sample

    // chunk description
    RiffHdr.groupID   = (uint32)BE32(riffID);
    RiffHdr.riffBytes = (uint32)LE32(total_bytes-8);
    RiffHdr.riffType  = (uint32)BE32(waveID);

    // first subchunk, format description
    FormatHdr.chunkID       = (uint32)BE32(fmtID);
    FormatHdr.chunkSize     = ( int32)LE32(sizeof(FormatHdr)-8);
    FormatHdr.formatTag     = ( int16)LE16(1);  // pcm
    FormatHdr.channels      = (uint16)LE16(1);  // mono
    FormatHdr.sampleRate    = (uint32)LE32(  sample_rate);
    FormatHdr.bytesPerSec   = (uint32)LE32(1*sample_rate);
    FormatHdr.blockAlign    = (uint16)LE16(1);
    FormatHdr.bitsPerSample = (uint16)LE16(8);  // good enough for a beep

    // second subchunk, data
    DataHdr.chunkID   = (uint32)BE32(dataID);
    DataHdr.chunkSize = (uint32)LE32(1*samples);

    // create a contiguous block, copy the headers into it,
    // then append the sample data
//  uint8 *wav = new (nothrow) uint8 [total_bytes];
    uint8 *wav = new uint8 [total_bytes];
    if (wav == nullptr) {
        return;
    }
    uint8 *wp = wav;
    memcpy(wp, &RiffHdr,   sizeof(RiffHdr));    wp += sizeof(RiffHdr);
    memcpy(wp, &FormatHdr, sizeof(FormatHdr));  wp += sizeof(FormatHdr);
    memcpy(wp, &DataHdr,   sizeof(DataHdr));    wp += sizeof(DataHdr);

    // make a clipped sin wave at 1 KHz
    // the frequency has been chosen to match that of my real 2336DE terminal
    float freq_Hz     = 1940.0f;
    float phase_scale = 2*3.14159f * freq_Hz / sample_rate;
    float clip        = 0.70f;  // chop off top/bottom of wave
    float amplitude   = 40.0f;  // loudness
    for(int n=0; n<samples; n++) {
        float s = sin(phase_scale * n);
        s = (s >  clip) ?  clip
          : (s < -clip) ? -clip
                        : s;
        int sample = int(128.0f + amplitude * s);
        *wp++ = sample;
    }

    m_beep = std::make_shared<wxSound>();
    bool success = m_beep->Create(total_bytes, wav);
    if (!success) {
        m_beep = nullptr;
    }

    delete [] wav;
}

// vim: ts=8:et:sw=4:smarttab
