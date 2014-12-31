// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Host.h"
#include "IoCard.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "DiskCtrlCfgState.h"
#include "UiDiskCtrlCfgDlg.h"
#include "System2200.h"

// ----------------------------------------------------------------------------
// a simple static dialog to provide help on the DiskCtrlCfgDlg options
// ----------------------------------------------------------------------------

class DiskCtrlCfgHelpDlg : public wxDialog
{
public:
    DiskCtrlCfgHelpDlg(wxWindow *parent);
private:
    CANT_ASSIGN_OR_COPY_CLASS(DiskCtrlCfgHelpDlg);
    //DECLARE_EVENT_TABLE()
};


DiskCtrlCfgHelpDlg::DiskCtrlCfgHelpDlg(wxWindow *parent)
        : wxDialog(parent, -1, "Disk Controller Configuration Help",
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl *txt = new wxTextCtrl(this, wxID_ANY, _T(""),
                               wxDefaultPosition, wxSize(480,400),
                               wxTE_RICH2 | wxTE_MULTILINE | wxTE_READONLY |
                               wxBORDER_NONE);

    txt->SetBackgroundColour( // wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)
                                 wxColour( 0xec, 0xe9, 0xd8 )
                            );

    // font for section headings
    wxFont section_font(12, wxFONTFAMILY_DEFAULT,
                            wxFONTSTYLE_NORMAL,
                            wxFONTWEIGHT_BOLD);

    wxColor section_color("BLACK");
    wxTextAttr section_attr;
    section_attr.SetTextColour(section_color);
    section_attr.SetFont(section_font);
    section_attr.SetLeftIndent(12);
    section_attr.SetRightIndent(12);

    // font for body of text
    wxFont body_font(10, wxFONTFAMILY_DEFAULT,
                         wxFONTSTYLE_NORMAL,
                         wxFONTWEIGHT_NORMAL);
    wxColor body_color( wxColour(0x00, 0x00, 0xC0) );
    wxTextAttr body_attr;
    body_attr.SetTextColour(body_color);
    body_attr.SetFont(body_font);
    body_attr.SetLeftIndent(50);
    body_attr.SetRightIndent(12);

    // create the message
    txt->SetDefaultStyle(section_attr);
    txt->AppendText(_T("Number of Drives\n"));

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        _T("\n")
        _T("Each disk controller supports from one to four drives.  ")
        _T("The first two drives are the primary drives, while the ")
        _T("second two are the secondary drives.  The primary drives ")
        _T("are addressed using the base address of the card, eg /310, ")
        _T("while the secondary drives are addressed by adding hex 40, ")
        _T("e.g., /350.  The first of each pair is historically called ")
        _T("the fixed, or F, drive, and the second of each pair is the ")
        _T("removable, or R, drive.")
        _T("\n\n")
        _T("Thus, if the base address of the controller is at hex 10, ")
        _T("the four drives can be referenced as F/310, R/310, F/350, R/350.  ")
        _T("Read the BASIC-2 disk reference manual for more details and ")
        _T("options.")
        _T("\n\n") );

    txt->SetDefaultStyle(section_attr);
    txt->AppendText(_T("Controller Intelligence\n"));

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        _T("\n")
        _T("In general, it is best to use the intelligent disk controller ")
        _T("mode.  The first generation CPUs, namely either 2220B or 2200T, ")
        _T("don't support the intelligent protocol, so intelligent disk ")
        _T("controllers pretend to be dumb anyway, making the choice moot.")
        _T("\n\n")
        _T("The protocol used by dumb disk controllers only allows for up ")
        _T("to 32K sectors per platter, and only a single platter can be ")
        _T("addressed per drive.  This is sufficient for floppy drives as ")
        _T("well as 2260-style drives.")
        _T("\n\n")
        _T("The protocol used by intelligent disk controllers allows for 64K ")
        _T("sectors per platter, and up to fourteen platters per drive.")
        _T("The protocol also allows for software-controlled disk formatting ")
        _T("and higher speed copying of sectors on a single platter or between ")
        _T("any two platters connected to the same disk controller.")
        _T("\n\n")
        _T("At first blush it seems like there is no drawback to simply always ")
        _T("using the intelligent disk controller, but there is a fly in ")
        _T("the ointment.  For unknown reasons, Wang BASIC used in the first ")
        _T("generation of machines would set the high bit of the 16 bit ")
        _T("sector address on files stored in the disk catalog if the file ")
        _T("was in the 'R' drive.")
        _T("\n\n")
        _T("Wang BASIC reads sector addresses from the disk, it ignores this ")
        _T("16th bit so no harm is done.  BASIC-2 also ignores this bit when ")
        _T("it is communicating with a dumb disk controller.  However, if one ")
        _T("uses a disk that was created on a dumb controller and inserts it ")
        _T("into the drive of an intelligent controller, that 16th bit is not ")
        _T("ignored, and confusion reigns.")
        _T("\n\n")
        _T("If a disk with this 16th bit problem is inserted into a drive in ")
        _T("intelligent mode, a warning will be generated and optinoally these ")
        _T("extraneous bits can be cleaned from the virtual disk image.")
