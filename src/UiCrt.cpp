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
// is the rawbmp interface must convert from DDB to DIB and back, which is
// slow.  #1 and #3 are about the same speed, but #3 using the dot matrix
// font wins on the basis of nostalgia.

// TODO:
//    *) in dot matrix mode, the cursor isn't blurred.

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


Crt::Crt(CrtFrame *parent, int screen_size) :
    wxWindow(parent, -1, wxDefaultPosition, wxDefaultSize),
    m_parent(parent),
    m_chars_w((screen_size == UI_SCREEN_80x24) ? 80 : 64),
    m_chars_h((screen_size == UI_SCREEN_80x24) ? 24 : 16),
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
    m_curs_x(0),
    m_curs_y(0),
    m_curs_prevx(0),
    m_curs_prevy(0),
    m_curs_on(true),            // enable cursor on reset
    m_frame_count(0),
    m_dirty(true)               // must be regenerated
{
    scr_clear();
}


// free resources on destruction
Crt::~Crt()
{
    // nothing
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
//   u = underline
//   c = cursor
//      |. . . . . . . . . .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |. x x x x x x x x .|
//      |u u u u u u u u u .|
//      |c c c c c c c c c c|
//
// the "cccccccccc" row is drawn on top of the stored character cell, so
// it appears as black pixels in the fontmap.  it is important that the
// active region have this one pixel inactive region so that we can apply
// a 3x3 blurring filter to simulate what the real CRT does -- without it
// the resulting display is painful to read.  the "uuuuuuuuuu" row does
// creep into this margin, but since there is a gap, not filtering it
// exactly right makes adjacent underlined characters have a subtly
// different gap that is hardly noticeable.

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
                m_font = wxFont(getFontSize(), wxMODERN, wxNORMAL, wxNORMAL);
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
            default: assert(0);
            case 0: filter_w = w_noop;           break;
            case 1: filter_w = w_semi_gaussian;  break;
            case 2: filter_w = w_gaussian;       break;
            case 3: filter_w = w_gaussian_tweak; break;
            case 4: filter_w = w_1D;             break;
            case 5: filter_w = w_contrast;       break;
        }
    }

    // this is a mapping of the filtered image to the final color
    wxColor colormap[256];
    {
        for(int n=0; n<256; n++) {
            float w = n * (1.0/256.0);
            wxColor c = intensityToColor(w);
            colormap[n].Set(c.Red(), c.Green(), c.Blue());
        }
    }

    // reallocate the bitmap which holds all the glyphs
    // build up a 256 character image map of the font
    m_fontmap = wxBitmap( 256*m_charcell_w, m_charcell_h, -1);
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
    wxMemoryDC charDC;
    charDC.SelectObject(char_bitmap);

    // get ready for setting pixels for bitmap
    wxColor fg(*wxWHITE), bg(*wxBLACK);
    charDC.SetBackground( wxBrush(bg, wxSOLID) );
    charDC.SetPen(wxPen(fg, 1, wxSOLID));

    // set text modes in case we are using FORCE_FONTMAP
    charDC.SetBackgroundMode(wxSOLID);
    charDC.SetTextForeground(fg);
    charDC.SetTextBackground(bg);
    if (m_font != wxNullFont)
        charDC.SetFont(m_font);

    charDC.SelectObject(wxNullBitmap);

    // build a 256 wide, 1 high character map
    for(int chr=0; chr<256; chr++) {

        int cc = (chr < 0x10) ? 0x20    // controls map to space
               : (chr & 0x7F);          // wraps around

        if (getFontSize() >= FONT_NATIVE8) {

            // prepare by blanking out everything
            charDC.SelectObject(char_bitmap);
            charDC.Clear();

            wxString text((wxChar)(xlat_char[cc]));
            // underline style doesn't work for all platforms,
            // so superimpose the underline later
            charDC.DrawText(text, offset, offset);

            if (chr >= 0x90) {
                // draw underline, keeping scaling into account
                for(int yy=0; yy<sy; yy++) {
                    int lft = offset;
                    int rgt = offset + m_charcell_w - 1;
                    int top = offset + m_begin_ul + yy;
                    charDC.DrawLine(lft, top, rgt, top);
                }
            }

            charDC.SelectObject(wxNullBitmap);  // release char_bitmap
            char_image = char_bitmap.ConvertToImage();

        } else {  // use real bitmap font

            // blank it
            char_image.SetRGB( wxRect(0,0,img_w,img_h), 0x00, 0x00, 0x00 );

            // use authentic Wang bitmap
            for(int bmr=0; bmr<8; bmr++) {  // bitmap row
                uint8 pixrow = chargen[8*cc+bmr];
                for(int bmc=0; bmc<8; pixrow <<= 1, bmc++) {  // bitmap col
                    if (pixrow & 0x80) {
                        // the pixel should be lit; replicate it
                        for(int yy=0; yy<sy; yy++)
                        for(int xx=0; xx<sx; xx++) {
                            char_image.SetRGB(offset + (bmc+1)*sx    + xx,
                                              offset + (bmr+1)*sy*dy + yy,
                                              0xff, 0xff, 0xff);
                        } // for xx,yy
                    } // pixel is lit
                } // for bmc
            } // for bmr

            if (chr >= 0x90) {
                // draw underline, keeping scaling into account
                for(int yy=0; yy<sy; yy++) {
                    int lft = offset;
                    int rgt = offset + m_charcell_w - 1;
                    int top = offset + m_begin_ul + yy;
                    for(int xx=lft; xx<rgt; xx++)
                        char_image.SetRGB(xx, top, 0xff, 0xff, 0xff);
                }
            }

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
        fdc.DrawBitmap( wxBitmap(blur_img),    // source image
                        chr*m_charcell_w, 0 ); // dest x,y

    } // for (chr)

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
    const bool use_fontmap = (getFontSize() < FONT_NATIVE8) ||
                             FORCE_FONTMAP ;

    wxColor fg(intensityToColor(1.0f)),  // color of text
            bg(intensityToColor(0.0f));  // color of background

    if (isFontDirty()) {
        generateFontmap();
        recalcBorders();  // the bitmap store might have changed size
    }

#if DRAW_WITH_RAWBMP
    if (use_fontmap) {
        // one drawing scheme
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

    if (use_fontmap)
        generateScreenByBlits(memDC);
    else
        generateScreenByText(memDC, fg, bg);

    // draw cursor -- there can be only zero/one on screen.
    // it is drawn one scanline lower than the underline
    if (m_curs_on) {
        // draw the one actual cursor, wherever it is
        int top   = m_curs_y * m_charcell_h + m_begin_curs;
        int left  = m_curs_x * m_charcell_w;
        int right = left + m_charcell_w - 1;
        memDC.SetPen( wxPen(fg, 1, wxSOLID) );
        for(int yy=0; yy<m_rows_curs; yy++)
            memDC.DrawLine(left,  top + yy,
                           right, top + yy);
    }

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

    // draw each row of the text
    for(int row=0; row<m_chars_h; row++) {
        for(int col=0; col<m_chars_w; col++) {
            int ch = m_display[row*m_chars_w + col];
            if ((ch >= 0x10) && (ch != 0x20)) {  // if (non-blank character)
                memDC.Blit(col*m_charcell_w, row*m_charcell_h,  // dest x,y
                           m_charcell_w, m_charcell_h,          // w,h
                           &fontmapDC,                          // src image
                           ch*m_charcell_w, 0);                 // src x,y
            }
        }
    }

    fontmapDC.SelectObject(wxNullBitmap);
}


// use DrawText (fast, but can use only native font)
void
Crt::generateScreenByText(wxMemoryDC &memDC, wxColor fg, wxColor bg)
{
    // draw each row a a single string using the native system font
    assert(m_chars_w < 81);
    char linebuff[81];
    linebuff[m_chars_w] = '\0';     // put in string terminator

    assert(m_font != wxNullFont);
    memDC.SetFont(m_font);
    memDC.SetBackgroundMode(wxSOLID);
    memDC.SetTextBackground(bg);
    memDC.SetTextForeground(fg);

    for(int row=0; row<m_chars_h; row++) {

        int y = row*m_charcell_h + 1;
        // +1 because of placement of active area in cell layout

        // we promise that there never is a null byte in m_display.
        // when bytes get stuffed into m_display they are converted
        // to spaces then.
        int col;
        bool underlines = false;
        for(col=0; col<m_chars_w; col++) {
            int chr = m_display[row*m_chars_w + col] & 0xFF;        // ignore underline bit
            linebuff[col] = (chr < 0x10) ? 0x20     // controls map to space
                          : xlat_char[chr&0x7F];
            underlines |= (chr >= 0x90);
        }

        wxString text = linebuff;
        memDC.DrawText(text, 0,y);

        if (underlines) {
            // go back and underline all appropriate characters
            int left;
            for(col=0, left=0; col<m_chars_w; col++, left += m_charcell_w) {
                int chr = m_display[row*m_chars_w + col];
                if (chr >= 0x90) {
                    int right = left + m_charcell_w - 1;
                    memDC.DrawLine(left,  y+m_begin_ul,
                                   right, y+m_begin_ul);
                }
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
    for(int row=0; row<m_chars_h; ++row) {

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
    if (m_curs_on) {

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
    if (0) {
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
    int height = m_charcell_h*m_chars_h;
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
        m_curs_x = 0;   // yes, scrolling has this effect
                        // (maybe an artifact of the microcode?  probably not)
        scr_scroll();
    } else if (m_curs_y < 0)
        m_curs_y = m_chars_h-1;     // wrap around
}

// move cursor left or right
void
Crt::adjustCursorX(int delta)
{
    m_curs_x += delta;
    if (m_curs_x >= m_chars_w)
        m_curs_x = 0;
    else if (m_curs_x < 0)
        m_curs_x = m_chars_w - 1;
}


// move the cursor to the named location
void
Crt::updateCursor()
{
    m_curs_prevx = m_curs_x;
    m_curs_prevy = m_curs_y;
}

// clear the display; home the cursor
void
Crt::scr_clear()
{
    char fillval = ' ';

    for(int y=0; y<m_chars_h; y++)
        for(int x=0; x<m_chars_w; x++)
            scr_write_char(x, y, fillval);

    setCursorX(0);
    setCursorY(0);
}


// scroll the contents of the screen up one row, and fill the new
// row with blanks.
void
Crt::scr_scroll()
{
    unsigned char *d  = m_display;                      // first char of row 0
    unsigned char *s  = d + m_chars_w;                  // first char of row 1
    unsigned char *d2 = d + m_chars_w*(m_chars_h-1);    // first char of last row

    // scroll up the data
    (void)memcpy(d, s, (m_chars_h-1)*m_chars_w);

    // blank the last line
    (void)memset(d2, ' ', m_chars_w);
}


// emit a character to the display
void
Crt::emitChar(uint8 byte)
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
            updateCursor();
            break;

        case 0x03:      // clear screen
            scr_clear();
            updateCursor();
            break;

        case 0x05:      // enable cursor
            m_curs_on = true;
            break;

        case 0x06:      // disable cursor
            m_curs_on = false;
            break;

        case 0x07:      // bell
            wxBell();
            break;

        case 0x08:      // cursor left
            adjustCursorX(-1);
            updateCursor();
            break;

        case 0x09:      // horizontal tab
            adjustCursorX(+1);
            updateCursor();
            break;

        case 0x0A:      // linefeed (go down one column)
            adjustCursorY(+1);
            updateCursor();
            break;

        case 0x0C:      // reverse index (cursor up)
            adjustCursorY(-1);
            updateCursor();
            break;

        case 0x0D:      // go to first column
            setCursorX(0);
            updateCursor();
            break;

        default:        // just a character
#if 0
            // this is true for the old 64x16 display generator, not 80x24
            if (0x10 <= byte && byte <= 0x1F)
                byte = byte + 0x40;     // 0x10 aliases into 0x50, etc.
#endif
            if (byte >= 0x10) {  // skip control characters
                scr_write_char(m_curs_x, m_curs_y, byte);
                adjustCursorX(+1);
                updateCursor();
            }
            break;
    }
    setDirty();
}
