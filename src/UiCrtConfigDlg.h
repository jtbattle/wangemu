// this implements a modal dialog box for configuring the emulated CRT

#ifndef _INCLUDE_UI_CRT_CONFIG_DLG_H_
#define _INCLUDE_UI_CRT_CONFIG_DLG_H_

class CrtConfigDlg : public wxDialog
{
public:
    CrtConfigDlg(wxFrame *parent, wxString title);
    ~CrtConfigDlg();

private:
    // ---- event handlers ----
    void OnFontChoice( wxCommandEvent &event );
    void OnColorChoice( wxCommandEvent& WXUNUSED(event) );
    void OnContrastSlider( wxScrollEvent &event );
    void OnBrightnessSlider( wxScrollEvent &event );

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();

    void updateDlg();   // make dialog reflect system state

    // data member
    wxChoice    *m_FontChoice;          // font type and size
    wxChoice    *m_ColorChoice;         // fg/bg color scheme
    wxSlider    *m_ContrastSlider;      // display contrast
    wxSlider    *m_BrightnessSlider;    // display brightness

    CANT_ASSIGN_OR_COPY_CLASS(CrtConfigDlg);
    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_CONFIG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
