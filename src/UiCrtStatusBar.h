// CrtStatusBar is derived from wxStatusBar.  As the name suggests, it is the
// implementation of the status bar for the CrtFrame window.

#ifndef _INCLUDE_UI_CRT_STATUSBAR_H_
#define _INCLUDE_UI_CRT_STATUSBAR_H_

// ----------------------------------------------------------------------------
// MyStaticBitmap
// ----------------------------------------------------------------------------
// the statusbar uses staticbitmaps to hold the icons for the disk state.
// (at first wxBitmapButtons were used, but on WXMAC it wasn't possible to
//  draw them flat and all sorts of bitmap clipping issues came up, so it
//  was abandoned (2005-03-06)).  staticbitmaps don't capture mouse events,
// thus this subclass.

// On wxMSW we don't need to do this -- redraw just works.
// On wxMAC, the control is active but the bitmap isn't drawn.
// If we remove all the EVENT_TABLE plumbing then the images show up
// but of course we don't handle mouse events then, so we handle the
// paint event.  This paint handler works too under wxMSW (with the
// bit of conditional code below), but we don't use it since not
// handling the paint event is probably more robust.
#ifdef __WXMAC__
    #define HANDLE_MSB_PAINT 1
#else
    #define HANDLE_MSB_PAINT 0
#endif

class MyStaticBitmap : public wxStaticBitmap
{
public:
    MyStaticBitmap( wxWindow *parent, wxWindowID id, const wxBitmap &label,
                    const wxPoint &pos = wxDefaultPosition,
                    const wxSize &size = wxDefaultSize,
                    long style = 0, const string &name = "staticBitmap" );
private:
    void OnMouseBtnDown(wxMouseEvent &event);
#if HANDLE_MSB_PAINT
    void OnPaint(wxPaintEvent& WXUNUSED(event));
#endif
    int m_myid;

    CANT_ASSIGN_OR_COPY_CLASS(MyStaticBitmap);
    DECLARE_EVENT_TABLE()
};

// ----------------------------------------------------------------------------
// CrtStatusBar
// ----------------------------------------------------------------------------

// derive a status bar from the normal one so I can add a button control
class CrtStatusBar : public wxStatusBar
{
public:
    CrtStatusBar(CrtFrame *parent, bool shown);
    ~CrtStatusBar();

    void SetStatusMessage(const string &text);

    // keyword control
    void setKeywordMode(bool state = true);
    bool getKeywordMode() const;

    // something changed about the indicated drive -- refresh display
    void diskEvent(int controller, int drive);

private:

    const static int MAX_DISK_CONTROLLERS = 3;  // 310, 320, 330
    const static int MAX_DISK_DRIVES = (4*MAX_DISK_CONTROLLERS);

    // event handlers
    void OnSize(wxSizeEvent &event);
    void OnKeywordCtl(wxCommandEvent &event);
    void OnDiskButton(wxMouseEvent &event);
    void OnDiskPopup(wxCommandEvent &event);

    void SetDiskIcon(const int slot, const int drive);

    CrtFrame * const m_parent;
    wxCheckBox     *m_keyword_ctl;      // set = "Keyword/A", unset = "A/a"
    wxStaticText   *m_disklabel[2*MAX_DISK_CONTROLLERS]; // eg "310:"
    int             m_disklabel_xoff[2*MAX_DISK_CONTROLLERS];  // positioning
    MyStaticBitmap *m_diskicon[MAX_DISK_DRIVES];  // icon for disk state
    int             m_diskicon_xoff[MAX_DISK_DRIVES];  // positioning
    int             m_diskstate[MAX_DISK_DRIVES]; // which icon we are showing
    wxBitmap       *m_icon_set;         // collection of images in one place

    int  m_num_disk_controllers;                // number of disk controllers
    int  m_num_drives[MAX_DISK_CONTROLLERS];    // drives per controller

    bool m_shown;                               // whether statbar is hidden

    enum { unknown, insert_disk, eject_disk, inspect_disk, format_disk }
          m_popup_action;

    CANT_ASSIGN_OR_COPY_CLASS(CrtStatusBar);
    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_STATUSBAR_H_