#if SUPPORT_AUTO_INTELLIGENCE
        _T("\n\n"));
        _T("Selecting the AUTO mode will cause the emulator to heursitically ")
        _T("pick dumb or intelligent mode based on the types of disk images ")
        _T("associated with a controller.  This precludes having to clear ")
        _T("the 16th bits from problem disks, but the heuristic isn't perfect.")
#endif
        _T("\n\n"));

    txt->SetDefaultStyle(section_attr);
    txt->AppendText(_T("Warn when the media doesn't match the controller intelligence\n"));

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        _T("\n")
        _T("Checking this box will cause the emulator to warn the user if ")
        _T("a large disk is put into a dumb controller, or a small disk ")
        _T("that has the 16th bit problem is inserted into an intelligent ")
        _T("controller.  In the latter case, the user has the option of ")
        _T("automatically clearing these extraneous bits.")
        _T("\n\n"));

    // make sure the start of text is at the top
    txt->SetInsertionPoint(0);
    txt->ShowPosition(false);

    // make it fill the window, and show it
    wxBoxSizer *sz = new wxBoxSizer(wxVERTICAL);
    sz->Add(txt, 1, wxEXPAND);
    SetSizerAndFit(sz);
}

// ----------------------------------------------------------------------------
// DiskCtrlCfgDlg implementation
// ----------------------------------------------------------------------------

enum
{
    ID_RB_NUM_DRIVES = 100,             // radio box
    ID_RB_INTELLIGENCE,                 // radio box
    ID_CHK_WARN_MISMATCH,               // check box

    ID_BTN_HELP   = 300,
    ID_BTN_REVERT
};


// dialog events to catch
BEGIN_EVENT_TABLE(DiskCtrlCfgDlg, wxDialog)
    EVT_RADIOBOX(ID_RB_NUM_DRIVES,      DiskCtrlCfgDlg::OnNumDrives)
    EVT_RADIOBOX(ID_RB_INTELLIGENCE,    DiskCtrlCfgDlg::OnIntelligence)
    EVT_CHECKBOX(ID_CHK_WARN_MISMATCH,  DiskCtrlCfgDlg::OnWarnMismatch)
    EVT_BUTTON(-1,                      DiskCtrlCfgDlg::OnButton)
END_EVENT_TABLE()


