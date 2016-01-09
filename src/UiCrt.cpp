// This file implements the Crt class.
// Logically, it implements the character generator and CRT tube.
// It is used by the CrtFrame class.
//
// To eliminate flashing, text is drawn to a pre-allocated bitmap,
// m_scrbits.  Once the full screen image has been constructed,
// it gets squirted out to the screen in a single operation.
//
// There are a few different ways that text can get drawn to this screen
// bitmap, m_scrbits.
//
//   1) Strings of characters are drawn using DrawText().  Because the
//      system font doesn't have the same characters that the original
//      Wang had, an attempt is made to map to something graphically
//      similar, but in some cases there is no good match.
//
//   2) Using the rawbmp.h interface, a bunch of for() loops sweeps through
//      the 64x16 or 80x24 character array and draws each character glyph
//      manually.  This "font map" containing these glyphs is described below.
//
//   3) Using a bitblt for each character, the right glyph from the "font map"
//      is copied to the right place in m_scrbits.
//
// The font map is a wide bitmap consisting of the image of each of the 256
// possible characters.  The glyph images come either from a system font,
// or a scaled version of the real Wang character generator dot matrix image.
// These two methods are equally efficient since the work of building the
// fontmap is done only once, when the font style is changed.
//
// Which one is faster is system and test case dependent.  However, on my
// system, (2.1 GHz AMD Athlon, nVidia GeForce 7600 GPU), scheme #2 is slowest,
// as the rawbmp interface must convert from DDB to DIB and back, which is
// slow.  #1 and #3 are about the same speed, but #3 using the dot matrix
// font wins on the basis of nostalgia.

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between Ui* wxgui modules
#include "UiCrt.h"              // this module's defines
#include "UiCrt_Charset.h"      // wang character generator bitmaps
#include "UiCrtErrorDlg.h"      // error code decoder
#include "UiCrtFrame.h"         // this module's owner

#include <wx/image.h>           // required only for blur hack
#include <wx/rawbmp.h>          // for direct bitmap manipulation

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

wxLog *g_logger = nullptr;

Crt::Crt(CrtFrame *parent, int screen_type) :
    wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize),
    m_parent(parent),
#if 0
    m_screen_type(UI_SCREEN_2236DE),  // FIXME: just a hack to test 2236 emulation behavior
#else
    m_screen_type(screen_type),
#endif
    m_chars_w((m_screen_type == UI_SCREEN_64x16) ? 64 : 80),
    m_chars_h((m_screen_type == UI_SCREEN_64x16) ? 16 : 24),
    m_chars_h2((m_screen_type == UI_SCREEN_2236DE) ? 25 : m_chars_h),
    m_font(wxNullFont),         // until SetFontSize() is called
    m_fontsize(FONT_MATRIX12),
    m_fontdirty(true),          // must be regenerated
    m_charcell_w(1),            // until SetFontSize overrides.  this prevents
    m_charcell_h(1),            // problems when m_scrbits is first allocated.
    m_begin_ul(0),
    m_begin_curs(0),
    m_rows_curs(0),
    m_FGcolor( wxColor(0xff,0xff,0xff) ),
    m_BGcolor( wxColor(0x00,0x00,0x00) ),
    m_display_contrast(100),
    m_display_brightness(0),
    m_scrpix_w(0),
    m_scrpix_h(0),
    m_RCscreen(wxRect(0,0,0,0)),
    m_frame_count(0),
    m_beep(nullptr)
{
    if (0) g_logger = new wxLogWindow(m_parent, "Logging window", true, false);
    create_beep();
    if (m_beep == nullptr) {
        UI_Warn("Emulator was unable to create the beep sound.\n"
                "HEX(07) will produce the host bell sound.");
    }
    scr_clear();
    reset();
}


// free resources on destruction
Crt::~Crt()
{
    wxDELETE(g_logger);
    wxDELETE(m_beep);
}

