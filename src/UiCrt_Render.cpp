// This file implements the part of the Crt class related to drawing
// the pixels of the Crt given the m_display[] and m_attr[] state.
//
// To eliminate flashing, text is drawn to a pre-allocated bitmap,
// m_scrbits.  Once the full screen image has been constructed,
// it gets squirted out to the screen in a single operation.
//
// Any time the user changes a display setting (font, color, brightness,
// contrast) generateFontmap() is called.  This function uses either the
// chosen native font or it consults the Wang character set bitmap in
// UiCrt_Charset.cpp to render each character set into the m_fontmap image.
// The image contains 8 rows of 256 characters per row; the first 128
// characters are the non-underlined version, and the last 128 are the
// same characters but underlined.  There are 8 rows of characters, one
// for each combination of
//     { normal, alt charset } x { normal, reverse } x { normal, bright }
// using the current fg/bg color, brightness, and intensity.
//
// When it comes time to redraw the screen, there are two ways it can be
// done.  Either way draws the screen to the preexisting m_scrbits.
//
//   1) [ generateScreenByBlits() ]
//      Nested for() loops sweep through the 64x16 or 80x24 screen array
//      and uses a blit to copy the appropriate part of m_fontmap to a
//      given screen location in m_scrbits.
//
//   2) [ generateScreenByRawBmp() ]
//      Nested for() loops sweep through the 64x16 or 80x24 screen array
//      and uses a nest inner pair of for() loops to manually copy the
//      appropriate part m_fontmap to m_scrbits one pixel at a time.
//
//      One would think this is slower than case #1, but at least as of
//      wxWidgets 2.9.5 on OSX, each character blit required an expensive
//      format conversion of a wxImage array.
//
//   3) in the future, it would be interesting to use a wxGlContext to
//      render the image map via a shader, using the m_display[] and
//      m_attr[] arrays as an input texture to the shader.

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

            } // if (drawable character)

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

// vim: ts=8:et:sw=4:smarttab
