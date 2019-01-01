// This file implements the Crt class.
// Logically, it implements the character generator and CRT tube.
// It is used by the CrtFrame class.

#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between Ui* wxgui modules
#include "UiCrt.h"              // this module's defines
#include "UiCrtErrorDlg.h"      // error code decoder
#include "UiCrtFrame.h"         // this module's owner
#include "TerminalState.h"      // state needed to create crt image

#include <wx/sound.h>           // "beep!"

#define USE_STRETCH_BLIT 0

// ----------------------------------------------------------------------------
// Crt
// ----------------------------------------------------------------------------

enum {
    Timer_Beep = 100,
};

Crt::Crt(CrtFrame *parent, crt_state_t *crt_state) :
    wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize),
    m_parent(parent),
    m_crt_state(crt_state),
    m_frame_count(0),
    m_dirty(true),
    m_font_size(FONT_MATRIX12),
    m_font_dirty(true),         // must be regenerated
    m_charcell_w(1),            // until SetFontSize overrides.  this prevents
    m_charcell_h(1),            // problems when m_scrbits is first allocated.
    m_charcell_sx(1),
    m_charcell_sy(1),
    m_charcell_dy(1),
    m_fg_color(wxColor(0xff, 0xff, 0xff)),
    m_bg_color(wxColor(0x00, 0x00, 0x00)),
    m_display_contrast(100),
    m_display_brightness(0),
    m_screen_pix_w(0),
    m_screen_pix_h(0),
    m_screen_rc(wxRect(0, 0, 0, 0)),
    m_beep(nullptr),
    m_beep_tmr(nullptr)
{
    createBeep();
#if 0
    if (!m_beep) {
        UI_warn("Emulator was unable to create the beep sound.\n"
                "HEX(07) will produce the host bell sound.");
    }
#endif

    m_beep_tmr = std::make_unique<wxTimer>(this, Timer_Beep);

    // event routing table
    Bind(wxEVT_ERASE_BACKGROUND, &Crt::OnEraseBackground, this);
    Bind(wxEVT_PAINT,            &Crt::OnPaint,           this);
    Bind(wxEVT_KEY_DOWN,         &Crt::OnKeyDown,         this);
    Bind(wxEVT_CHAR,             &Crt::OnChar,            this);
    Bind(wxEVT_SIZE,             &Crt::OnSize,            this);
    Bind(wxEVT_LEFT_DCLICK,      &Crt::OnLeftDClick,      this);
    Bind(wxEVT_TIMER,            &Crt::OnTimer,           this, Timer_Beep);
}


void
Crt::setFontDirty(bool dirty)
{
    m_font_dirty = dirty;
    m_dirty |= dirty;

    if (dirty) {
        // invalidate all, not just the text area, to ensure borders get redrawn
        invalidateAll();
    }
}


bool
Crt::isFontDirty() const noexcept
{
    return m_font_dirty;
};


// set the point size of the text as well as the font style.
void
Crt::setFontSize(const int size)
{
    m_font_size = size;
    setFontDirty();
}