// hardware reset
// mostly needed for intelligent terminal operation
// FIXME: the real terminal doesn't know when a reset happens on
//        the system board.  the host must have a foolproof sequence
//        to reesablish protocol sync with the terminal.
//        Thus, we shouldn't need to expose this.
void
Crt::reset()
{
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

    // model 2236 behavior by emitting ID string
    if (true) {
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


// Take an intensity, ranging from 0.0 to 1.0, and turn it into a display
// color.  All colors in the CRT region should ultimate come from here.
// The function isn't all that fast so if the generated value is expected
// to be used frequently, it should be cached by the caller.
wxColor
Crt::intensityToColor(float f) const
{
    assert(f >= 0.0f && f <= 1.0f);

    const float contrast   = getDisplayContrast()   * 0.01f;
    const float brightness = getDisplayBrightness() * 0.01f;

    bool black_bg = (m_BGcolor.Red()   == 0x00) &&
                    (m_BGcolor.Green() == 0x00) &&
                    (m_BGcolor.Blue()  == 0x00);

    int r, g, b;

    if (black_bg) {
        // We are modeling a monochromatic CRT.
        // Twiddle the intensity and then apply it uniformly.
        float v = brightness + f*contrast;
        v = (v < 0.0f) ? 0.0f
          : (v > 1.0f) ? 1.0f : v;
        r = (int)(v * m_FGcolor.Red()   + 0.5f);
        g = (int)(v * m_FGcolor.Green() + 0.5f);
        b = (int)(v * m_FGcolor.Blue()  + 0.5f);
    } else {
        // FG/BG both have colors.  The monochromatic model doesn't apply.
        // Instead what we do is use the intensity to interpolate between
        // the BG color (f=0.0) and the FG color (f=1.0).  contrast scales
        // the interpolation factor, and brighness adds a constant offset
        // to each component.
        const float weight = f * contrast;
        const float diff_r = weight * (m_FGcolor.Red()   - m_BGcolor.Red());
        const float diff_g = weight * (m_FGcolor.Green() - m_BGcolor.Green());
        const float diff_b = weight * (m_FGcolor.Blue()  - m_BGcolor.Blue());

        r = (int)(m_BGcolor.Red()   + diff_r + (brightness * 255.0) + 0.5f);
        g = (int)(m_BGcolor.Green() + diff_g + (brightness * 256.0) + 0.5f);
        b = (int)(m_BGcolor.Blue()  + diff_b + (brightness * 256.0) + 0.5f);
#define CLAMP8(x) ( ((x)<0x00) ? 0x00 : ((x)>0xFF) ? 0xFF : (x) )
        r = CLAMP8(r);
        g = CLAMP8(g);
        b = CLAMP8(b);
    }

    return wxColor(r,g,b);
}


// regenerate a cache of the font map (required for dot matrix font;
// optional for native font)
//
// Wang character cell layout (10x11 character cell)
//   x = 8x8 active pixel area
//   u = underline or cursor
//   c = cursor
//      |. . . . . . . . . .|
//      |. . . . . . . . . .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |c c c c c c c c c c|
//      |c c u c u c u c u c|
//
// the "cccccccccc" row is drawn on top of the stored character cell, so
// it appears as black pixels in the fontmap.  it is important that the
// active region have this one pixel inactive region so that we can apply
// a 3x3 blurring filter to simulate what the real CRT does -- without it
// the resulting display is painful to read.  the "uuuuuuuuuu" row does
// creep into this margin, but since there is a gap, not filtering it
// exactly right makes adjacent underlined characters have a subtly
// different gap that is hardly noticeable.
//
// The early terminals had 128 characters, plus the msb could be used to
// indicate underline for characters > 0x90.  However, for simplicity
// we generate all 256 characters and not worry about manually underlining.
// The 2236 also offers an alternate upper character set for 0x80-0xFF.
// In the end, we generate 256+128 characters.

// the schematic for the 2336DW has a mapping PROM based on the scanline row.
// it shows that glyph rows 0-7 map to scanlines 2-9.  scanline 10 uses
// glyph row 6.  I believe this is only used in box graphics mode, in which
// case glyph rows 6 and 7 will be identical.

void
Crt::generateFontmap()
{
    wxClientDC dc(this);
    int sx,     // bitmap replication factor in x
        sy,     // bitmap replication factor in y
        dy;     // step in y (allows skipping rows)
    int filter; // which filter kernel to use

    switch (getFontSize()) {
        default: // just in case someone has diddled the ini file with a bad value
        case FONT_MATRIX12:     // this is closest to the original
                sx = 1; sy = 1; dy = 2;
                m_charcell_w = 10*sx;
                m_charcell_h = 11*sy*dy;
                filter = 1; // semi Gaussian
                break;
        case FONT_MATRIX11:
                sx = 1; sy = 1; dy = 1;
                m_charcell_w = 10*sx;
                m_charcell_h = 11*sy*dy;
                filter = 4; // filter in X only
                break;
        case FONT_MATRIX24:
                sx = 2; sy = 2; dy = 2;
                m_charcell_w = 10*sx;
                m_charcell_h = 11*sy*dy;
                filter = 2; // Gaussian
                break;
        case FONT_NATIVE8:
        case FONT_NATIVE10:
        case FONT_NATIVE12:
        case FONT_NATIVE14:
        case FONT_NATIVE18:
        case FONT_NATIVE24:
                m_font = wxFont(getFontSize(), wxFONTFAMILY_MODERN,
                                               wxFONTSTYLE_NORMAL,
                                               wxFONTWEIGHT_NORMAL);
// Experiment: can we use a unicode capable-font to get better
// mapping to wang symbols?
#undef TRY_UNICODE
#ifdef TRY_UNICODE
    #if 0
                // this doesn't seem to help
                m_font = wxFont(getFontSize(), wxFONTFAMILY_SWISS,
                                               wxFONTSTYLE_NORMAL,
                                               wxFONTWEIGHT_NORMAL);
    #else
                // this seems to work!, but it is small
                m_font = *wxNORMAL_FONT;
                m_font.SetPointSize(getFontSize());
    #endif
#endif
                assert(m_font != wxNullFont);
                dc.SetFont( m_font );
                m_charcell_w = dc.GetCharWidth();
                m_charcell_h = dc.GetCharHeight()
                             + 3;  // make room for underline, cursor, blank
                sx = sy = dy = 1; // keep lint happy
                filter = 0;  // no filter, although filtering does look cool too
                break;
    }

    m_begin_ul   = m_charcell_h - 2*sy*dy;
    m_begin_curs = m_charcell_h - 1*sy*dy;
    m_rows_curs  = sy;

    // pick a filter kernel for blurring
    const float *filter_w;
    {
        static const float w_noop[9] = {
            // don't do any filtering
            0.0000,  0.0000,  0.0000,
            0.0000,  1.0000,  0.0000,
            0.0000,  0.0000,  0.0000,
        };
        static const float w_semi_gaussian[9] = {
            // kind of like a Gaussian, but modified to reflect that the
            // smearing of the ideal dot comes about from two sources.
            // first, the dot isn't perfectly focused, so it spreads out
            // radially.  second, the modulation of the beam occurs during
            // the horizontal sweep and this signal has finite bandwidth.
            // thus there should be more horizontal weighting than vertical.
            0.07f,  0.21f,  0.07f,
            0.28f,  0.90f,  0.28f,
            0.07f,  0.21f,  0.07f,
        };
        static const float w_gaussian[9] = {
            // 2D Gaussian
            0.1250f,  0.2500f,  0.1250f,
            0.2500f,  0.5000f,  0.2500f,
            0.1250f,  0.2500f,  0.1250f,
        };
        static const float w_gaussian_tweak[9] = {
            // emphasis on pixel center
            0.1250f,  0.2500f,  0.1250f,
            0.2500f,  0.7500f,  0.2500f,
            0.1250f,  0.2500f,  0.1250f,
        };
        static const float w_1D[9] = {
            // good for filtering 1:1 map
            0.0000f,  0.0000f,  0.0000f,
            0.3300f,  1.0000f,  0.3300f,
            0.0000f,  0.0000f,  0.0000f,
        };
        static const float w_contrast[9] = {
            0.2500f*0.7f,  0.2500f*0.7f,  0.2500f*0.7f,
            0.2500f*0.7f,  1.0000f*0.7f,  0.2500f*0.7f,
            0.2500f*0.7f,  0.2500f*0.7f,  0.2500f*0.7f,
        };

        switch (filter) {
            default: assert(false);
            case 0: filter_w = w_noop;           break;
            case 1: filter_w = w_semi_gaussian;  break;
            case 2: filter_w = w_gaussian;       break;
            case 3: filter_w = w_gaussian_tweak; break;
            case 4: filter_w = w_1D;             break;
            case 5: filter_w = w_contrast;       break;
        }
    }

    // reallocate the bitmap which holds all the glyphs.
    // there are 8 rows of 256 characters; characters 00-7F
    // are not underined, while characters 80-FF are.
    // each row contains one combination of attributes:
    //     row   charset  reverse  intensity
    //     ---   -------  -------  ---------
    //       0 : normal   no       normal
    //       1 : normal   no       normal
    //       2 : normal   yes      high
    //       3 : normal   yes      high
    //       4 : alt      no       normal
    //       5 : alt      no       normal
    //       6 : alt      yes      high
    //       7 : alt      yes      high
    m_fontmap = wxBitmap( 256*m_charcell_w, 8*m_charcell_h, -1);
    wxMemoryDC fdc(m_fontmap);  // create a dc for us to write to

    // allocate a temp bitmap for working on one character.
    // it has a one pixel border all around so we can do 3x3 convolution
    // easily and not worry about the edge cases.
    // char_bitmap is used to receive a real font character.
    // char_image is used to construct a filtered bitmap image.
    const int offset = 1;
    const int img_w = m_charcell_w + 2*offset;
    const int img_h = m_charcell_h + 2*offset;
    wxImage  char_image( img_w, img_h);
    wxBitmap char_bitmap(img_w, img_h, 32);

    wxColor fg(*wxWHITE), bg(*wxBLACK);
    wxMemoryDC charDC;
    charDC.SelectObject(char_bitmap);
    charDC.SetBackground( wxBrush(bg, wxSOLID) );
    charDC.SetPen(wxPen(fg, 0, wxSOLID));
    charDC.SetBrush(wxBrush(fg, wxSOLID));
    charDC.SetBackgroundMode(wxSOLID);
    charDC.SetTextForeground(fg);
    charDC.SetTextBackground(bg);

    // get ready for setting pixels for bitmap
    if (m_font != wxNullFont)
        charDC.SetFont(m_font);

    charDC.SelectObject(wxNullBitmap);

    // boundaries for drawing graphics chars
    int boxx[3] = { 0, m_charcell_w / 2, m_charcell_w };
    int boxy[4] = { 0, m_charcell_h / 3, 2*m_charcell_h/3, m_charcell_h };

    // build a glyph map of the entire character set
    for(int alt=0; alt<2; alt++) {
    for(int inv=0; inv<2; inv++) {
    for(int bright=0; bright<2; bright++) {

        // mapping from filtered image intensity to a color
        wxColor colormap[256];
        {
            int tmp_contrast = getDisplayContrast();
            if (m_screen_type == UI_SCREEN_2236DE) {
                // make normal mode darker and bright brighter
                // to increase the difference between them.
                // inverse video looks bad as the bright swamps the dark, so
                // change the scale factor for inverse video to favor dark.
                // FIXME: this needs to be better wrt inverse video
                float scale = (!bright && !inv)   ? 0.8
                            : ( bright && !inv)   ? 1.7
                            : (!bright &&  inv)   ? 0.4
                            /*( bright &&  inv)*/ : 1.2;
                setDisplayContrast(tmp_contrast * scale);
            }
            for(int n=0; n<256; n++) {
                float w = n * (1.0/256.0);
                wxColor c = intensityToColor(w);
                int nn = (inv) ? 255-n : n;
                colormap[nn].Set(c.Red(), c.Green(), c.Blue());
            }
            setDisplayContrast(tmp_contrast);
        }

        for(int chr=0; chr<256; chr++) {

            int ch = (chr & 0x7F);  // minus underline flag

            if (getFontSize() >= FONT_NATIVE8) {

                // prepare by blanking out everything
                charDC.SelectObject(char_bitmap);
                charDC.Clear();

                if (alt && (ch >= 0x40)) {
                    // box graphics characters
                    for(int yy=0; yy<3; yy++) {
                        for(int xx=0; xx<2; xx++) {
                            int shift = 2*yy + xx;
                            if ((chr >> shift) & 1) {
                                // x,y,w,h
                                charDC.DrawRectangle(
                                    boxx[xx],
                                    boxy[yy],
                                    boxx[xx+1] - boxx[xx] + 1,
                                    boxy[yy+1] - boxy[yy] + 1
                                );
                            }
                        }
                    }
                } else if (alt) {
                    wxString text((wxChar)(xlat_char_alt[ch]));
                    charDC.DrawText(text, offset, offset);
                } else {
                    wxString text((wxChar)(xlat_char[ch]));
#ifdef TRY_UNICODE
// an experiment to use unicode "BLACK LEFT-POINTING TRIANGLE"
text = L"\u25c0";
#endif
                    charDC.DrawText(text, offset, offset);
                }

                // at this level of representation >=0x80 always means underline.
                // underline style doesn't work for all platforms, so we do it
                // manually. not ideal.
                if (chr >= 0x80 && chr <= 0xFF) {
                    for(int yy=0; yy<sy; yy++) {
                        int lft = offset;
                        int rgt = offset + m_charcell_w - 1;
                        int top = offset + m_begin_ul + yy - 1;
                        charDC.DrawLine(lft, top, rgt, top);
                    }
                }

                charDC.SelectObject(wxNullBitmap);  // release char_bitmap
                char_image = char_bitmap.ConvertToImage();

            } else {  // use real bitmap font

                // blank it
                char_image.SetRGB( wxRect(0,0,img_w,img_h), 0x00, 0x00, 0x00 );

                for(int bmr=0; bmr<11; bmr++) {  // bitmap row

                    int pixrow;
                    if (alt && (ch >= 0x40)) {
                        // alt character set, w/block graphics
                        // hardware maps it this way, so we do too
                        pixrow = (bmr <  2) ? chargen_2236_alt[8*ch + 0+(bmr&1)]
                               : (bmr < 10) ? chargen_2236_alt[8*ch + bmr-2]
                                            : chargen_2236_alt[8*ch + 6+(bmr&1)];
                    } else if (alt) {
                        // alt character set
                        pixrow = (bmr <  2) ? 0x00
                               : (bmr < 10) ? chargen_2236_alt[8*ch + bmr-2]
                                            : 0x00;
                    } else {
                        // normal character set
                        pixrow = (bmr <  2) ? 0x00
                               : (bmr < 10) ? chargen[8*ch + bmr-2]
                                            : 0x00;
                    }

                    // pad out to 10 pixel row
                    pixrow <<= 1;
                    if (alt && (ch >= 0x40)) {
                        // block graphics fills the character cell.
                        // from the original bitmap, we pad to the left using
                        // bit 6 (not 7), and we pad to the right using bit 1
                        // (not 0). this is how the hardware does it, because
                        // the bitmaps are not solid; eg, all on (FF) is made
                        // up of rows of nothing but 0x55 bit patterns.
                        pixrow |= ((pixrow << 2) & 0x200)
                               |  ((pixrow >> 2) & 0x001);
                    }

                    // add underline on the last bitmap row
                    // the hardware stipples the underline this way
                    if ((chr >= 0x80) && (bmr == 10)) {
                        pixrow = 0x55 << 1;
                    }

                    for(int bmc=0; bmc<10; pixrow <<= 1, bmc++) {  // bitmap col
                        if (pixrow & 0x200) {
                            // the pixel should be lit; replicate it
                            for(int yy=0; yy<sy; yy++)
                            for(int xx=0; xx<sx; xx++) {
                                char_image.SetRGB(offset + bmc*sx    + xx,
                                                  offset + bmr*sy*dy + yy,
                                                  0xff, 0xff, 0xff);
                            } // for xx,yy
                        } // pixel is lit
                    } // for bmc

                } // for bmr

            } // use Wang bitmap

            // we run a 3x3 convolution kernel on each character of the font map
            // to simulate the limited bandwidth of the real CRT.
            wxImage blur_img(m_charcell_w, m_charcell_h);

            for(int y=0; y<m_charcell_h; y++)
            for(int x=0; x<m_charcell_w; x++) {

                // pick up 3x3 neighborhood around pixel of interest
                int pix[9];
                for(int yy=-1;yy<=1;yy++)
                for(int xx=-1;xx<=1;xx++)
                    // wxImage is always RGB, so we can just pick any one channel
                    pix[(yy+1)*3 + (xx+1)] =
                        char_image.GetBlue(x+offset+xx, y+offset+yy);

                // apply kernel
                int idx = int(
                          filter_w[0]*pix[0] + filter_w[1]*pix[1] + filter_w[2]*pix[2]
                        + filter_w[3]*pix[3] + filter_w[4]*pix[4] + filter_w[5]*pix[5]
                        + filter_w[6]*pix[6] + filter_w[7]*pix[7] + filter_w[8]*pix[8]
                        + 0.5f);  // round

                // clamp
                idx = (idx < 0x00) ? 0x00
                    : (idx > 0xFF) ? 0xFF
                                   : idx;

                const wxColor rgb = colormap[idx]; // colorize
                blur_img.SetRGB(x, y, rgb.Red(), rgb.Green(), rgb.Blue());

            } // for x,y

            // copy it to the final font bitmap
            int row_offset = m_charcell_h * (4*alt + 2*inv + bright);
            fdc.DrawBitmap( wxBitmap(blur_img),    // source image
                            chr*m_charcell_w, row_offset ); // dest x,y

        } // for chr

    } } } // bright, inv, alt

    fdc.SelectObject(wxNullBitmap);  // release m_fontmap

    setFontDirty(false);
}


// if the display has changed, updated it
void
Crt::refreshWindow()
{
    if (isDirty()) {
        invalidateText();
        setDirty(false);
    }
}


// update the bitmap of the screen image
void
Crt::generateScreen()
{
    if (isFontDirty()) {
        generateFontmap();
        recalcBorders();  // the bitmap store might have changed size
    }

    wxColor fg(intensityToColor(1.0f)),  // color of text
            bg(intensityToColor(0.0f));  // color of background

#if DRAW_WITH_RAWBMP
    {
// FIXME: see if we still need this for OSX
// FIXME: it doesn't have box mode overlay
        bool success = generateScreenByRawBmp(fg, bg);
        if (success)
            return;
    }

    // that didn't work, so build image via more platform-independent means
#endif

    wxMemoryDC memDC(m_scrbits);

    // start by blanking out everything
    memDC.SetBackground( wxBrush(bg, wxSOLID) );
    memDC.Clear();

    generateScreenByBlits(memDC);
    generateScreenCursor(memDC);  // FIXME: should this come after overlay?
    if (m_screen_type == UI_SCREEN_2236DE)
        generateScreenOverlay(memDC);

    // release the bitmap
    memDC.SelectObject(wxNullBitmap);
}


// draw each character by blit'ing from the fontmap
void
Crt::generateScreenByBlits(wxMemoryDC &memDC)
{
    // draw each character from the fontmap
    wxMemoryDC fontmapDC;
    fontmapDC.SelectObjectAsSource(m_fontmap);

    bool text_blink_enable = m_parent->getTextBlinkPhase();

    // draw each row of the text
    for(int row=0; row<m_chars_h2; row++) {

        if (m_screen_type == UI_SCREEN_2236DE) {

            for(int col=0; col<m_chars_w; col++) {
                uint8 chr   = m_display[row*m_chars_w + col];
                uint8 attr  =    m_attr[row*m_chars_w + col];
                bool blink  = (attr & char_attr_t::CHAR_ATTR_BLINK)  ? true : false;
                int  alt    = (attr & char_attr_t::CHAR_ATTR_ALT)    ? 4 : 0;
                int  inv    = (attr & char_attr_t::CHAR_ATTR_INV)    ? 2 : 0;
                int  bright = (attr & char_attr_t::CHAR_ATTR_BRIGHT) ? 1 : 0;

                // blinking alternates between normal and bright intensity
                // but intense text can't blink because it is already intense
                if (text_blink_enable && blink)
                    bright = 1;

                if ((chr != 0x20) || inv) {
                    // if (non-blank character)
                    int font_row = m_charcell_h * (alt + inv + bright);
                    memDC.Blit(col*m_charcell_w, row*m_charcell_h,  // dest x,y
                               m_charcell_w, m_charcell_h,          // w,h
                               &fontmapDC,                          // src image
                               chr*m_charcell_w, font_row);         // src x,y
                }
            }

        } else {

            // old terminal: one character set, no attributes
            for(int col=0; col<m_chars_w; col++) {
                int chr = m_display[row*m_chars_w + col];
                if ((chr >= 0x10) && (chr != 0x20)) {  // if (non-blank character)
                    memDC.Blit(col*m_charcell_w, row*m_charcell_h,  // dest x,y
                               m_charcell_w, m_charcell_h,          // w,h
                               &fontmapDC,                          // src image
                               chr*m_charcell_w, 0);                // src x,y
                }
            }
        }
    }

    fontmapDC.SelectObject(wxNullBitmap);
}


void
Crt::generateScreenCursor(wxMemoryDC &memDC)
{
    // FIXME: should this be brighter than 1.0?
    wxColor fg(intensityToColor(1.0f));  // color of cursor

    bool cursor_blink_enable = m_parent->getCursorBlinkPhase();

    // cursor it is drawn one scanline lower than the underline
    // FIXME: it should be two scanlines high, I believe
    if (m_curs_attr == cursor_attr_t::CURSOR_ON  ||
        m_curs_attr == cursor_attr_t::CURSOR_BLINK && cursor_blink_enable
       ) {
        // draw the one actual cursor, wherever it is
        int top   = m_curs_y * m_charcell_h + m_begin_curs;
        int left  = m_curs_x * m_charcell_w;
        int right = left + m_charcell_w - 1;
        memDC.SetPen( wxPen(fg, 1, wxSOLID) );
        for(int yy=0; yy<m_rows_curs; yy++)
            memDC.DrawLine(left,  top + yy,
                           right, top + yy);
    }
}


// draw box/line overlay onto DC
void
Crt::generateScreenOverlay(wxMemoryDC &memDC)
{
    assert(m_screen_type == UI_SCREEN_2236DE);

    int sx, sy;  // scale factor for logical pixel to screen mapping
    switch (getFontSize()) {
        default: // in case someone has diddled the ini file
        case FONT_MATRIX12:     // this is closest to the original
                sx = 1; sy = 2;
                break;
        case FONT_MATRIX11:
                sx = 1; sy = 1;
                break;
        case FONT_MATRIX24:
                sx = 2; sy = 2;
                break;
        case FONT_NATIVE8:
        case FONT_NATIVE10:
        case FONT_NATIVE12:
        case FONT_NATIVE14:
        case FONT_NATIVE18:
        case FONT_NATIVE24:
                sx = sy = 1;
                break;
    }

    wxColor fg(intensityToColor(1.0f));
    memDC.SetBrush(wxBrush(fg, wxSOLID));
    memDC.SetPen(wxPen(fg, 0, wxSOLID));

    // find horizontal runs of lines and draw them
    for(int row=0; row < 25; row++) {
        int off = 80 * row;
        int top = row * m_charcell_h;
        int start = -1;
        for(int col=0; col < 80; col++, off++) {
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_LEFT)) {
                // start or extend
                if (start < 0)
                    start = col*m_charcell_w;
            } else if (start >= 0) {
                // hit the end
                int rgt = col * m_charcell_w;
                memDC.DrawRectangle(start, top, rgt-start+sx, sy);
                start = -1;
            }
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_RIGHT)) {
                if (start < 0)  // start of run
                    start = col*m_charcell_w + (m_charcell_w >> 1);
            } else if (start >= 0) {
                // end of run
                int rgt = col * m_charcell_w + (m_charcell_w >> 1);
                memDC.DrawRectangle(start, top, rgt-start+sx, sy);
                start = -1;
            }
        }
        // draw if we made it all the way to the right side
        if (start >= 0) {
            int rgt = 80 * m_charcell_w;
            memDC.DrawRectangle(start, top, rgt-start+sx, sy);
        }
    }

    // find vertical runs of lines and draw them
    // the 25th line is guaranteed to not have the vert attribute
    for(int col=0; col < 80; col++) {
        int off = col;
        int mid = col * m_charcell_w + (m_charcell_w >> 1);
        int start = -1;
        for(int row=0; row < 25; row++, off += 80) {
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_VERT)) {
                if (start < 0)  // start of run
                    start = row * m_charcell_h;
            } else if (start >= 0) {
                // end of run
                int end = row * m_charcell_h;
                memDC.DrawRectangle(mid, start, sx, end-start+sy);
                start = -1;
            }
        }
    }
}


