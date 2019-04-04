// this implements a modal dialog box for configuring the emulated CRT

#ifndef _INCLUDE_UI_CRT_CONFIG_DLG_H_
#define _INCLUDE_UI_CRT_CONFIG_DLG_H_

#include "w2200.h"
#include "wx/wx.h"

class CrtConfigDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(CrtConfigDlg);
    CrtConfigDlg(wxFrame *parent, const wxString &title, const wxString &subgroup);

    ~CrtConfigDlg() override;

private:
    // ---- event handlers ----
    void OnFontChoice(wxCommandEvent &event);
    void OnColorChoice(wxCommandEvent& WXUNUSED(event));
    void OnContrastSlider(wxScrollEvent &event);
    void OnBrightnessSlider(wxScrollEvent &event);

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();

    void updateDlg();   // make dialog reflect system state

    // data member
    const std::string m_subgroup;                 // ini file tag
    wxChoice    *m_font_choice = nullptr;         // font type and size
    wxChoice    *m_color_choice = nullptr;        // fg/bg color scheme
    wxSlider    *m_contrast_slider = nullptr;     // display contrast
    wxSlider    *m_brightness_slider = nullptr;   // display brightness
};

#endif // _INCLUDE_UI_CRT_CONFIG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
