// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "host.h"
#include "IoCard.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "DiskCtrlCfgState.h"
#include "UiDiskCtrlCfgDlg.h"
#include "system2200.h"

// ----------------------------------------------------------------------------
// a simple static dialog to provide help on the DiskCtrlCfgDlg options
// ----------------------------------------------------------------------------

class DiskCtrlCfgHelpDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(DiskCtrlCfgHelpDlg);
    explicit DiskCtrlCfgHelpDlg(wxWindow *parent);
    //DECLARE_EVENT_TABLE()
};


DiskCtrlCfgHelpDlg::DiskCtrlCfgHelpDlg(wxWindow *parent)
        : wxDialog(parent, -1, "Disk Controller Configuration Help",
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl *txt = new wxTextCtrl(this, wxID_ANY, "",
                               wxDefaultPosition, wxSize(480, 400),
                               wxTE_RICH2 | wxTE_MULTILINE | wxTE_READONLY |
                               wxBORDER_NONE);

    txt->SetBackgroundColour(// wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)
                                wxColour(0xec, 0xe9, 0xd8));

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
    wxColor body_color(wxColour(0x00, 0x00, 0xC0));
    wxTextAttr body_attr;
    body_attr.SetTextColour(body_color);
    body_attr.SetFont(body_font);
    body_attr.SetLeftIndent(50);
    body_attr.SetRightIndent(12);

    // create the message
    txt->SetDefaultStyle(section_attr);
    txt->AppendText("Number of Drives\n");

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        "\n"
        "Each disk controller supports from one to four drives.  "
        "The first two drives are the primary drives, while the "
        "second two are the secondary drives.  The primary drives "
        "are addressed using the base address of the card, eg /310, "
        "while the secondary drives are addressed by adding hex 40, "
        "e.g., /350.  The first of each pair is historically called "
        "the fixed, or F, drive, and the second of each pair is the "
        "removable, or R, drive."
        "\n\n"
        "Thus, if the base address of the controller is at hex 10, "
        "the four drives can be referenced as F/310, R/310, F/350, R/350.  "
        "Read the BASIC-2 disk reference manual for more details and "
        "options."
        "\n\n");

    txt->SetDefaultStyle(section_attr);
    txt->AppendText("Controller Intelligence\n");

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        "\n"
        "In general, it is best to use the intelligent disk controller "
        "mode.  The first generation CPUs, namely either 2220B or 2200T, "
        "don't support the intelligent protocol, so intelligent disk "
        "controllers pretend to be dumb anyway, making the choice moot."
        "\n\n"
        "The protocol used by dumb disk controllers only allows for up "
        "to 32K sectors per platter, and only a single platter can be "
        "addressed per drive.  This is sufficient for floppy drives as "
        "well as 2260-style drives."
        "\n\n"
        "The protocol used by intelligent disk controllers allows for 64K "
        "sectors per platter, and up to fourteen platters per drive."
        "The protocol also allows for software-controlled disk formatting "
        "and higher speed copying of sectors on a single platter or between "
        "any two platters connected to the same disk controller."
        "\n\n"
        "At first blush it seems like there is no drawback to simply always "
        "using the intelligent disk controller, but there is a fly in "
        "the ointment.  For unknown reasons, Wang BASIC used in the first "
        "generation of machines would set the high bit of the 16 bit "
        "sector address on files stored in the disk catalog if the file "
        "was in the 'R' drive."
        "\n\n"
        "Wang BASIC reads sector addresses from the disk, it ignores this "
        "16th bit so no harm is done.  BASIC-2 also ignores this bit when "
        "it is communicating with a dumb disk controller.  However, if one "
        "uses a disk that was created on a dumb controller and inserts it "
        "into the drive of an intelligent controller, that 16th bit is not "
        "ignored, and confusion reigns."
        "\n\n"
        "If a disk with this 16th bit problem is inserted into a drive in "
        "intelligent mode, a warning will be generated and optionally these "
        "extraneous bits can be cleaned from the virtual disk image."
#if SUPPORT_AUTO_INTELLIGENCE
        "\n\n"
        "Selecting the AUTO mode will cause the emulator to heuristically "
        "pick dumb or intelligent mode based on the types of disk images "
        "associated with a controller.  This precludes having to clear "
        "the 16th bits from problem disks, but the heuristic isn't perfect."