int
Crt::getFontSize() const noexcept
{
    return m_font_size;
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
Crt::setColor(const wxColor &FG, const wxColor &BG)
{
    m_fg_color = FG;
    m_bg_color = BG;
    setFontDirty();
}


// if the display has changed, updated it
void
Crt::refreshWindow()
{
    if (isDirty() || m_crt_state->dirty) {
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
            m_screen_pix_w,          // dstWidth
            m_screen_pix_h,          // dstHeight
            &memDC,                  // source
            0,                       // xsrc
            0,                       // ysrc
            m_screen_rc.GetWidth(),  // srcWidth
            m_screen_rc.GetHeight()  // srcHeight
    );
    memDC.SelectObject(wxNullBitmap);
#else
    dc.DrawBitmap(m_scrbits, m_screen_rc.GetX(), m_screen_rc.GetY());

    // draw borders around active text area.
    // if we are doing an incremental update, supposedly
    // this all gets clipped against the damaged region,
    // so we don't actually draw it if it isn't necessary.
    {
        const int left     = m_screen_rc.GetLeft();
        const int top      = m_screen_rc.GetTop();
        const int bottom   = m_screen_rc.GetBottom() + 1;
        const int right    = m_screen_rc.GetRight() + 1;
// hmm, I was wondering how the bottom & right edges were treated.
// dumping various m_screen_rc.GetFoo() calls, I got this example:
//     top=12, bottom=300, height=289.
// Thus, height isn't (bottom-top).  the bottom returned appears to be
// included in the height.  when drawing, though, I think it isn't included.
// When I set up m_screen_rc, I set top and height.  thus, the use of
// Bottom (and Right) are suspect here.  bottom and right are inclusive
// of the active text area.  I fix that by adding 1 to bottom and right.
        const int bottom_h = (m_screen_pix_h - bottom);
        const int right_w  = (m_screen_pix_w - right);

        wxColor bg(intensityToColor(0.0f));
        dc.SetBrush(wxBrush(bg, wxBRUSHSTYLE_SOLID));
        dc.SetPen(wxPen(bg, 1, wxPENSTYLE_SOLID));

        if (top > 0) {  // top border is required
            dc.DrawRectangle(0, 0, m_screen_pix_w, top);
        }
        if (bottom_h > 0) {     // bottom border is required
            dc.DrawRectangle(0, bottom, m_screen_pix_w, bottom_h);
        }
        if (left > 0) { // left border is required
            dc.DrawRectangle(0, top, left, bottom-top);
        }
        if (right_w > 0) {  // right border is required
            dc.DrawRectangle(right, top, right_w, bottom-top);
        }

        dc.SetPen(wxNullPen);
        dc.SetBrush(wxNullBrush);
    }
#endif

    setFrameCount(getFrameCount() + 1);
}


void
Crt::OnSize(wxSizeEvent &event)
{
    int width, height;
    GetClientSize(&width, &height);

    m_screen_pix_w = width;
    m_screen_pix_h = height;

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
    const wxPoint pos = event.GetPosition();  // client window coordinates
    wxPoint abs_pos = ClientToScreen(pos);    // absolute screen coordinates

    if (m_charcell_w == 0 || m_charcell_h == 0) {
        return;
    }

    const int cell_x = (pos.x - m_screen_rc.GetX()) / m_charcell_w;
    const int cell_y = (pos.y - m_screen_rc.GetY()) / m_charcell_h;

    if (cell_x < 0 || cell_x > m_crt_state->chars_w) {
        return;
    }
    if (cell_y < 0 || cell_y > m_crt_state->chars_h) {
        return;
    }

    // scan line to see if it is an error line.  Although most errors
    // are of the form:
    //   Wang BASIC: <spaces>^ERR <number><spaces>
    //            or <spaces>^ERR =<number><spaces>
    //   BASIC-2:    <spaces>^ERR <letter><number><spaces>
    // some are not.  Some have arbitrary garbage on the line before
    // the appearance of the "^ERR ..." string.

    // first char of row
    char *p = reinterpret_cast<char *>(&m_crt_state->display[cell_y * m_crt_state->chars_w]);
    // one past final char of row
    const char *e = (p + m_crt_state->chars_w);

    // scan entire line looking for first appearance of one of these forms.
    // Wang BASIC:
    //    ^ERR <number><spaces>
    //    ^ERR =<number><spaces>
    // BASIC-2:
    //    ^ERR (<letter>|'=')*<number>+
    // this scanner is lax and scans for both with this pattern:
    for ( ; p < e-7; p++) {

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
        if (isdigit(static_cast<unsigned char>(*pp)) == 0) {
            continue;
        }

        // grab the number
        while ((pp < e) && (isdigit(static_cast<unsigned char>(*pp)) != 0)) {
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
        for (int ec=0; ec<100; ec++) {
            wxString ecs;
            ecs.Printf("%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        ExplainError("=1", abs_pos);
        ExplainError("=2", abs_pos);
        ExplainError("=3", abs_pos);
    } else {
        int ec;
        for (ec=1; ec<9; ec++) {
            wxString ecs;
            ecs.Printf("A%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=10; ec<29; ec++) {
            wxString ecs;
            ecs.Printf("S%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=32; ec<59; ec++) {
            wxString ecs;
            ecs.Printf("P%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=60; ec<69; ec++) {
            wxString ecs;
            ecs.Printf("C%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=70; ec<77; ec++) {
            wxString ecs;
            ecs.Printf("X%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=80; ec<89; ec++) {
            wxString ecs;
            ecs.Printf("D%02d", ec);
            ExplainError(ecs, abs_pos);
        }
        for (ec=190; ec<199; ec++) {
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
    dlg.ShowModal();
}


// any time the screen size changes or the font options change,
// we recalculate where the active part of the screen.
void
Crt::recalcBorders()
{
    // figure out where the active drawing area is
    const int width  = m_charcell_w*m_crt_state->chars_w;
    const int height = m_charcell_h*m_crt_state->chars_h2;
    const int orig_x = (width  < m_screen_pix_w) ? (m_screen_pix_w-width)/2  : 0;
    const int orig_y = (height < m_screen_pix_h) ? (m_screen_pix_h-height)/2 : 0;

    assert(width >= 0 && width < 4096 && height >= 0 && height < 4096);

    m_screen_rc.SetX(orig_x);
    m_screen_rc.SetY(orig_y);
    m_screen_rc.SetWidth(width);
    m_screen_rc.SetHeight(height);

    // resize the bitmap for the new screen dimensions.
    // we can skip the malloc when the user simply changes the screen
    // size, or changes the font color, contrast, or brightness.
    // only a font change requires a new wxBitmap.
    if (!m_scrbits.IsOk() || (m_scrbits.GetWidth()  != width)  ||
                             (m_scrbits.GetHeight() != height)) {
#if !(__WXMAC__) && DRAW_WITH_RAWBMP
        m_scrbits = wxBitmap(width, height, 24);
#else
        m_scrbits = wxBitmap(width, height, wxBITMAP_SCREEN_DEPTH);
#endif
    }
}


void
Crt::ding()
{
    if (!m_beep) {
        wxBell();
    } else {
        if (!m_beep_tmr->IsRunning()) {
            // start it going
            m_beep->Play(wxSOUND_ASYNC | wxSOUND_LOOP);
        }
        // schedule the beep end time
        m_beep_tmr->Start(100, wxTIMER_ONE_SHOT);
    }
}


// the beep timer is has ended
void
Crt::OnTimer(wxTimerEvent &event)
{
    if (event.GetId() == Timer_Beep) {
        m_beep->Stop();
    }
}

// ----------------------------------------------------------------------------
// RIFF WAV file format
// ----------------------------------------------------------------------------
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
struct RIFF_t {
    uint32      groupID;        // should be 'RIFF'
    uint32      riffBytes;      // number of bytes in file after this header
    uint32      riffType;       // should be 'WAVE'
};

const uint32 fmtID = ('f' << 24) | ('m' << 16) | ('t' << 8) | (' ' << 0);
struct FormatChunk_t {
    uint32      chunkID;        // should be 'fmt '
    int32       chunkSize;      // not including first 8 bytes of header
    int16       formatTag;      // data format
    uint16      channels;       // number of audio channels
    uint32      sampleRate;     // samples/sec
    uint32      bytesPerSec;    // samples/sec * #channels * bytes/sample
    uint16      blockAlign;     // # bytes per (sample*channels)
    uint16      bitsPerSample;
};

const uint32 dataID = ('d' << 24) | ('a' << 16) | ('t' << 8) | ('a' << 0);
struct DataChunk_t {
    uint32      chunkID;        // must be 'data'
    int32       chunkSize;      // not including first 8 bytes of header
//  unsigned char data[];       // everything that follows
};


// most of the data fields are stored little endian, but the ID tags
// are big endian.
#define LE16(v) wxUINT16_SWAP_ON_BE(v)
#define LE32(v) wxUINT32_SWAP_ON_BE(v)
#define BE16(v) wxUINT16_SWAP_ON_LE(v)
#define BE32(v) wxUINT32_SWAP_ON_LE(v)

// create a beep sound which chr(0x07) produces
void
Crt::createBeep()
{
    const float sample_rate = 44100.0f;

    // the schematics for the dumb terminal seem to show about a 1100 Hz tone,
    // while the 2336 I have on hand seems to be about 1940 Hz
    const float target_freq =
        (m_crt_state->screen_type == UI_SCREEN_2236DE) ? 1940.0f  // 2336
                                                       : 1100.0f; // dumb crt schematics indicate this

    // we want the buffer to have an integral number of complete cycles
    // so fudge the buffer size to make that happen
    const int   cycles_per_tenth = static_cast<int>(target_freq / 10.0f);
    const int   num_samples = static_cast<int>(sample_rate * 0.1f);  // 1/10th of a second
    const float act_freq    = 10.0 * cycles_per_tenth;

    const int total_bytes = sizeof(RIFF_t)
                          + sizeof(FormatChunk_t)
                          + sizeof(DataChunk_t)
                          + 1*num_samples;  // 1 byte per sample

    // chunk description
    RIFF_t RiffHdr;
    RiffHdr.groupID   = static_cast<uint32>(BE32(riffID));
    RiffHdr.riffBytes = static_cast<uint32>(LE32(total_bytes-8));
    RiffHdr.riffType  = static_cast<uint32>(BE32(waveID));

    // first subchunk, format description
    FormatChunk_t FormatHdr;
    FormatHdr.chunkID       = static_cast<uint32>(BE32(fmtID));
    FormatHdr.chunkSize     = static_cast< int32>(LE32(sizeof(FormatHdr)-8));
    FormatHdr.formatTag     = static_cast< int16>(LE16(1));  // pcm
    FormatHdr.channels      = static_cast<uint16>(LE16(1));  // mono
    FormatHdr.sampleRate    = static_cast<uint32>(LE32(  sample_rate));
    FormatHdr.bytesPerSec   = static_cast<uint32>(LE32(1*sample_rate));
    FormatHdr.blockAlign    = static_cast<uint16>(LE16(1));
    FormatHdr.bitsPerSample = static_cast<uint16>(LE16(8));  // good enough for a beep

    // second subchunk, data
    DataChunk_t DataHdr;
    DataHdr.chunkID   = static_cast<uint32>(BE32(dataID));
    DataHdr.chunkSize = static_cast<uint32>(LE32(1*num_samples));

    // create a contiguous block, copy the headers into it,
    // then append the sample data
    uint8 *wav = new uint8 [total_bytes];
    if (wav == nullptr) {
        return;
    }
    uint8 *wp = wav;
    memcpy(wp, &RiffHdr,   sizeof(RiffHdr));    wp += sizeof(RiffHdr);
    memcpy(wp, &FormatHdr, sizeof(FormatHdr));  wp += sizeof(FormatHdr);
    memcpy(wp, &DataHdr,   sizeof(DataHdr));    wp += sizeof(DataHdr);

    // make a clipped sine wave. the nominal 1940 Hz frequency was chosen
    // to match my real 2336DE terminal. the frequency generated is stretched
    // to ensure an exact number of cycles is generated to produce seamless
    // looping.
    const float phase_scale = 2*3.14159f * act_freq / sample_rate;
    const float clip        = 0.70f;  // chop off top/bottom of wave
    const float amplitude   = 40.0f;  // loudness
    for (int n=0; n<num_samples; n++) {
        float s = sin(phase_scale * n);
        s = (s >  clip) ?  clip
          : (s < -clip) ? -clip
                        : s;
        int sample = static_cast<int>(128.0f + amplitude * s);
        *wp++ = sample;
    }

    m_beep = std::make_unique<wxSound>();
    const bool success = m_beep->Create(total_bytes, wav);
    if (!success) {
        m_beep = nullptr;
    }

    delete [] wav;
}

// vim: ts=8:et:sw=4:smarttab