// Layout:
//      topsizer (V)
//      |
//      +-- num drives radiobox (H)
//      +-- disk intelligence radiobox (H)
//      +-- warn on mismatch checkbox
//      +-- button_sizer (H)
//          |
//          +-- m_btnHelp
//          +-- m_btnRevert
//          +-- m_btnOk
//          +-- m_btnCancel
DiskCtrlCfgDlg::DiskCtrlCfgDlg(wxFrame *parent, DiskCtrlCfgState &cfg) :
        wxDialog(parent, -1, "Disk Controller Configuration",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_rbNumDrives(NULL),
        m_rbIntelligence(NULL),
        m_warnMismatch(NULL),
        m_btnRevert(NULL),
        m_btnOk(NULL),
        m_btnCancel(NULL),
        m_btnHelp(NULL),
        m_oldcfg( cfg ),  // the existing configuration
        m_cfg   ( cfg )   // we edit this, and it is the exact one passed to us
{
    wxString choicesNumDrives[] = { _T("1"), _T("2"), _T("3"), _T("4") };
    m_rbNumDrives = new wxRadioBox(this, ID_RB_NUM_DRIVES,
                                   "Number of drives",
                                   wxDefaultPosition, wxDefaultSize,
                                   4, choicesNumDrives,
                                   1, wxRA_SPECIFY_ROWS);
    m_rbNumDrives->SetItemToolTip(0, "Primary F drive");
    m_rbNumDrives->SetItemToolTip(1, "Primary R drive");
    m_rbNumDrives->SetItemToolTip(2, "Secondary F drive");
    m_rbNumDrives->SetItemToolTip(3, "Secondary R drive");

    wxString choicesIntelligence[] = {
          _T("Dumb")
        , _T("Intelligent")
#if SUPPORT_AUTO_INTELLIGENCE
        , _T("Automatic")
#endif
    };
    const int numIntelligenceChoices = (SUPPORT_AUTO_INTELLIGENCE) ? 3 : 2;

    m_rbIntelligence = new wxRadioBox(this, ID_RB_INTELLIGENCE,
                                      "Controller Type",
                                      wxDefaultPosition, wxDefaultSize,
                                      numIntelligenceChoices, choicesIntelligence,
                                      1, wxRA_SPECIFY_ROWS);
    m_rbIntelligence->SetItemToolTip(0, "Controller for single platter\n"
                                        "drives with <= 32K sectors" );
    m_rbIntelligence->SetItemToolTip(1, "Controller for multiplatter drives\n"
                                        "or drives with >32K sectors" );
#if SUPPORT_AUTO_INTELLIGENCE
    m_rbIntelligence->SetItemToolTip(2,
                                "Try to adapt intelligence based on inserted\n"
                                "media types.  Problems may arise if the\n"
                                "types are mixed in a single drive.");
#endif

    m_warnMismatch = new wxCheckBox(this, ID_CHK_WARN_MISMATCH,
                "Warn when the media doesn't match the controller intelligence");
    m_warnMismatch->SetToolTip(
                "Dumb controllers can't address sector counts > 32K/platter,\n"
                "nor can they address anything other than the first platter\n"
                "of a multiplatter disk.  Intelligent controllers can access\n"
                "large and small drives alike, but some small drives have a\n"
                "disk catalog with the 16th bit of sector addresses set.\n"
                "Dumb drives ignore this bit, but smart drives don't and\n"
                "can cause problems." );

    // put three buttons side by side
    m_btnHelp   = new wxButton(this, ID_BTN_HELP,   "Help");
    m_btnRevert = new wxButton(this, ID_BTN_REVERT, "Revert");
    m_btnOk     = new wxButton(this, wxID_OK,       "OK");
    m_btnCancel = new wxButton(this, wxID_CANCEL,   "Cancel");

    wxBoxSizer *button_sizer = new wxBoxSizer( wxHORIZONTAL );
    button_sizer->Add(m_btnHelp,   0, wxALL, 10);
    button_sizer->Add(m_btnRevert, 0, wxALL, 10);
    button_sizer->Add(m_btnOk,     0, wxALL, 10);
    button_sizer->Add(m_btnCancel, 0, wxALL, 10);
#ifdef __WXMAC__
    // the cancel button was running into the window resizing grip
    button_sizer->AddSpacer(10);
#endif
    m_btnRevert->Disable();      // until something changes

    // all of it is stacked vertically
    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );
    topsizer->Add(m_rbNumDrives,    0, wxALIGN_LEFT | wxALL, 5);
    topsizer->Add(m_rbIntelligence, 0, wxALIGN_LEFT | wxALL, 5);
    topsizer->Add(m_warnMismatch,   0, wxALIGN_LEFT | wxALL, 5);
    topsizer->AddStretchSpacer();
    topsizer->Add(button_sizer,     0, wxALIGN_RIGHT | wxALL, 5);

    updateDlg();                        // select current options

    // tell the thing to get to work
    SetSizer(topsizer);                 // use the sizer for layout
    topsizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();  // get default size & location
}