#endif
        "\n\n");

    txt->SetDefaultStyle(section_attr);
    txt->AppendText("Warn when the media doesn't match the controller intelligence\n");

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        "\n"
        "Checking this box will cause the emulator to warn the user if "
        "a large disk is put into a dumb controller, or a small disk "
        "that has the 16th bit problem is inserted into an intelligent "
        "controller.  In the latter case, the user has the option of "
        "automatically clearing these extraneous bits."
        "\n\n");

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


// Layout:
//      top_sizer (V)
//      |
//      +-- num drives radiobox (H)
//      +-- disk intelligence radiobox (H)
//      +-- warn on mismatch checkbox
//      +-- button_sizer (H)
//          |
//          +-- m_btn_help
//          +-- m_btn_revert
//          +-- m_btn_ok
//          +-- m_btn_cancel
DiskCtrlCfgDlg::DiskCtrlCfgDlg(wxFrame *parent, CardCfgState &cfg) :
        wxDialog(parent, -1, "Disk Controller Configuration",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_rb_num_drives(nullptr),
        m_rb_intelligence(nullptr),
        m_warn_mismatch(nullptr),
        m_btn_revert(nullptr),
        m_btn_ok(nullptr),
        m_btn_cancel(nullptr),
        m_btn_help(nullptr),
        m_cfg(dynamic_cast<DiskCtrlCfgState&>(cfg)),      // edited version
        m_old_cfg(dynamic_cast<DiskCtrlCfgState&>(cfg))   // copy of original
{
    const wxString num_drive_choices[] = { "1", "2", "3", "4" };
    m_rb_num_drives = new wxRadioBox(this, ID_RB_NUM_DRIVES,
                                     "Number of drives",
                                     wxDefaultPosition, wxDefaultSize,
                                     4, &num_drive_choices[0],
                                     1, wxRA_SPECIFY_ROWS);
    m_rb_num_drives->SetItemToolTip(0, "Primary F drive");
    m_rb_num_drives->SetItemToolTip(1, "Primary R drive");
    m_rb_num_drives->SetItemToolTip(2, "Secondary F drive");
    m_rb_num_drives->SetItemToolTip(3, "Secondary R drive");

    wxString intelligenceChoices[] = {
          "Dumb"
        , "Intelligent"
#if SUPPORT_AUTO_INTELLIGENCE
        , "Automatic"
#endif
    };
    const int num_intelligence_choices = (SUPPORT_AUTO_INTELLIGENCE) ? 3 : 2;

    m_rb_intelligence = new wxRadioBox(this, ID_RB_INTELLIGENCE,
                                       "Controller Type",
                                       wxDefaultPosition, wxDefaultSize,
                                       num_intelligence_choices,
                                       &intelligenceChoices[0],
                                       1, wxRA_SPECIFY_ROWS);
    m_rb_intelligence->SetItemToolTip(0, "Controller for single platter\n"
                                         "drives with <= 32K sectors");
    m_rb_intelligence->SetItemToolTip(1, "Controller for multiplatter drives\n"
                                         "or drives with >32K sectors");
#if SUPPORT_AUTO_INTELLIGENCE
    m_rb_intelligence->SetItemToolTip(2,
                                "Try to adapt intelligence based on inserted\n"
                                "media types.  Problems may arise if the\n"
                                "types are mixed in a single drive.");
#endif

    m_warn_mismatch = new wxCheckBox(this, ID_CHK_WARN_MISMATCH,
                "Warn when the media doesn't match the controller intelligence");
    m_warn_mismatch->SetToolTip(
                "Dumb controllers can't address sector counts > 32K/platter,\n"
                "nor can they address anything other than the first platter\n"
                "of a multiplatter disk.  Intelligent controllers can access\n"
                "large and small drives alike, but some small drives have a\n"
                "disk catalog with the 16th bit of sector addresses set.\n"
                "Dumb drives ignore this bit, but smart drives don't and\n"
                "can cause problems.");

    // put three buttons side by side
    m_btn_help   = new wxButton(this, ID_BTN_HELP,   "Help");
    m_btn_revert = new wxButton(this, ID_BTN_REVERT, "Revert");
    m_btn_ok     = new wxButton(this, wxID_OK,       "OK");
    m_btn_cancel = new wxButton(this, wxID_CANCEL,   "Cancel");

    wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->Add(m_btn_help,   0, wxALL, 10);
    button_sizer->Add(m_btn_revert, 0, wxALL, 10);
    button_sizer->Add(m_btn_ok,     0, wxALL, 10);
    button_sizer->Add(m_btn_cancel, 0, wxALL, 10);
#ifdef __WXMAC__
    // the cancel button was running into the window resizing grip
    button_sizer->AddSpacer(10);
#endif
    m_btn_revert->Disable();      // until something changes

    // all of it is stacked vertically
    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(m_rb_num_drives,   0, wxALIGN_LEFT | wxALL, 5);
    top_sizer->Add(m_rb_intelligence, 0, wxALIGN_LEFT | wxALL, 5);
    top_sizer->Add(m_warn_mismatch,   0, wxALIGN_LEFT | wxALL, 5);
    top_sizer->AddStretchSpacer();
    top_sizer->Add(button_sizer,      0, wxALIGN_RIGHT | wxALL, 5);

    updateDlg();                        // select current options

    // tell the thing to get to work
    SetSizer(top_sizer);                 // use the sizer for layout
    top_sizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();  // get default size & location

    // event routing table
    Bind(wxEVT_RADIOBOX, &DiskCtrlCfgDlg::OnNumDrives,    this, ID_RB_NUM_DRIVES);
    Bind(wxEVT_RADIOBOX, &DiskCtrlCfgDlg::OnIntelligence, this, ID_RB_INTELLIGENCE);
    Bind(wxEVT_CHECKBOX, &DiskCtrlCfgDlg::OnWarnMismatch, this, ID_CHK_WARN_MISMATCH);
    Bind(wxEVT_BUTTON,   &DiskCtrlCfgDlg::OnButton,       this, -1);
}


// update the display to reflect the current state
void
DiskCtrlCfgDlg::updateDlg()
{
    m_rb_num_drives->SetSelection(m_cfg.getNumDrives()-1);
    switch (m_cfg.getIntelligence()) {
        case DiskCtrlCfgState::DISK_CTRL_DUMB:
                m_rb_intelligence->SetSelection(0); break;
        case DiskCtrlCfgState::DISK_CTRL_INTELLIGENT:
                m_rb_intelligence->SetSelection(1); break;
#if SUPPORT_AUTO_INTELLIGENCE
        case DiskCtrlCfgState::DISK_CTRL_AUTO:
                m_rb_intelligence->SetSelection(2); break;
#endif
        default: assert(false);
    }
    m_warn_mismatch->SetValue(m_cfg.getWarnMismatch());
}


void
DiskCtrlCfgDlg::OnNumDrives(wxCommandEvent& WXUNUSED(event))
{
    switch (m_rb_num_drives->GetSelection()) {
        case 0: m_cfg.setNumDrives(1); break;
        case 1: m_cfg.setNumDrives(2); break;
        case 2: m_cfg.setNumDrives(3); break;
        case 3: m_cfg.setNumDrives(4); break;
        default: assert(false); break;
    }
    m_btn_revert->Enable(m_cfg != m_old_cfg);
}


void
DiskCtrlCfgDlg::OnIntelligence(wxCommandEvent& WXUNUSED(event))
{
    switch (m_rb_intelligence->GetSelection()) {
        case 0: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_DUMB); break;
        case 1: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_INTELLIGENT); break;
#if SUPPORT_AUTO_INTELLIGENCE
        case 2: m_cfg.setIntelligence(DiskCtrlCfgState::DISK_CTRL_AUTO); break;
#endif
        default: assert(false); break;
    }
    m_btn_revert->Enable(m_cfg != m_old_cfg);
}


void
DiskCtrlCfgDlg::OnWarnMismatch(wxCommandEvent& WXUNUSED(event))
{
    const bool checked = m_warn_mismatch->IsChecked();
    m_cfg.setWarnMismatch(checked);
    m_btn_revert->Enable(m_cfg != m_old_cfg);
}


// used for all dialog button presses
void
DiskCtrlCfgDlg::OnButton(wxCommandEvent &event)
{
    switch (event.GetId()) {

        case ID_BTN_HELP:
            {
                DiskCtrlCfgHelpDlg *helpDlg = new DiskCtrlCfgHelpDlg(this);
                helpDlg->ShowModal();
            }
            break;

        case ID_BTN_REVERT:
            m_cfg = m_old_cfg;      // revert state
            updateDlg();            // select current options
            m_btn_revert->Disable();
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
    const std::string subgroup("ui/diskcfgdlg");

    // save position and size
    host::configWriteWinGeom(this, subgroup);
}


void
DiskCtrlCfgDlg::getDefaults()
{
    // see if we've established a favored location and size
    const std::string subgroup("ui/diskcfgdlg");
    host::configReadWinGeom(this, subgroup);
}

// vim: ts=8:et:sw=4:smarttab
