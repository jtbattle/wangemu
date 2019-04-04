// This implements a modal dialog box for creating, formatting, and inspecting
// virtual disk images.

#ifndef _INCLUDE_UI_DISK_FACTORY_H_
#define _INCLUDE_UI_DISK_FACTORY_H_

#include "w2200.h"
#include "wx/wx.h"

class PropPanel;
class LabelPanel;
class Wvd;

class DiskFactory : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(DiskFactory);
     DiskFactory(wxFrame *parent, const std::string &filename);
    ~DiskFactory() override;

    void updateButtons();  // so ugly -- only needed by subordinate notebook pages
                           // why advertise it to the rest of the world?
private:
    // ---- event handlers ----
    void OnButton_Save(wxCommandEvent& WXUNUSED(event));
    void OnButton_SaveAs(wxCommandEvent& WXUNUSED(event));
    void OnButton_Cancel(wxCommandEvent& WXUNUSED(event));
    void OnSize(wxSizeEvent &event);
    void OnClose(wxCloseEvent& WXUNUSED(event));

    // helper routines
    void updateDlg();

    // member data
    std::shared_ptr<Wvd> m_disk_data;
    PropPanel  *m_tab1       = nullptr;
    LabelPanel *m_tab2       = nullptr;
    wxButton   *m_btn_cancel = nullptr;
    wxButton   *m_btn_save   = nullptr;
};

#endif // _INCLUDE_UI_DISK_FACTORY_H_

// vim: ts=8:et:sw=4:smarttab
