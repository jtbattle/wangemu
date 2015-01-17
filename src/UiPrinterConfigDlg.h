// This class is used for configuring printer settings.
// The validator class handles the data transfer to/from members
// of an instance of the PrinterDialogDataTransfer class.
//
// It uses the wxValidator class and default dialog capabilities.

#ifndef _INCLUDE_PRINTERCONFIGDLG_H_
#define _INCLUDE_PRINTERCONFIGDLG_H_

// ==================== exported by UiConfigPrinter.cpp ====================

#include "wx/dialog.h"
#include "wx/string.h"

// create a class for transfering data to and from the dialog
class PrinterDialogDataTransfer
{
public:
//    MyData();
    // These data members are designed for transfer to and from
    // controls, via validators.
    wxString m_string_pagelength;
    wxString m_string_linelength;
    bool     m_checkbox_autoshow;
    bool     m_checkbox_printasgo;
    bool     m_checkbox_portdirect;
    wxString m_choice_portstring;
};


// this is the acttual dialog for configuring specific printer settings
class PrinterConfigDlg : public wxDialog
{
public:
    PrinterConfigDlg(wxWindow *parent, const wxString& title,
                     PrinterDialogDataTransfer *data,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     const long style = wxDEFAULT_DIALOG_STYLE
                    );

    bool TransferDataToWindow();
    wxTextCtrl *text;

    CANT_ASSIGN_OR_COPY_CLASS(PrinterConfigDlg);
};

#endif _INCLUDE_PRINTERCONFIGDLG_H_
