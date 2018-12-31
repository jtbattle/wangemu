// This class is used for configuring printer settings.
// The validator class handles the data transfer to/from members
// of an instance of the PrinterDialogDataTransfer class.
//
// It uses the wxValidator class and default dialog capabilities.

#ifndef _INCLUDE_PRINTERCONFIGDLG_H_
#define _INCLUDE_PRINTERCONFIGDLG_H_

// ==================== exported by UiConfigPrinter.cpp ====================

#include "w2200.h"
#include "wx/dialog.h"
#include "wx/string.h"

// create a class for transferring data to and from the dialog
class PrinterDialogDataTransfer
{
public:
    // These data members are designed for transfer to and from
    // controls, via validators.
    wxString m_page_length;
    wxString m_line_length;
    bool     m_cb_auto_show;
    bool     m_cb_print_as_go;
    bool     m_cb_port_direct;
    wxString m_port_string;
};


// this is the actual dialog for configuring specific printer settings
class PrinterConfigDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(PrinterConfigDlg);
    PrinterConfigDlg(wxWindow *parent, const wxString& title,
                     PrinterDialogDataTransfer *data,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     const long style = wxDEFAULT_DIALOG_STYLE
                    );

    bool TransferDataToWindow() override;
    wxTextCtrl *text;
};

#endif // _INCLUDE_PRINTERCONFIGDLG_H_

// vim: ts=8:et:sw=4:smarttab
