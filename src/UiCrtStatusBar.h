// CrtStatusBar is derived from wxStatusBar.  As the name suggests, it is the
// implementation of the status bar for the CrtFrame window.

#ifndef _INCLUDE_UI_CRT_STATUSBAR_H_
#define _INCLUDE_UI_CRT_STATUSBAR_H_

#include "w2200.h"
#include "wx/wx.h"
#include "wx/generic/statbmpg.h"  // because wxStaticBitmap doesn't catch mouse events on osx

// ----------------------------------------------------------------------------
// CrtStatusBar
// ----------------------------------------------------------------------------

// derive a status bar from the normal one so I can add a button control
class CrtStatusBar : public wxStatusBar
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(CrtStatusBar);
    CrtStatusBar(CrtFrame *parent, bool smart_term, bool primary_crt);
    ~CrtStatusBar() = default;

    void SetStatusMessage(const std::string &text);

    // keyword control
    void setKeywordMode(bool state = true);
    bool getKeywordMode() const;

    // something changed about the indicated drive -- refresh display
    void diskEvent(int slot, int drive);

private:

    const static int MAX_DISK_CONTROLLERS = 3;  // 310, 320, 330
    const static int MAX_DISK_DRIVES = (4*MAX_DISK_CONTROLLERS);

    // event handlers
    void OnSize(wxSizeEvent &event);
    void OnKeywordCtl(wxCommandEvent &event);
    void OnDiskButton(wxMouseEvent &event);
    void OnDiskPopup(wxCommandEvent &event);
    void OnMouseBtnDown(wxMouseEvent &event);

    void SetDiskIcon(const int slot, const int drive);

    CrtFrame * const m_parent;
    wxCheckBox     *m_keyword_ctl                             = nullptr;    // set = "Keyword/A", unset = "A/a"
    wxStaticText   *m_disk_label[2*MAX_DISK_CONTROLLERS]      = {nullptr};  // eg "310:"
    wxGenericStaticBitmap *m_disk_icon[MAX_DISK_DRIVES]       = {nullptr};  // icon for disk state
    int             m_disk_label_xoff[2*MAX_DISK_CONTROLLERS] = {0};        // positioning
    int             m_disk_icon_xoff[MAX_DISK_DRIVES]         = {0};        // positioning
    int             m_disk_state[MAX_DISK_DRIVES]             = {-1};       // which icon we are showing
    wxIcon          m_icon_set[6];                                          // collection of images in one place

    int  m_num_disk_controllers = 0;                // number of disk controllers
    int  m_num_drives[MAX_DISK_CONTROLLERS] = {0};  // drives per controller

    enum { unknown, insert_disk, eject_disk, inspect_disk, format_disk }
          m_popup_action = unknown;
};

#endif // _INCLUDE_UI_CRT_STATUSBAR_H_

// vim: ts=8:et:sw=4:smarttab
