// Dialog box to control the configuration of a particular disk controller.
// The state it tracks is
//    the number of drives associated with the controller
//    whether the controller is dumb or intelligent
//    whether or not to warn when the disk type doesn't match the intelligence

#ifndef _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_
#define _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_

#include "wx/wx.h"
#include "DiskCtrlCfgState.h"

class DiskCtrlCfgDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(DiskCtrlCfgDlg);
    DiskCtrlCfgDlg(wxFrame *parent, DiskCtrlCfgState &cfg);
    // ~DiskCtrlCfgDlg();

private:
    // ---- event handlers ----
    void OnNumDrives(wxCommandEvent &event);
    void OnIntelligence(wxCommandEvent &event);
    void OnWarnMismatch(wxCommandEvent &event);
    void OnButton(wxCommandEvent &event);

    wxRadioBox *m_rbNumDrives;      // number of attached disk drives
    wxRadioBox *m_rbIntelligence;   // dumb, smart, auto intelligence
    wxCheckBox *m_warnMismatch;     // warn if media & intelligence don't match
    wxButton   *m_btnRevert;
    wxButton   *m_btnOk;
    wxButton   *m_btnCancel;
    wxButton   *m_btnHelp;

    // system configuration state
    DiskCtrlCfgState &m_cfg;            // the one being modified
    DiskCtrlCfgState  m_oldcfg;         // the existing configuration

    // helper routines
    void updateDlg();

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();

    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_DISK_CONTROLLER_CFG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
