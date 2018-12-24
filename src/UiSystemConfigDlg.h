// This is a model dialog box for configuring the emulated computer system.
// It keeps track of which CPU type is installed, how much RAM, whether the
// cpu and disk are to be emulated in realtime or as fast as possible, and
// it keeps track of the per-I/O card configuration state.
//
// Actually, the state is held elsewhere, but this carries out the UI to
// allow establishing state, and making or reverting state changes.

#ifndef _INCLUDE_UI_SYSTEM_CONFIG_DLG_H_
#define _INCLUDE_UI_SYSTEM_CONFIG_DLG_H_

#include "wx/wx.h"
#include "SysCfgState.h"

class SystemConfigDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(SystemConfigDlg);
    explicit SystemConfigDlg(wxFrame *parent);
    // ~SystemConfigDlg();

private:
    // ---- event handlers ----
    void OnCpuChoice(wxCommandEvent& WXUNUSED(event));
    void OnMemsizeChoice(wxCommandEvent& WXUNUSED(event));
    void OnCardChoice(wxCommandEvent &event);
    void OnAddrChoice(wxCommandEvent &event);
    void OnButton(wxCommandEvent &event);
    void OnDiskRealtime(wxCommandEvent& WXUNUSED(event));
    void OnWarnIo(wxCommandEvent& WXUNUSED(event));

    wxChoice *m_cpuType;                // B/T/VP/MVP/VLSI
    wxChoice *m_memSize;                // main memory size
    wxChoice *m_kbType;                 // keyboard type
    wxChoice *m_cardDesc[NUM_IOSLOTS];  // io slot card types
    wxChoice *m_cardAddr[NUM_IOSLOTS];  // io slot addresses
    wxButton *m_cardCfg[NUM_IOSLOTS];   // per-card configuration

    wxCheckBox *m_diskRealtime;         // whether disk emulation is realistic/fast

    wxCheckBox *m_warnIo;       // whether to want on access to non-existing IO devices
                                // the real Wang hardware ignored such actions
                                // the emulator has support to catch such actions, which is
                                // useful for debugging
    wxButton *m_btnRevert;
    wxButton *m_btnOk;
    wxButton *m_btnCancel;

    // system configuration state
    SysCfgState m_oldcfg;       // the existing configuration
    SysCfgState m_cfg;          // the one being modified

    // return true if it is OK to commit the state as it is
    bool configStateOk(bool warn);

    // helper routines
    void setValidIoChoices(int slot, int cardtype_idx);
    void updateDlg();
    void updateButtons();
    void setMemsizeStrings();

    // save/get dialog options to the config file
    void saveDefaults();
    void getDefaults();
};

#endif _INCLUDE_UI_SYSTEM_CONFIG_DLG_H_

// vim: ts=8:et:sw=4:smarttab
