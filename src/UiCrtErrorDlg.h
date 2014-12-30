// If the user double clicks on a line on the screen where a standard
// BASIC error code is displayed, this class is used to open a dialog to
// display the full error message.

#ifndef _INCLUDE_UI_CRT_ERROR_DLG_H_
#define _INCLUDE_UI_CRT_ERROR_DLG_H_

// modal dialog box for configuring the emulated computer
class CrtErrorDlg : public wxDialog
{
public:
    CrtErrorDlg(wxWindow *parent,
                const wxString &errcode,
                const wxPoint origin );

private:
    // ---- event handlers ----
    void OnClick(wxMouseEvent& WXUNUSED(event));

    CANT_ASSIGN_OR_COPY_CLASS(CrtErrorDlg);
    DECLARE_EVENT_TABLE()
};

#endif _INCLUDE_UI_CRT_ERROR_DLG_H_
