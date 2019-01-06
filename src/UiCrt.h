// this class is responsible for drawing the CRT image on the window.
// it is subordinate to CrtFrame.

#ifndef _INCLUDE_UI_CRT_H_
#define _INCLUDE_UI_CRT_H_

#include "w2200.h"
#include "wx/wx.h"

class wxSound;
class CrtFrame;
struct crt_state_t;

class Crt: public wxWindow
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Crt);

    Crt(CrtFrame *parent, crt_state_t *crt_state);

    // ---- setters/getters ----

    enum crtfont_t {
        FONT_MATRIX11 =  1,     // wang dot matrix, scale x*1, y*1
        FONT_MATRIX12 =  2,     // wang dot matrix, scale x*1, y*2
        FONT_MATRIX24 =  3,     // wang dot matrix, scale x*2, y*4
        FONT_NATIVE8  =  8,     // system native font,  8 point
        FONT_NATIVE10 = 10,     // system native font, 10 point
        FONT_NATIVE12 = 12,     // system native font, 12 point
        FONT_NATIVE14 = 14,     // system native font, 14 point
        FONT_NATIVE18 = 18,     // system native font, 18 point
        FONT_NATIVE24 = 24,     // system native font, 24 point
    };
    void setFontSize(const int size);

    void setColor(const wxColor &FG, const wxColor &BG);

    // values range from 0 to 100
    void setDisplayContrast(int n);
    void setDisplayBrightness(int n);
    int  getDisplayContrast()   const noexcept { return m_display_contrast; }
    int  getDisplayBrightness() const noexcept { return m_display_brightness; }

    // tracks whether screen might have changed
    void setDirty(bool dirty = true) noexcept
        { m_dirty = dirty; };
    bool isDirty() const noexcept
        { return m_dirty; };

    // tracks whether cached fontmap is out of date
    void setFontDirty(bool dirty = true);
    bool isFontDirty() const noexcept;

    void setFrameCount(int n)  noexcept { m_frame_count = n; }
    int  getFrameCount() const noexcept { return m_frame_count; }

    // redraw the CRT display as necessary
    void refreshWindow();

    // redraw entire screen, or just the text region
    void invalidateAll()  { Refresh(false); }
    void invalidateText() { Refresh(false, &m_screen_rc); }

    // return a pointer to the screen image
    wxBitmap* grabScreen();

    // create a bell (0x07) sound
    void ding();
    void beepCallback();

private:
    // ---- event handlers ----
    void OnEraseBackground(wxEraseEvent &event);
    void OnPaint(wxPaintEvent &event);
    void OnKeyDown(wxKeyEvent &event);
    void OnChar(wxKeyEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnLeftDClick(wxMouseEvent &event);
    void OnTimer(wxTimerEvent &event);

    // ---- utility functions ----

    wxFont pickFont(int pointsize, bool bold, const std::string &facename="");
    int getFontSize() const noexcept;

    // rebuild m_font_map
    void generateFontmap();

    // recalculate where the active part of the screen is then
    // fill the borders with the background color.
    void recalcBorders();

    // update the bitmap of the screen image
    void generateScreen();
    bool generateScreenByRawBmp(wxColor fg, wxColor bg);
    void generateScreenByBlits(wxMemoryDC &memDC);
    void generateScreenOverlay(wxMemoryDC &memDC);
    void generateScreenCursor(wxMemoryDC &memDC);

    // map an intensity to a display color
    wxColor intensityToColor(float f) const;

    void explainError(const std::string &errcode,
                      const wxPoint &orig); // pop up error description

    // ---- state ----

    CrtFrame     * const m_parent;      // who owns us
    crt_state_t  * const m_crt_state;   // contents of display memory

    wxBitmap  m_scrbits;            // image of the display
    int       m_frame_count = 0;    // for tracking refresh fps
    bool      m_dirty       = true; // need to refresh display

    wxBitmap  m_font_map;           // image of the font in use
    int       m_font_size   = FONT_MATRIX12;  // size of font (in points)
    bool      m_font_dirty  = true; // font/color/contrast/brightness changed
    int       m_charcell_w  = 1;    // width of one character cell
    int       m_charcell_h  = 1;    // height of one character cell
    int       m_charcell_sx = 1;    // real x pixels per logical pixel
    int       m_charcell_sy = 1;    // # real y pixels per logical pixel
    int       m_charcell_dy = 1;    // row skipping factor

    wxColor   m_fg_color = wxColor(0xff, 0xff, 0xff);  // phosphor color
    wxColor   m_bg_color = wxColor(0x00, 0x00, 0x00);  // background color
    int       m_display_contrast   = 100;
    int       m_display_brightness = 0;

    // this holds the dimensions of the visible area we have to draw on,
    // which is entirely independent of the logical CRT dimensions.
    int       m_screen_pix_w = 0;   // display dimension, in pixels
    int       m_screen_pix_h = 0;   // display dimension, in pixels
    wxRect    m_screen_rc = wxRect(0, 0, 0, 0);  // active text area

    // sound for beep
    void createBeep();
    std::unique_ptr<wxSound> m_beep;
    std::unique_ptr<wxTimer> m_beep_tmr;
};

#endif // _INCLUDE_UI_CRT_H_

// vim: ts=8:et:sw=4:smarttab
