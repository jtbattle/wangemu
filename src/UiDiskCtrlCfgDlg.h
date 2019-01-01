// Dialog box to control the configuration of a particular disk controller.
// The state it tracks is
//    the number of drives associated with the controller
//    whether the controller is dumb or intelligent
//    whether or not to warn when the disk type doesn't match the intelligence

#ifndef _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_
#define _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_

#include "DiskCtrlCfgState.h"
#include "wx/wx.h"

class DiskCtrlCfgDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(DiskCtrlCfgDlg);
    DiskCtrlCfgDlg(wxFrame *parent, CardCfgState &cfg);
    // ~DiskCtrlCfgDlg();

private:
    // ---- event handlers ----
    void OnNumDrives(wxCommandEvent &event);
    void OnIntelligence(wxCommandEvent &event);
    void OnWarnMismatch(wxCommandEvent &event);
    void OnButton(wxCommandEvent &event);

    wxRadioBox *m_rb_num_drives;     // number of attached disk drives
    wxRadioBox *m_rb_intelligence;   // dumb, smart, auto intelligence
    wxCheckBox *m_warn_mismatch;     // warn if media & intelligence don't match
    wxButton   *m_btn_revert;
    wxButton   *m_btn_ok;
    wxButton   *m_btn_cancel;
    wxButton   *m_btn_help;

    // system configuration state
    DiskCtrlCfgState &m_cfg;        // the one being modified
    DiskCtrlCfgState  m_old_cfg;    // a copy of the starting state

    // helper routines
    void updateDlg();

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();
};

#endif // _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
