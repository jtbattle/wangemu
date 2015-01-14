// this class is responsible for drawing the CRT image on the window.
// it is subordinate to CrtFrame.

#ifndef _INCLUDE_UI_CRT_H_
#define _INCLUDE_UI_CRT_H_

class CrtFrame;

class Crt: public wxWindow
{
public:
    Crt( CrtFrame *parent, int screen_size);    // constructor
    ~Crt();                                     // destructor

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

    void setColor(wxColor FG, wxColor BG);

    // values range from 0 to 100
    void setDisplayContrast(int n);
    void setDisplayBrightness(int n);
    int  getDisplayContrast()   { return m_display_contrast; }
    int  getDisplayBrightness() { return m_display_brightness; }

    // tracks whether screen might have changed
    void setDirty(bool dirty = true)
        { m_dirty = dirty; };
    bool isDirty()
        { return m_dirty; };

    // tracks whether cached fontmap is out of date
    void setFontDirty(bool dirty = true);
    bool isFontDirty() const;

    void setFrameCount(int n) { m_frame_count = n; }
    int  getFrameCount()      { return m_frame_count; }

    // redraw the CRT display as necessary
    void refreshWindow(void);

    // redraw entire screen, or just the text region
    void invalidateAll()  { Refresh(false); }
    void invalidateText() { Refresh(false, &m_RCscreen); }

    // return a pointer to the screen image
    wxBitmap* grabScreen();

    // emit a character to the display
    void emitChar(uint8 byte);

private:
    // ---- event handlers ----
    void OnEraseBackground(wxEraseEvent& event);
    void OnPaint(wxPaintEvent &event);
    void OnKeyDown(wxKeyEvent& event);
    void OnChar(wxKeyEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnLeftDClick(wxMouseEvent& event);

    // ---- utility functions ----

    int getFontSize() const;

    // rebuild m_fontmap
    void generateFontmap();

    // recalculate where the active part of the screen is then
    // fill the borders with the background color.
    void recalcBorders();

    // update the bitmap of the screen image
    void generateScreen();
    bool generateScreenByRawBmp(wxColor fg, wxColor bg);
    void generateScreenByBlits(wxMemoryDC &memDC);
    void generateScreenByText(wxMemoryDC &memDC, wxColor fg, wxColor bg);

    // map an intensity to a display color
    wxColor intensityToColor(float f);

    // clear the display; home the cursor
    void scr_clear();

    // write 1 character to the video memory at location (x,y)
    // but don't redraw it.
    void scr_write_char(int x, int y, char ch)
        { m_display[m_chars_w*y + x] = ch; }

    // scroll the contents of the screen up one row,
    // and fill the new row with blanks.
    void scr_scroll();

    void setCursorX(int x);             // set horzontal position
    void setCursorY(int y);             // set vertical position
    void adjustCursorY(int delta);      // advance the cursor in y
    void adjustCursorX(int delta);      // move cursor left or right
    void updateCursor();                // redraw the cursor at current loc
    void explainError(const string &errcode,
                      const wxPoint  &orig); // pop up error description

    CrtFrame * const m_parent;  // who owns us
    const int   m_chars_w;      // screen dimension, in characters
    const int   m_chars_h;      // screen dimension, in characters

    unsigned char m_display[80*24]; // sometimes only 64x16 of it is used
    wxBitmap    m_scrbits;      // image of the display

    wxFont      m_font;         // font in use
    wxBitmap    m_fontmap;      // image of the font in use
    int         m_fontsize;     // size of font (in points)
    bool        m_fontdirty;    // font/color/contrast/brightness has changed
    int         m_charcell_w;   // width of one character cell
    int         m_charcell_h;   // height of one character cell
    int         m_begin_ul;     // vertical offset for underline
    int         m_begin_curs;   // vertical offset for cursor
    int         m_rows_curs;    // vertical height for ul/cursor

    wxColor     m_FGcolor;      // screen phosphor color
    wxColor     m_BGcolor;      // screen background color
    int         m_display_contrast;
    int         m_display_brightness;

    // this holds the dimensions of the visible area we have to draw on,
    // which is entirely independent of the logical CRT dimensions.
    int         m_scrpix_w;     // display dimension, in pixels
    int         m_scrpix_h;     // display dimension, in pixels
    wxRect      m_RCscreen;     // active text area

    int         m_curs_x;       // cursor location now
    int         m_curs_y;       // cursor location now
    int         m_curs_prevx;   // cursor location at last UpdateCursor()
    int         m_curs_prevy;   // cursor location at last UpdateCursor()
    bool        m_curs_on;      // cursor is enabled

    int         m_frame_count;  // for tracking refresh fps
    bool        m_dirty;        // something has changed since last refresh

    CANT_ASSIGN_OR_COPY_CLASS(Crt);
    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_H_
