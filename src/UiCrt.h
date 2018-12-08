// this class is responsible for drawing the CRT image on the window.
// it is subordinate to CrtFrame.

#ifndef _INCLUDE_UI_CRT_H_
#define _INCLUDE_UI_CRT_H_

class wxSound;
class CrtFrame;

class Crt: public wxWindow
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Crt);
    Crt( CrtFrame *parent, int screen_type);    // constructor
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
    int  getDisplayContrast()   const { return m_display_contrast; }
    int  getDisplayBrightness() const { return m_display_brightness; }

    // tracks whether screen might have changed
    void setDirty(bool dirty = true)
        { m_dirty = dirty; };
    bool isDirty() const
        { return m_dirty; };

    // tracks whether cached fontmap is out of date
    void setFontDirty(bool dirty = true);
    bool isFontDirty() const;

    void setFrameCount(int n)  { m_frame_count = n; }
    int  getFrameCount() const { return m_frame_count; }

    // hardware reset
    void reset(bool hard_reset);

    // redraw the CRT display as necessary
    void refreshWindow();

    // redraw entire screen, or just the text region
    void invalidateAll()  { Refresh(false); }
    void invalidateText() { Refresh(false, &m_RCscreen); }

    // return a pointer to the screen image
    wxBitmap* grabScreen();

    // send a character to the display controller
    void processChar(uint8 byte);

private:
    // ---- event handlers ----
    void OnEraseBackground(wxEraseEvent &event);
    void OnPaint(wxPaintEvent &event);
    void OnKeyDown(wxKeyEvent &event);
    void OnChar(wxKeyEvent &event);
    void OnSize(wxSizeEvent &event);
    void OnLeftDClick(wxMouseEvent &event);

    // ---- utility functions ----

    wxFont pickFont(int pointsize, int bold, const std::string &facename="");
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
    void generateScreenOverlay(wxMemoryDC &memDC);
    void generateScreenCursor(wxMemoryDC &memDC);

    // map an intensity to a display color
    wxColor intensityToColor(float f) const;

    // clear the display; home the cursor
    void scr_clear();

    // write 1 character to the video memory at location (x,y).
    // it is up to the caller to set the screen dirty flag.
    void scr_write_char(int x, int y, uint8 ch)
        { m_display[m_chars_w*y + x] = ch; }
    void scr_write_attr(int x, int y, uint8 attr)
        { m_attr[m_chars_w*y + x] = attr; }

    // lower level crt character handling
    void processCrtChar2(uint8 byte);
    // lowest level crt character handling
    void processCrtChar3(uint8 byte);
    // prt character handling (only one level of interpretation)
    void processPrtChar2(uint8 byte);

    // scroll the contents of the screen up one row,
    // and fill the new row with blanks.
    void scr_scroll();

    void setCursorX(int x);             // set horizontal position
    void setCursorY(int y);             // set vertical position
    void adjustCursorY(int delta);      // advance the cursor in y
    void adjustCursorX(int delta);      // move cursor left or right
    void explainError(const std::string &errcode,
                      const wxPoint &orig); // pop up error description

    CrtFrame * const m_parent;      // who owns us
    const int     m_screen_type;    // UI_SCREEN_* enum value
    const int     m_chars_w;        // screen dimension, in characters
    const int     m_chars_h;        // screen dimension, in characters
    const int     m_chars_h2;       // 2236: 25, otherwise == m_chars_h

    uint8         m_display[80*25]; // character codes
    wxBitmap      m_scrbits;        // image of the display

    wxBitmap      m_fontmap;        // image of the font in use
    int           m_fontsize;       // size of font (in points)
    bool          m_fontdirty;      // font/color/contrast/brightness changed
    int           m_charcell_w;     // width of one character cell
    int           m_charcell_h;     // height of one character cell
    int           m_charcell_sx;    // real x pixels per logical pixel
    int           m_charcell_sy;    // # real y pixels per logical pixel
    int           m_charcell_dy;    // row skipping factor

    wxColor       m_FGcolor;        // screen phosphor color
    wxColor       m_BGcolor;        // screen background color
    int           m_display_contrast;
    int           m_display_brightness;

    // this holds the dimensions of the visible area we have to draw on,
    // which is entirely independent of the logical CRT dimensions.
    int           m_scrpix_w;       // display dimension, in pixels
    int           m_scrpix_h;       // display dimension, in pixels
    wxRect        m_RCscreen;       // active text area

    enum cursor_attr_t {
        CURSOR_OFF,
        CURSOR_ON,
        CURSOR_BLINK
    };
    int           m_curs_x;         // cursor location now
    int           m_curs_y;         // cursor location now
    cursor_attr_t m_curs_attr;      // cursor state

    int           m_frame_count;    // for tracking refresh fps
    bool          m_dirty;          // something has changed since last refresh

    // state used only in smart terminal mode (eg 2236DE)
    enum char_attr_t : uint8 {
        CHAR_ATTR_RIGHT  = 0x01,  // top right horizontal line
        CHAR_ATTR_VERT   = 0x02,  // mid cell vertical line
        CHAR_ATTR_LEFT   = 0x04,  // top left horizontal line
        CHAR_ATTR_ALT    = 0x08,  // alternate character set
        CHAR_ATTR_BRIGHT = 0x10,  // high intensity
        CHAR_ATTR_BLINK  = 0x40,  // blink character
        CHAR_ATTR_INV    = 0x80,  // inverse character
    };
    uint8         m_attr[80*25];    // display attributes

    // byte stream command interpretation
    bool          m_crt_sink;       // true=route to crt, false=to prt
    int           m_raw_cnt;        // raw input stream buffered until we have
    uint8         m_raw_buf[5];     // ... a complete runlength sequence
    int           m_input_cnt;      // buffered input stream while decoding
    uint8         m_input_buf[10];  // ... character sequence escapes
    bool          m_box_bottom;     // true if we've seen at least one 0B
    int           m_attrs;          // current char attributes
    bool          m_attr_on;        // use attr bits until next 0F
    bool          m_attr_temp;      // use attr bits until next 0D or 0F
    bool          m_attr_under;     // draw underlined char if m_attr_on

    void setBoxAttr(bool box_draw, uint8 attr, int y_adj=0) {
        if (box_draw) {
            m_attr[80*(m_curs_y+y_adj) + m_curs_x] |=  attr;
        } else { // erase
            m_attr[80*(m_curs_y+y_adj) + m_curs_x] &= ~attr;
        }
    }

    // sound for beep
    void create_beep();
    std::shared_ptr<wxSound> m_beep;

    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_H_

// vim: ts=8:et:sw=4:smarttab
