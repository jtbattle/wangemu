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

    const float contrast   = getDisplayContrast()   * 0.01f * 1.3f;
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

wxFont Crt::pickFont(int pointsize, int bold, const string &facename)
{
    wxFont font;
    auto fontweight = (bold) ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL;
    bool underline = false;

    if (facename != "") {
        // try the specified family name
        font = wxFont(pointsize,
                      wxFONTFAMILY_MODERN,  // fixed pitch
                      wxFONTSTYLE_NORMAL,
                      fontweight,
                      underline,
                      facename
                );
    }

    // pick whatever the default fixed size font is
    if (!font.IsOk()) {
        font = wxFont(pointsize, wxFONTFAMILY_MODERN,
                                 wxFONTSTYLE_NORMAL,
                                 fontweight);
        string name = font.GetFaceName();
        name = "";
    }

    return font;
}

void
Crt::generateFontmap()
{
    wxClientDC dc(this);
    wxMemoryDC charDC;
    wxFont font;  // for prerendering native font
    int sx,       // bitmap replication factor in x
        sy,       // bitmap replication factor in y
        dy;       // step in y (allows skipping rows)
    int filter;   // which filter kernel to use

    const int fontsize = getFontSize();

    switch (fontsize) {
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
                font = pickFont(fontsize, /*bold*/false, "Courier New");
                assert(font != wxNullFont);
                charDC.SetFont(font);
                m_charcell_w = charDC.GetCharWidth();
                m_charcell_h = charDC.GetCharHeight()
                             + 3;  // make room for underline, cursor, blank
                sx = sy = dy = 1; // keep lint happy

                filter = 0;  // no filter, although filtering does look cool too
                break;
    }

    // this stuff is needed when drawing in other routines, eg generateScreenCursor()
    m_charcell_sx = sx;
    m_charcell_sy = sy;
    m_charcell_dy = dy;

    // pick a filter kernel for blurring
    // there is no real science here, just ad-hoc tweaking
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

//filter = 0; // debugging, makes it easier to see pixel pattern
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
#if !(__WXMAC__) && DRAW_WITH_RAWBMP
    m_fontmap = wxBitmap( 256*m_charcell_w, 8*m_charcell_h, 24);   // use DIB
#else
    m_fontmap = wxBitmap( 256*m_charcell_w, 8*m_charcell_h, wxBITMAP_SCREEN_DEPTH);
#endif
    wxMemoryDC fdc(m_fontmap);  // create a dc for us to write to

    // allocate a temp bitmap for working on one character.
    // it has a one pixel border all around so we can do 3x3 convolution
    // easily and not worry about the edge cases.
    // char_bitmap is used to receive a real font character.
    const int offset = 1;
    const int img_w = m_charcell_w + 2*offset;
    const int img_h = m_charcell_h + 2*offset;
    wxBitmap char_bitmap(img_w, img_h, 32);
    vector<vector<float>> char_intensity(img_h, vector<float>(img_w, 0.0f));

    charDC.SelectObject(char_bitmap);
    charDC.SetBackgroundMode(wxSOLID);

    wxColor blk(*wxBLACK), norm(*wxWHITE), intense(*wxWHITE);
    float f_blk(0.0f),   f_norm(1.0f),   f_intense(1.0f);
    if (m_screen_type == UI_SCREEN_2236DE) {
        // diminish normal to differentiate it from bright intensity
        norm = wxColor(0x70,0x70,0x70);  // only blue channel is used
        f_norm = 160.0f/255.0f;
    }

    // mapping from filtered image intensity to a color
    // FIMXE: gamma compensation?
    wxColor colormap[256];
    for(int n=0; n<256; ++n) {
        float w = n * (1.0f/256.0f);
        wxColor c = intensityToColor(w);
        colormap[n].Set(c.Red(), c.Green(), c.Blue());
    }

    // boundaries for drawing graphics chars
    int boxx[3] = { 0, m_charcell_w / 2, m_charcell_w };
    int boxy[4] = { 0, m_charcell_h / 3, 2*m_charcell_h/3, m_charcell_h };

    // build a glyph map of the entire character set
    for(int bright=0; bright<2; ++bright) {
    for(int inv=0; inv<2; ++inv) {

        // using bold font helps make black on white readability
        if (fontsize >= FONT_NATIVE8) {
// on windows, at least, the bold font is shifted slightly relative to
// normal weight, so blink causes the characters to shift up/down. too bad.
//          font = pickFont(fontsize, /*bold*/(inv==1) || (bright==1) );
            font = pickFont(fontsize, /*bold*/(inv==1), "Courier New");
            charDC.SetFont(font);
        }

        for(int alt=0; alt<2; ++alt) {

            // brightness modulation for native font rendering
            wxColor fg_eff = ( inv) ? blk : (bright) ? intense : norm;
            wxColor bg_eff = (!inv) ? blk : (bright) ? intense : norm;
            charDC.SetBackground( wxBrush(bg_eff, wxBRUSHSTYLE_SOLID) );
            charDC.SetTextBackground(bg_eff);
            charDC.SetTextForeground(fg_eff);
            charDC.SetPen( wxPen(fg_eff, 1, wxPENSTYLE_SOLID) );
            charDC.SetBrush( wxBrush(fg_eff, wxBRUSHSTYLE_SOLID) );

            for(int chr=0; chr<256; ++chr) {
                int ch = (chr & 0x7F);  // minus underline flag

                if (fontsize >= FONT_NATIVE8) {

                    // prepare by blanking out everything
                    charDC.Clear();

                    if (alt && (ch >= 0x40)) {
                        // box graphics characters
                        for(int yy=0; yy<3; ++yy) {
                            for(int xx=0; xx<2; ++xx) {
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
#if 0
                        wxString text((wxChar)(xlat_char_alt[ch]));
#else
                        wxString text(unicode_xlat_char_alt[ch]);
#endif
                        charDC.DrawText(text, offset, offset);
                    } else {
#if 0
                        wxString text((wxChar)(xlat_char[ch]));
#else
                        wxString text(unicode_xlat_char[ch]);
#endif
                        charDC.DrawText(text, offset, offset);
                    }

                    // convert to float intensity
                    wxImage char_image(char_bitmap.ConvertToImage());
                    for(int rr=0; rr < m_charcell_h; ++rr) {
                        for(int cc=0; cc < m_charcell_w; ++cc) {
                            char_intensity[rr+offset][cc+offset] =
                                char_image.GetBlue(cc+offset, rr+offset) / 255.0f;
                        }
                    }

                    // at this point, chr >=0x80 always means underline.
                    // underline style doesn't work for all platforms,
                    // so do it manually.
                    if (chr >= 0x80) {
                        float dot_bg = blk.Blue() / 255.0f;
                        float dot_fg = norm.Blue() / 255.0f;
                        int thickness = (fontsize > FONT_NATIVE10) ? 2 : 1;
                        for(int yy=0; yy<thickness; ++yy) {
                            int row = offset + (m_charcell_h-sy) + yy - thickness+1;
                            for(int x=0; x<m_charcell_w; ++x) {
#if 0
                                // normal mode:
                                //    alternate pixels, skipping the first pair
                                // inv mode:
                                //    light just the first pair
                                bool lit = (inv) ? (x < 2)
                                                 : (x > 1 && ((x&1)==1));
#else
                                bool lit = (inv) ? (x < 2) : (x > 1);
#endif
                                float v = (lit) ? dot_fg : dot_bg;
                                char_intensity[row][x] = v;
                            }
                        }
                    }

                } else {  // use real bitmap font

                    for(int bmr=0; bmr<11; ++bmr) {  // bitmap row

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

                        // inv is a bit strange, but this is what the hardware does.
                        // dot = (inv & !dot_last_cycle) ? 0
                        //     : (inv)                   ? !(glyph_dot | box_dot)
                        //                               :  (glyph_dot | box_dot);
                        //
                        // I can't model this accurately right now because the box
                        // graphics overlay is done separately
                        if (inv) {
                            pixrow = (~pixrow >> 1)  // 1st term above
                                   &  ~pixrow;       // 2nd term above
                        }

                        // add underline on the last bitmap row
                        // the hardware stipples the underline this way
                        float dot_fg = (bright) ? f_intense : f_norm;
                        if ((chr >= 0x80) && (bmr == 10)) {
                            pixrow = 0x55 << 1;
                            dot_fg = f_norm;   // underline is not affected by bright
                        }

                        for(int bmc=0; bmc<10; pixrow <<= 1, ++bmc) { // bitmap col
                            float v = (pixrow & 0x200) ? dot_fg : f_blk;
                            for(int yy=0; yy<sy; ++yy) {
                            for(int xx=0; xx<sx; ++xx) {
                                char_intensity[offset + bmr*sy*dy + yy]
                                              [offset + bmc*sx    + xx] = v;
                            } } // for xx,yy
                        } // for bmc

                    } // for bmr

                } // use Wang bitmap

                wxImage blur_img(m_charcell_w, m_charcell_h);

                // we run a 3x3 convolution kernel on each character of the
                // font map to simulate the limited bandwidth of the real CRT
                for(int y=offset; y<m_charcell_h+offset; ++y) {
                for(int x=offset; x<m_charcell_w+offset; ++x) {

                    float fv = filter_w[0]*char_intensity[y-1][x-1]
                             + filter_w[1]*char_intensity[y-1][x+0]
                             + filter_w[2]*char_intensity[y-1][x+1]
                             + filter_w[3]*char_intensity[y+0][x-1]
                             + filter_w[4]*char_intensity[y+0][x+0]
                             + filter_w[5]*char_intensity[y+0][x+1]
                             + filter_w[6]*char_intensity[y+1][x-1]
                             + filter_w[7]*char_intensity[y+1][x+0]
                             + filter_w[8]*char_intensity[y+1][x+1];

                    int idx = int(255.0f*fv + 0.5f);
                    idx = (idx < 0x00) ? 0x00
                        : (idx > 0xFF) ? 0xFF
                                       : idx;

                    wxColor rgb = colormap[idx];
                    blur_img.SetRGB(x-offset, y-offset,
                                    rgb.Red(), rgb.Green(), rgb.Blue());

                } } // for x,y

                // copy it to the final font bitmap
                int row_offset = m_charcell_h * (4*alt + 2*inv + bright);
                fdc.DrawBitmap( wxBitmap(blur_img),    // source image
                                chr*m_charcell_w, row_offset ); // dest x,y

            } // for chr

        } // for alt

    } } // inv, bright

    fdc.SelectObject(wxNullBitmap);  // release m_fontmap
    charDC.SelectObject(wxNullBitmap);  // release char_bitmap

    setFontDirty(false);
}


// update the bitmap of the screen image
void
Crt::generateScreen()
{
    static bool reentrant = false;
//  assert(!reentrant);
    reentrant = true;

    if (isFontDirty()) {
        generateFontmap();
        recalcBorders();  // the bitmap store might have changed size
    }

    wxColor fg(intensityToColor(1.0f)),  // color of text
            bg(intensityToColor(0.0f));  // color of background

    bool success = false;
#if DRAW_WITH_RAWBMP
// FIXME: see if we still need this for OSX
    success = generateScreenByRawBmp(fg, bg);
#endif
    wxMemoryDC memDC(m_scrbits);
    if (!success) {
        memDC.SetBackground( wxBrush(bg, wxBRUSHSTYLE_SOLID) );
        memDC.Clear();
        generateScreenByBlits(memDC);
    }

    if (m_screen_type == UI_SCREEN_2236DE) {
        generateScreenOverlay(memDC);
    }

    generateScreenCursor(memDC);

    // release the bitmap
    memDC.SelectObject(wxNullBitmap);
    reentrant = false;
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
    for(int row=0; row<m_chars_h2; ++row) {

        if (m_screen_type == UI_SCREEN_2236DE) {

            for(int col=0; col<m_chars_w; ++col) {
                uint8 chr   = m_display[row*m_chars_w + col];
                uint8 attr  =    m_attr[row*m_chars_w + col];
                bool blink  = (attr & char_attr_t::CHAR_ATTR_BLINK)  ? true : false;
                int  alt    = (attr & char_attr_t::CHAR_ATTR_ALT)    ? 4 : 0;
                int  inv    = (attr & char_attr_t::CHAR_ATTR_INV)    ? 2 : 0;
                int  bright = (attr & char_attr_t::CHAR_ATTR_BRIGHT) ? 1 : 0;

                // blinking alternates between normal and bright intensity
                // but intense text can't blink because it is already intense
                if (text_blink_enable && blink) {
                    bright = 1;
                }

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
            for(int col=0; col<m_chars_w; ++col) {
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
    bool cursor_blink_enable = m_parent->getCursorBlinkPhase();
    if (m_curs_attr == cursor_attr_t::CURSOR_OFF  ||
        m_curs_attr == cursor_attr_t::CURSOR_BLINK && !cursor_blink_enable) {
        // don't draw the cursor at all
        return;
    }

    wxColor fg(intensityToColor(1.0f));
    wxColor bg(intensityToColor(0.0f));
    wxColor color(fg);
    if (m_screen_type == UI_SCREEN_2236DE) {
        uint8 attr = m_attr[80*m_curs_y + m_curs_x];
        color = (attr & char_attr_t::CHAR_ATTR_INV) ? bg : fg;
    }

    int top   = m_charcell_h*(m_curs_y+1) - (2 * m_charcell_sy*m_charcell_dy);
    int left  = m_charcell_w* m_curs_x;
    int right = left + m_charcell_w - 1;
    memDC.SetPen( wxPen(color, 1, wxPENSTYLE_SOLID) );
    for(int y=0; y < 2; ++y) {
        for(int yy=0; yy<m_charcell_sy; ++yy) {
            int yyy = top + y*m_charcell_dy*m_charcell_sy + yy;
            memDC.DrawLine(left,  yyy,
                           right, yyy);
        }
    }
}


// draw box/line overlay onto DC
void
Crt::generateScreenOverlay(wxMemoryDC &memDC)
{
    assert(m_screen_type == UI_SCREEN_2236DE);

    // box overlay is always normal brightness.
    // in 2236 mode, we diminish normal brightness in order to get bright (1.0)
    wxColor fg = (m_screen_type == UI_SCREEN_2236DE) ? intensityToColor(0.6f)
                                                     : intensityToColor(1.0f);
    wxPen pen(fg, 1, wxPENSTYLE_USER_DASH);
    wxDash dashpat[2];
    if (m_charcell_sx == 1) {
        // rather than 1 on, 1 off, this looks like a solid line
        // but I can't find a work around to make it look right
        dashpat[0] = 1; dashpat[1] = 1;
    } else {
        assert(m_charcell_sx == 2);
    //  dashpat[0] = 2; dashpat[1] = 2; // 3 on, 1 off for some reason
        dashpat[0] = 1; dashpat[1] = 3; // 2 on, 2 off (empirically, win7)
    }
    pen.SetDashes(2, dashpat);
    memDC.SetPen(pen);

    // find horizontal runs of lines and draw them
    for(int row=0; row < 25; ++row) {
        int off = 80 * row;
        int top = row * m_charcell_h;
        int start = -1;
        for(int col=0; col < 80; ++col, ++off) {
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_LEFT)) {
                // start or extend
                if (start < 0) {
                    start = col*m_charcell_w;
                }
            } else if (start >= 0) {
                // hit the end
                int rgt = col * m_charcell_w;
                for(int yy=0; yy<m_charcell_sy; ++yy) {
                    memDC.DrawLine(start, top+yy, rgt, top+yy);
                }
                start = -1;
            }
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_RIGHT)) {
                if (start < 0)  // start of run
                    start = col*m_charcell_w + (m_charcell_w >> 1);
            } else if (start >= 0) {
                // end of run
                int rgt = col * m_charcell_w + (m_charcell_w >> 1);
                for(int yy=0; yy<m_charcell_sy; ++yy) {
                    memDC.DrawLine(start, top+yy, rgt, top+yy);
                }
                start = -1;
            }
        }
        // draw if we made it all the way to the right side
        if (start >= 0) {
            int rgt = 80 * m_charcell_w;
            for(int yy=0; yy<m_charcell_sy; ++yy) {
                memDC.DrawLine(start, top+yy, rgt, top+yy);
            }
        }
    }

    // find vertical runs of lines and draw them
    // the 25th line is guaranteed to not have the vert attribute
    for(int col=0; col < 80; ++col) {
        int off = col;
        int mid = col * m_charcell_w + (m_charcell_w >> 1);
        int start = -1;
        for(int row=0; row < 25; ++row, off += 80) {
            if (m_attr[off] & (char_attr_t::CHAR_ATTR_VERT)) {
                if (start < 0)  // start of run
                    start = row * m_charcell_h;
            } else if (start >= 0) {
                // end of run
                int end = row * m_charcell_h;
                for(int xx=0; xx<m_charcell_sx; ++xx) {
                    memDC.DrawLine(mid+xx, start, mid+xx, end+(m_charcell_sy>>1));
                }
                start = -1;
            }
        }
    }
}


#if DRAW_WITH_RAWBMP
// update the bitmap of the screen image, using rawbmp interface
// returns false if it fails.
bool
Crt::generateScreenByRawBmp(wxColor fg, wxColor bg)
{
// this is very hacky, and for windows it works only if the m_scrbits and
// m_fontmap bitmaps are declared with depth 24, instead of 32 or -1.
// enabling it for windows is mostly useful for debugging
#if __WXMAC__
  #define TT_t wxAlphaPixelData
  #define TW 32
#else
  #define TT_t wxNativePixelData
  #define TW 24
#endif

    // this path is faster only if we are drawing to a 32b surface.
    // this is because the code must commit to using either
    // wxAlphaPixelData (32b) or wxNativePixelData (24b except under OSX).
    if (m_scrbits.GetDepth() != TW) {
        return false;
    }

    TT_t raw_screen(m_scrbits);
    if (!raw_screen) {
        return false;
    }

    TT_t raw_font(m_fontmap);
    if (!raw_font) {
        return false;
    }

    bool text_blink_enable = m_parent->getTextBlinkPhase();

    // draw the characters (diddlescan order)
    TT_t::Iterator sp(raw_screen);  // screen pointer
    for(int row=0; row<m_chars_h2; ++row) {

        // the upper left corner of the leftmost char of row
        TT_t::Iterator rowUL = sp;

        for(int col=0; col<m_chars_w; ++col) {

            // the upper left corner of the char on the screen
            TT_t::Iterator charUL = sp;

            int ch   = m_display[row*m_chars_w + col];
            int attr =    m_attr[row*m_chars_w + col];

            // pick out subimage of current character from the
            // fontmap and copy it to the screen image
            TT_t::Iterator cp(raw_font);
            cp.OffsetX(raw_font, m_charcell_w * ch);

            bool blink  = (attr & char_attr_t::CHAR_ATTR_BLINK)  ? true : false;
            int  alt    = (attr & char_attr_t::CHAR_ATTR_ALT)    ? 4 : 0;
            int  inv    = (attr & char_attr_t::CHAR_ATTR_INV)    ? 2 : 0;
            int  bright = (attr & char_attr_t::CHAR_ATTR_BRIGHT) ? 1 : 0;

            // blinking alternates between normal and bright intensity
            // but intense text can't blink because it is already intense
            if (text_blink_enable && blink) {
                bright = 1;
            }

            int font_row = m_charcell_h * (alt + inv + bright);
            cp.OffsetY(raw_font, font_row);

            for(int rr=0; rr<m_charcell_h; ++rr) {
                // pointers to the start of the current character scanline
                TT_t::Iterator sRowp = sp;
                TT_t::Iterator cRowp = cp;
                for(int cc=0; cc<m_charcell_w; ++cc) {
#if __WXMAC__
                    // fails for 24bpp; but 32b was asserted earlier
                    sp.Data() = cp.Data();
#else
                    sp.Red()   = cp.Red();
                    sp.Green() = cp.Green();
                    sp.Blue()  = cp.Blue();
#endif
                    ++sp;
                    ++cp;
                }
                sp = sRowp;  sp.OffsetY(raw_screen, 1);
                cp = cRowp;  cp.OffsetY(raw_font, 1);
            } // for (rr)

            // advance to the next character of row
            sp = charUL;
            sp.OffsetX(raw_screen, m_charcell_w);

        } // for(col)

        // advance to the next row of characters
        sp = rowUL;
        sp.OffsetY(raw_screen, m_charcell_h);

    } // for(row)

    return true;
}
#endif

// vim: ts=8:et:sw=4:smarttab
