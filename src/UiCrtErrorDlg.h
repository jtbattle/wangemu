// If the user double clicks on a line on the screen where a standard
// BASIC error code is displayed, this class is used to open a dialog to
// display the full error message.

#ifndef _INCLUDE_UI_CRT_ERROR_DLG_H_
#define _INCLUDE_UI_CRT_ERROR_DLG_H_

#include "w2200.h"
#include "wx/wx.h"

// modal dialog box for configuring the emulated computer
class CrtErrorDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(CrtErrorDlg);
    CrtErrorDlg(wxWindow *parent,
                const wxString &errcode,
                const wxPoint origin);

private:
    // ---- event handlers ----
    void OnClick(wxMouseEvent& WXUNUSED(event));
};

#endif // _INCLUDE_UI_CRT_ERROR_DLG_H_

// vim: ts=8:et:sw=4:smarttab
