// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"
#include "UiPrinterConfigDlg.h"
#include "UiSystem.h"

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

PrinterConfigDlg::PrinterConfigDlg(wxWindow *parent, const wxString& title,
                                   PrinterDialogDataTransfer *data,
                                   const wxPoint& pos, const wxSize& size,
                                   const long WXUNUSED(style)) :
        wxDialog(parent, VALIDATE_DIALOG_ID, title, pos, size,
                 wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
    // Sizers automatically ensure a workable layout.
    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer *flexgrid_sizer = new wxFlexGridSizer(2, 2, 5, 5);

    // Create and add controls to sizers. Note that a member variable
    // of data is bound to each control upon construction. There is
    // currently no easy way to substitute a different validator or a
    // different transfer variable after a control has been constructed.

    // Pointers to the first text control is saved
    // so that we can use them elsewhere to set focus
    flexgrid_sizer->Add(new wxStaticText(this, wxID_ANY, "Page Length"));
    text = new wxTextCtrl(this, VALIDATE_TEXT_PAGELENGTH, "",
    wxPoint(10, 10), wxSize(120, -1), 0,
    wxTextValidator(wxFILTER_NUMERIC, &data->m_page_length));
    flexgrid_sizer->Add(text);
    flexgrid_sizer->Add(new wxStaticText(this, wxID_ANY, "Line Length"));
    flexgrid_sizer->Add(new wxTextCtrl(this, VALIDATE_TEXT_LINELENGTH, "",
    wxPoint(10, 10), wxSize(120, -1), 0,
    wxTextValidator(wxFILTER_NUMERIC, &data->m_line_length)));

    main_sizer->Add(flexgrid_sizer, 1, wxGROW | wxALL, 10);

    wxBoxSizer *check_sizer = new wxBoxSizer(wxVERTICAL);

    check_sizer->Add(new wxCheckBox(this, VALIDATE_CHECK_AUTOSHOW, "Auto show printer view",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_cb_auto_show)), 0, wxALL, 5);

    check_sizer->Add(new wxCheckBox(this, VALIDATE_CHECK_PRINTASGO, "Auto print full pages",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_cb_print_as_go)), 0, wxALL, 5);

#if !defined(__WXMAC__)
    wxBoxSizer *port_sizer = new wxBoxSizer(wxHORIZONTAL);
    port_sizer->Add(new wxCheckBox(this, VALIDATE_CHECK_PORTDIRECT, "Print directly to port",
        wxDefaultPosition, wxDefaultSize, 0,
        wxGenericValidator(&data->m_cb_port_direct)), 0, wxLEFT | wxTOP | wxRIGHT, 5);

    // leaf controls for leftgrid
    wxChoice *port_string = new wxChoice(this, VALIDATE_PORTSTRING_CHOICE,
                                         wxDefaultPosition, wxDefaultSize,
                                         0, nullptr, 0,
                                wxGenericValidator(&data->m_port_string));
    port_string->Append("LPT1", new wxStringClientData("LPT1"));
    port_string->Append("LPT2", new wxStringClientData("LPT2"));

    port_sizer->Add(port_string, 1, wxGROW | wxALL);
    check_sizer->Add(port_sizer, 1, wxGROW | wxALL);
#else
    data->m_cb_port_direct = false;
#endif
    main_sizer->Add(check_sizer, 1, wxGROW | wxALL);

    wxGridSizer *grid_sizer = new wxGridSizer(2, 2, 5, 5);

    wxButton *ok_button = new wxButton(this, wxID_OK, "OK", wxPoint(250, 70), wxSize(80, 30));
    ok_button->SetDefault();
    grid_sizer->Add(ok_button);
    grid_sizer->Add(new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(250, 100), wxSize(80, 30)));

    main_sizer->Add(grid_sizer, 0, wxGROW | wxALL, 10);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
}


bool
PrinterConfigDlg::TransferDataToWindow()
{
    const bool r = wxDialog::TransferDataToWindow();
    // set focus to the first text control
    text->SetFocus();
    return r;
}

// vim: ts=8:et:sw=4:smarttab
