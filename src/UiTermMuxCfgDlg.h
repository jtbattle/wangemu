// Dialog box to control the configuration of a particular disk controller.
// The state it tracks is
//    the number of drives associated with the controller
//    whether the controller is dumb or intelligent
//    whether or not to warn when the disk type doesn't match the intelligence

#ifndef _INCLUDE_UI_TERMINAL_MUX_CFG_DLG_H_
#define _INCLUDE_UI_TERMINAL_MUX_CFG_DLG_H_

#include "wx/wx.h"
#include "TermMuxCfgState.h"

class TermMuxCfgDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(TermMuxCfgDlg);
    TermMuxCfgDlg(wxFrame *parent, CardCfgState &cfg);
    // ~TermMuxCfgDlg();

private:
    // ---- event handlers ----
    void OnNumTerminals(wxCommandEvent &event);
    void OnButton(wxCommandEvent &event);

    wxRadioBox *m_rb_num_terminals;   // number of attached terminals
    wxButton   *m_btn_revert;
    wxButton   *m_btn_ok;
    wxButton   *m_btn_cancel;
    wxButton   *m_btn_help;

    // system configuration state
    TermMuxCfgState &m_cfg;         // the one being modified
    TermMuxCfgState  m_old_cfg;     // the existing configuration

    // helper routines
    void updateDlg();

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();
};

#endif _INCLUDE_UI_TERMINAL_MUX_CFG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
