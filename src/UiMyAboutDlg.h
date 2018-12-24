// this class is just a hand-rolled rip off of wxAboutBox.
// it presents a dialog with information about the emulator.

#ifndef _INCLUDE_UI_MYABOUTDLG_H_
#define _INCLUDE_UI_MYABOUTDLG_H_

#include "w2200.h"
#include "wx/wx.h"

class MyAboutDlg : public wxDialog
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(MyAboutDlg);
    explicit MyAboutDlg(wxWindow *parent);

private:
    // ---- event handlers ----
    void OnClick(wxMouseEvent& WXUNUSED(event));
};

#endif // _INCLUDE_UI_MYABOUTDLG_H_

// vim: ts=8:et:sw=4:smarttab