#if DRAW_WITH_RAWBMP
// update the bitmap of the screen image, using rawbmp interface
// returns false if it fails.
// TODO: random ideas, need to be vetted
//   + add code that when it sees a non-printing character,
//     it looks for a run of such characters and blanks them
//     all in raster scan order, instead of diddle scan.
//   + keep a copy of the ascii array, and do an incremental update
//     based on the character differences.  need to track the cursor too!
bool
Crt::generateScreenByRawBmp(wxColor fg, wxColor bg)
{
    // set to true if a fair portion of the chars on screen are blank.
    // set to false if most characters are non-blank
    const bool pre_clear = false;

    // select how pre_clear is carried out.
    // at least on my machine, using DrawRect is faster.
    const bool clear_with_rawbmp = false;

    // this path is faster only if we are drawing to a 32b surface.
    // this is because the code must commit to using either
    // wxAlphaPixeldata (32b) or wxNativePixelData (24b except under OSX).
    if (m_scrbits.GetDepth() != 32)
        return false;

    // this must be done after isFontDirty(), as it may affect m_scrbits
    const int h = m_scrbits.GetHeight();
    const int w = m_scrbits.GetWidth();

    if (pre_clear && !clear_with_rawbmp) {
        // blank everything, as we skip blank characters
        wxMemoryDC mdc(m_scrbits);
        mdc.SetBackground( wxBrush(bg, wxSOLID) );
        mdc.Clear();
        mdc.SelectObject(wxNullBitmap);
    }

    wxAlphaPixelData raw_screen(m_scrbits);
    if (!raw_screen)
        return false;

    wxAlphaPixelData raw_font(m_fontmap);
    if (!raw_font)
        return false;

    if (pre_clear && clear_with_rawbmp) {
        // blank everything, as we skip blank characters
        wxAlphaPixelData::Iterator p(raw_screen);
        for(int y=0; y<h; ++y) {
            wxAlphaPixelData::Iterator rowStart = p;
            for(int x=0; x<w; ++x) {
                p.Red()   = bg.Red();
                p.Green() = bg.Green();
                p.Blue()  = bg.Blue();
                ++p;
            }
            p = rowStart;
            p.OffsetY(raw_screen, 1);
        }
    }

    // draw the characters (diddlescan order),
    // skipping non-printing characters
    wxAlphaPixelData::Iterator sp(raw_screen);  // screen pointer
    for(int row=0; row<m_chars_h2; ++row) {

        // the upper left corner of the leftmost char of row
        wxAlphaPixelData::Iterator rowUL = sp;

        for(int col=0; col<m_chars_w; ++col) {

            // the upper left corner of the char on the screen
            wxAlphaPixelData::Iterator charUL = sp;

            int ch = m_display[row*m_chars_w + col];
            if (!pre_clear || ((ch >= 0x10) && (ch != 0x20))) { // control or space

                // pick out subimage of current character from the
                // fontmap and copy it to the screen image
                wxAlphaPixelData::Iterator cp(raw_font);
                cp.OffsetX(raw_font, ch*m_charcell_w);

                for(int rr=0; rr<m_charcell_h; ++rr) {

                    // pointers to the start of the current character scanline
                    wxAlphaPixelData::Iterator sRowp = sp;
                    wxAlphaPixelData::Iterator cRowp = cp;

                    for(int cc=0; cc<m_charcell_w; ++cc) {
                        sp.Red()   = cp.Red();
                        sp.Green() = cp.Green();
                        sp.Blue()  = cp.Blue();
                        ++sp;
                        ++cp;
                    }

                    sp = sRowp;  sp.OffsetY(raw_screen, 1);
                    cp = cRowp;  cp.OffsetY(raw_font, 1);
                }

            } // if(drawable character)

            // advance to the next character of row
            sp = charUL;
            sp.OffsetX(raw_screen, m_charcell_w);

        } // for(col)

        // advance to the next row of characters
        sp = rowUL;
        sp.OffsetY(raw_screen, m_charcell_h);

    } // for(row)


    // draw cursor -- there can be only zero/one on screen.
    // it is drawn one scanline lower than the underline
    bool cursor_blink_enable = m_parent->getCursorBlinkPhase();
    if (m_curs_attr == cursor_attr_t::CURSOR_ON ||
        m_curs_attr == cursor_attr_t::CURSOR_BLINK && cursor_blink_enable
       ) {

        // draw the one actual cursor, wherever it is
        int top  = m_curs_y * m_charcell_h + m_begin_curs;
        int left = m_curs_x * m_charcell_w;

        for(int yy=0; yy<m_rows_curs; yy++) {
            wxAlphaPixelData::Iterator spi(raw_screen);  // screen pointer
            spi.MoveTo(raw_screen, left, top+yy);
            for(int xx=0; xx<m_charcell_w; xx++) {
                spi.Red()   = fg.Red();
                spi.Green() = fg.Green();
                spi.Blue()  = fg.Blue();
                ++spi;
            }
        }
    }

    return true;
}
#endif


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
        dc.SetBrush(wxBrush(bg, wxSOLID));
        dc.SetPen(wxPen(bg, 1, wxSOLID));

        if (top > 0)    // top border is required
            dc.DrawRectangle(0,0, m_scrpix_w,top);
        if (bottom_h > 0)       // bottom border is required
            dc.DrawRectangle(0,bottom, m_scrpix_w,bottom_h);
        if (left > 0)   // left border is required
            dc.DrawRectangle(0,top, left,bottom-top);
        if (right_w)    // right border is required
            dc.DrawRectangle(right,top, right_w,bottom-top);

        dc.SetPen(wxNullPen);
        dc.SetBrush(wxNullBrush);
    }

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

    if (m_charcell_w == 0 || m_charcell_h == 0)
        return;

    int cell_x = (pos.x - m_RCscreen.GetX()) / m_charcell_w;
    int cell_y = (pos.y - m_RCscreen.GetY()) / m_charcell_h;

    if (cell_x < 0 || cell_x > m_chars_w)
        return;
    if (cell_y < 0 || cell_y > m_chars_h)
        return;

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

        if (strncmp(p, "^ERR ", 5) != 0)
            continue;
        char *pp = p + 5;

        string errcode;

        // check for optional initial letter or '='
        if ((*pp >= 'A' && *pp <= 'Z') || (*pp == '='))
            errcode = *pp++;

        // make sure there is at least one digit
        if (!isdigit(*pp))
            continue;

        // grab the number
        while ((pp < e) && isdigit(*pp))
            errcode += *pp++;

    #if 0
        // launch an HTML browser and look up the error code
        string helpfile = "errors.html#Code-" + errcode;
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
Crt::explainError(const string &errcode, const wxPoint &orig)
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
                              (m_scrbits.GetHeight() != height) )
        m_scrbits = wxBitmap(width, height, -1);  // native depth
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

    for(int y=0; y<m_chars_h; y++)
        for(int x=0; x<m_chars_w; x++)
            scr_write_char(x, y, fillval);

    if (m_screen_type == UI_SCREEN_2236DE) {
        uint8 attr = 0;
        // take care of the 25th row
        for(int x=0; x<m_chars_w; x++)
            scr_write_char(x, 24, fillval);
        for(int y=0; y<25; y++)
            for(int x=0; x<80; x++)
                scr_write_attr(x, y, attr);
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

// first level: decompress
void
Crt::processChar(uint8 byte)
{
    if (m_screen_type != UI_SCREEN_2236DE) {
        processChar3(byte);
        return;
    }
    //wxLogMessage("processChar(0x%02x)", byte);

    if (m_raw_cnt > 0) {
        // keep accumulating
        assert(m_raw_cnt < sizeof(m_raw_buf));
        m_raw_buf[m_raw_cnt++] = byte;
    } else if (byte == 0xFB) {
        // start a FB ... sequence
        m_raw_buf[0] = 0xFB;
        m_raw_cnt = 1;
        return;
    } else {
        // pass it through
        processChar2(byte);
        return;
    }

    // check for literal 0xFB byte
    if (m_raw_cnt == 2 && m_raw_buf[1] == 0xD0) {
        processChar2(0xFB);
        m_raw_cnt = 0;
        return;
    }

    // check for delay sequence
    if (m_raw_cnt == 2 && m_raw_buf[1] >= 0xC1 && m_raw_buf[1] <= 0xC9) {
        int delay_ms = 1000 * ((int)m_raw_buf[1] - 0xC0) / 6;
        // FIXME: doing this requires creating a flow control mechanism
        // to block further input, and creating a timer to expire N/6 seconds
        delay_ms = delay_ms;  // defeat linter
        m_raw_cnt = 0;
        return;
    }

    // TODO: what should happen with illegal sequences?
    //       for now, I'm just going to pass them through
    if (m_raw_cnt == 2 && m_raw_buf[1] >= 0x60) {
        processChar2(m_raw_buf[0]);
        processChar2(m_raw_buf[1]);
        m_raw_cnt = 0;
        return;
    }

    // what is left is a run count: FB nn cc
    // where nn is the repetition count, and cc is the character
    if (m_raw_cnt == 3 && m_raw_buf[0] == 0xFB && m_raw_buf[1] < 0x60) {
//UI_Info("Decompress: cnt=%d, chr=0x%02x", m_raw_buf[1], m_raw_buf[2]);
        for(int i=0; i<m_raw_buf[1]; i++)
            processChar2(m_raw_buf[2]);
        m_raw_cnt = 0;
        return;
    }

    // else keep accumulating
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
        if (m_input_buf[4] != 0x0E && m_input_buf[4] != 0x0F)
            return;  // TODO: ignore?
        m_attrs &= ~( char_attr_t::CHAR_ATTR_BRIGHT |
                      char_attr_t::CHAR_ATTR_BLINK  |
                      char_attr_t::CHAR_ATTR_INV    );
        m_attr_under = false;
        if (m_input_buf[2] == 0x02 || m_input_buf[2] == 0x0B)
            m_attrs |= (char_attr_t::CHAR_ATTR_BRIGHT);
        if (m_input_buf[2] == 0x04 || m_input_buf[2] == 0x0B)
            m_attrs |= (char_attr_t::CHAR_ATTR_BLINK);
        if (m_input_buf[3] == 0x02 || m_input_buf[3] == 0x0B)
            m_attrs |= (char_attr_t::CHAR_ATTR_INV);
        if (m_input_buf[3] == 0x04 || m_input_buf[3] == 0x0B)
            m_attr_under = true;
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
        reset();
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
            if (m_beep == nullptr) {
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
            if (0x10 <= byte && byte <= 0x1F)
                byte = byte + 0x40;     // 0x10 aliases into 0x50, etc.
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
        int sample = int(128.0 + amplitude * s);
        *wp++ = sample;
    }

    m_beep = new wxSound;
    bool success = m_beep->Create(total_bytes, wav);
    if (!success) {
        wxDELETE(m_beep);
        m_beep = nullptr;
    }

    delete wav;
}

// vim: ts=8:et:sw=4:smarttab
