// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "host.h"
#include "IoCard.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "TermMuxCfgState.h"
#include "UiTermMuxCfgDlg.h"
#include "system2200.h"

// ----------------------------------------------------------------------------
// a simple static dialog to provide help on the TermMuxCfgDlg options
// ----------------------------------------------------------------------------

class TermMuxCfgHelpDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(TermMuxCfgHelpDlg);
    explicit TermMuxCfgHelpDlg(wxWindow *parent);
    //DECLARE_EVENT_TABLE()
};


TermMuxCfgHelpDlg::TermMuxCfgHelpDlg(wxWindow *parent)
        : wxDialog(parent, -1, "Terminal Mux Controller Configuration Help",
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
    txt->AppendText("Number of Terminals\n");

    txt->SetDefaultStyle(body_attr);
    txt->AppendText(
        "\n"
        "Each 2236MXD controller supports from one to four terminals. "
        "Right now there is nothing else to configure, so there isn't "
        "much to explain."
        "\n\n"
        "The MXD can be used by Wang VP and Wang MVP OS's, though "
        "multiple terminals are supported by only the MVP OS's."
        "\n\n"
        "The MXD can be used in a 2200B or 2200T as it mimics a "
        "keyboard at I/O 001 and a CRT controller at I/O 005, though "
        "the character set won't be exactly the same as a dumb "
        "controller.  Also, because the link to the serial terminal "
        "runs at 19200 baud, throughput can sometimes lag as compared "
        "to a dumb CRT controller."
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
// TermMuxCfgDlg implementation
// ----------------------------------------------------------------------------

enum
{
    ID_RB_NUM_TERMINALS = 100,          // radio box
    ID_BTN_HELP   = 300,
    ID_BTN_REVERT
};


// Layout:
//      topsizer (V)
//      |
//      +-- num drives radiobox (H)
//      +-- button_sizer (H)
//          |
//          +-- m_btnHelp
//          +-- m_btnRevert
//          +-- m_btnOk
//          +-- m_btnCancel
TermMuxCfgDlg::TermMuxCfgDlg(wxFrame *parent, CardCfgState &cfg) :
        wxDialog(parent, -1, "Terminal Mux Controller Configuration",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_rbNumTerminals(nullptr),
        m_btnRevert(nullptr),
        m_btnOk(nullptr),
        m_btnCancel(nullptr),
        m_btnHelp(nullptr),
        m_oldcfg(dynamic_cast<TermMuxCfgState&>(cfg)),  // copy of original
        m_cfg(dynamic_cast<TermMuxCfgState&>(cfg))      // edited version
{
    const wxString choicesNumTerminals[] = { "1", "2", "3", "4" };
    m_rbNumTerminals = new wxRadioBox(this, ID_RB_NUM_TERMINALS,
                                      "Number of terminals",
                                      wxDefaultPosition, wxDefaultSize,
                                      4, &choicesNumTerminals[0],
                                      1, wxRA_SPECIFY_ROWS);

    // put three buttons side by side
    m_btnHelp   = new wxButton(this, ID_BTN_HELP,   "Help");
    m_btnRevert = new wxButton(this, ID_BTN_REVERT, "Revert");
    m_btnOk     = new wxButton(this, wxID_OK,       "OK");
    m_btnCancel = new wxButton(this, wxID_CANCEL,   "Cancel");

    wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
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
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(m_rbNumTerminals, 0, wxALIGN_LEFT | wxALL, 5);
    topsizer->Add(button_sizer,     0, wxALIGN_RIGHT | wxALL, 5);

    updateDlg();                        // select current options

    // tell the thing to get to work
    SetSizer(topsizer);                 // use the sizer for layout
    topsizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();  // get default size & location

    // event routing table
    Bind(wxEVT_RADIOBOX, &TermMuxCfgDlg::OnNumTerminals, this, ID_RB_NUM_TERMINALS);
    Bind(wxEVT_BUTTON,   &TermMuxCfgDlg::OnButton,       this, -1);
}


// update the display to reflect the current state
void
TermMuxCfgDlg::updateDlg()
{
    m_rbNumTerminals->SetSelection(m_cfg.getNumTerminals()-1);
}


void
TermMuxCfgDlg::OnNumTerminals(wxCommandEvent& WXUNUSED(event))
{
    switch (m_rbNumTerminals->GetSelection()) {
        case 0: m_cfg.setNumTerminals(1); break;
        case 1: m_cfg.setNumTerminals(2); break;
        case 2: m_cfg.setNumTerminals(3); break;
        case 3: m_cfg.setNumTerminals(4); break;
        default: assert(false); break;
    }
    m_btnRevert->Enable(m_cfg != m_oldcfg);
}


// used for all dialog button presses
void
TermMuxCfgDlg::OnButton(wxCommandEvent &event)
{
    switch (event.GetId()) {

        case ID_BTN_HELP:
            {
                TermMuxCfgHelpDlg *helpDlg = new TermMuxCfgHelpDlg(this);
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
TermMuxCfgDlg::saveDefaults()
{
// FIXME: we need to receive the MXD-nn-CRT-m prefix subgroup
    const std::string subgroup("ui/termmuxcfgdlg");

    // save position and size
    host::configWriteWinGeom(this, subgroup);
}


void
TermMuxCfgDlg::getDefaults()
{
    // see if we've established a favored location and size
// FIXME: we need to receive the MXD-nn-CRT-m prefix subgroup
    const std::string subgroup("ui/termmuxcfgdlg");
    host::configReadWinGeom(this, subgroup);
}

// vim: ts=8:et:sw=4:smarttab
