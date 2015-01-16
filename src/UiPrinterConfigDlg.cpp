// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"
#include "UiSystem.h"
#include "UiPrinterConfigDlg.h"

#include "wx/valgen.h"
#include "wx/valtext.h"

#define VALIDATE_DIALOG_ID              200

#define VALIDATE_TEXT_PAGELENGTH        101
#define VALIDATE_TEXT_LINELENGTH        102
#define VALIDATE_CHECK_AUTOSHOW         103
#define VALIDATE_CHECK_PRINTASGO        104
#define VALIDATE_CHECK_PORTDIRECT       105
#define VALIDATE_PORTSTRING_CHOICE      106


// ----------------------------------------------------------------------------
// PrinterConfigDlg
// ----------------------------------------------------------------------------

PrinterConfigDlg::PrinterConfigDlg( wxWindow *parent, const wxString& title,
                                    PrinterDialogDataTransfer *data,
                                    const wxPoint& pos, const wxSize& size,
                                    const long WXUNUSED(style)) :
        wxDialog(parent, VALIDATE_DIALOG_ID, title, pos, size,
                 wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
    // Sizers automatically ensure a workable layout.
    wxBoxSizer *mainsizer = new wxBoxSizer( wxVERTICAL );
    wxFlexGridSizer *flexgridsizer = new wxFlexGridSizer(2, 2, 5, 5);

    // Create and add controls to sizers. Note that a member variable
    // of data is bound to each control upon construction. There is
    // currently no easy way to substitute a different validator or a
    // different transfer variable after a control has been constructed.

    // Pointers to the first text control is saved 
    // so that we can use them elsewhere to set focus
    flexgridsizer->Add(new wxStaticText(this, wxID_ANY, "Page Length"));
    text = new wxTextCtrl(this, VALIDATE_TEXT_PAGELENGTH, "",
    wxPoint(10, 10), wxSize(120, -1), 0,
    wxTextValidator(wxFILTER_NUMERIC, &data->m_string_pagelength));
    flexgridsizer->Add(text);
    flexgridsizer->Add(new wxStaticText(this, wxID_ANY, "Line Length"));
    flexgridsizer->Add(new wxTextCtrl(this, VALIDATE_TEXT_LINELENGTH, "",
    wxPoint(10, 10), wxSize(120, -1), 0,
    wxTextValidator(wxFILTER_NUMERIC, &data->m_string_linelength)));

    mainsizer->Add(flexgridsizer, 1, wxGROW | wxALL, 10);

    wxBoxSizer *checksizer = new wxBoxSizer(wxVERTICAL);

    checksizer->Add(new wxCheckBox(this, VALIDATE_CHECK_AUTOSHOW, "Auto show printer view",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_checkbox_autoshow)), 0, wxALL, 5);

    checksizer->Add(new wxCheckBox(this, VALIDATE_CHECK_PRINTASGO, "Auto print full pages",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_checkbox_printasgo)), 0, wxALL, 5);

#if !defined(__WXMAC__)
    wxBoxSizer *portsizer = new wxBoxSizer(wxHORIZONTAL);
    portsizer->Add(new wxCheckBox(this, VALIDATE_CHECK_PORTDIRECT, "Print directly to port",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_checkbox_portdirect)), 0, wxLEFT | wxTOP | wxRIGHT, 5);

    // leaf controls for leftgrid
    wxChoice *portstring = new wxChoice(this, VALIDATE_PORTSTRING_CHOICE,
                                        wxDefaultPosition, wxDefaultSize,
                                        0, nullptr, 0,
                                wxGenericValidator(&data->m_choice_portstring));
    portstring->Append("LPT1", new wxStringClientData("LPT1"));
    portstring->Append("LPT2", new wxStringClientData("LPT2"));

    portsizer->Add(portstring, 1, wxGROW | wxALL);
    checksizer->Add(portsizer, 1, wxGROW | wxALL);
#endif
    mainsizer->Add(checksizer, 1, wxGROW | wxALL);  

    wxGridSizer *gridsizer = new wxGridSizer(2, 2, 5, 5);

    wxButton *ok_button = new wxButton(this, wxID_OK, "OK", wxPoint(250, 70), wxSize(80, 30));
    ok_button->SetDefault();
    gridsizer->Add(ok_button);
    gridsizer->Add(new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(250, 100), wxSize(80, 30)));

    mainsizer->Add(gridsizer, 0, wxGROW | wxALL, 10);

    SetSizer(mainsizer);
    mainsizer->SetSizeHints(this);
}

bool
PrinterConfigDlg::TransferDataToWindow()
{
    bool r = wxDialog::TransferDataToWindow();
    // set focus to the first text control
    text->SetFocus();
    return r;
}