// update the display to reflect the current state
void
DiskCtrlCfgDlg::updateDlg()
{
    m_rbNumDrives->SetSelection( m_cfg.getNumDrives()-1 );
    switch (m_cfg.getIntelligence()) {
        case DiskCtrlCfgState::DISK_CTRL_DUMB:
                m_rbIntelligence->SetSelection(0); break;
        case DiskCtrlCfgState::DISK_CTRL_INTELLIGENT:
                m_rbIntelligence->SetSelection(1); break;
#if SUPPORT_AUTO_INTELLIGENCE
        case DiskCtrlCfgState::DISK_CTRL_AUTO:
                m_rbIntelligence->SetSelection(2); break;
#endif
        default: assert(0);
    }
    m_warnMismatch->SetValue( m_cfg.getWarnMismatch() );
}


void
DiskCtrlCfgDlg::OnNumDrives( wxCommandEvent& WXUNUSED(event) )
{
    switch (m_rbNumDrives->GetSelection()) {
        case 0: m_cfg.setNumDrives(1); break;
        case 1: m_cfg.setNumDrives(2); break;
        case 2: m_cfg.setNumDrives(3); break;
        case 3: m_cfg.setNumDrives(4); break;
        default: assert(0); break;
    }
    m_btnRevert->Enable( m_cfg != m_oldcfg );
}


void
DiskCtrlCfgDlg::OnIntelligence( wxCommandEvent& WXUNUSED(event) )
{
    switch (m_rbIntelligence->GetSelection()) {
        case 0: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_DUMB); break;
        case 1: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_INTELLIGENT); break;
#if SUPPORT_AUTO_INTELLIGENCE
        case 2: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_AUTO); break;
#endif
        default: assert(0); break;
    }
    m_btnRevert->Enable( m_cfg != m_oldcfg );
}


void
DiskCtrlCfgDlg::OnWarnMismatch( wxCommandEvent& WXUNUSED(event) )
{
    bool checked = m_warnMismatch->IsChecked();
    m_cfg.setWarnMismatch( checked );
    m_btnRevert->Enable( m_cfg != m_oldcfg );
}


// used for all dialog button presses
void
DiskCtrlCfgDlg::OnButton(wxCommandEvent& event)
{
    switch (event.GetId()) {

        case ID_BTN_HELP:
            {
                DiskCtrlCfgHelpDlg *helpDlg = new DiskCtrlCfgHelpDlg(this);
                helpDlg->ShowModal();
            }
            break;

        case ID_BTN_REVERT:
            m_cfg = m_oldcfg;           // revert state
            updateDlg();                // select current options
            m_btnRevert->Disable();
            break;

        case wxID_OK:
        {
            // make sure all io addresses have been selected
            // see if config mgr is happy with things
            if (m_cfg.configOk(true)) {
                saveDefaults();         // save location & size of dlg
                EndModal(0);
            }
            break;
        }

        case wxID_CANCEL:
            saveDefaults();             // save location & size of dlg
            EndModal(1);
            break;

        default:
            event.Skip();
            break;
    }
}


// save dialog options to the config file
void
DiskCtrlCfgDlg::saveDefaults()
{
    const string subgroup("ui/diskcfgdlg");

    // save position and size
    Host().ConfigWriteWinGeom(this, subgroup);
}


void
DiskCtrlCfgDlg::getDefaults()
{
    // see if we've established a favored location and size
    const string subgroup("ui/diskcfgdlg");
    Host().ConfigReadWinGeom(this, subgroup);
}
