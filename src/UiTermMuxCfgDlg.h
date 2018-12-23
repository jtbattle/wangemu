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

    wxRadioBox *m_rbNumTerminals;   // number of attached terminals
    wxButton   *m_btnRevert;
    wxButton   *m_btnOk;
    wxButton   *m_btnCancel;
    wxButton   *m_btnHelp;

    // system configuration state
    TermMuxCfgState &m_cfg;         // the one being modified
    TermMuxCfgState  m_oldcfg;      // the existing configuration

    // helper routines
    void updateDlg();

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();

    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_TERMINAL_MUX_CFG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
