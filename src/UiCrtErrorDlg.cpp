// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Cpu2200.h"
#include "system2200.h"
#include "SysCfgState.h"
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiMyStaticText.h"     // sharing info between UI_wxgui modules
#include "UiCrtErrorDlg.h"

// ----------------------------------------------------------------------------
// error message class
// ----------------------------------------------------------------------------

#include "errtable.h"   // error explanations

CrtErrorDlg::CrtErrorDlg(wxWindow *parent,
                         const wxString &errcode,
                         const wxPoint origin)
        : wxDialog(parent, -1, "ERR " + errcode,
                   origin, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // this doesn't seem to have any effect on wxMAC.
    // I also messed around with SetBackgroundStyle, ClearBackground, Refresh,
    // but none had the desired effect.
#if 1
    wxColor bg_color("pale green");
#else
    // same thing as bg_color("pale green"); at least on windows
    wxColor bg_color(0x8F, 0xBC, 0x8F);
#endif
    SetBackgroundColour(bg_color);

    // determine which entry matches
    const int cpu_type = system2200::config().getCpuType();
    const bool vp_mode = (cpu_type != Cpu2200::CPUTYPE_2200B)
                      && (cpu_type != Cpu2200::CPUTYPE_2200T);
    const std::vector<error_table_t> &pet = (vp_mode) ? error_table_vp
                                                      : error_table;

    bool found = false;
    auto err = pet.cbegin();
    while (err != pet.cend()) {
        if (strcmp(err->errcode, errcode) == 0) {
            found = true;
            break;
        }
        err++;
    }

    // this is the font used for the example and correction code text
    wxFont fixedfont = wxFont(10,                   // point size
                              wxFONTFAMILY_MODERN,
                              wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_BOLD);

    // the FlexGridSizer is wrapped by a BoxSizer so we can put a margin
    // all the way around it so the text isn't flush with the window edges
    const int  edge_margin = 10; // margin around entire window, in pixels

    const int  v_margin = 10;           // vertical space between grid cells
    const int  h_margin = 10;           // horizontal space between grid cells
    const long L_style  = wxALIGN_TOP;  // top aligned, margin on left
    const long R_style  = wxALIGN_TOP;  // top aligned, margin on right

    wxFlexGridSizer *grid = new wxFlexGridSizer(0, 2, v_margin, h_margin);

    if (!found) {
        std::string msg("Unknown error code");
        grid->Add(new MyStaticText(this, -1, "Error"),    0, L_style);
        grid->Add(new MyStaticText(this, -1, msg),        0, R_style);
    } else {
        grid->Add(new MyStaticText(this, -1, "Error"),    0, L_style);
        grid->Add(new MyStaticText(this, -1, err->error), 0, R_style);

        if (err->cause != nullptr) {
            grid->Add(new MyStaticText(this, -1, "Cause"),    0, L_style);
            grid->Add(new MyStaticText(this, -1, err->cause), 0, R_style);
        }

        if (err->action != nullptr) {
            std::string txt = (vp_mode) ? "Recovery" : "Action";
            grid->Add(new MyStaticText(this, -1, txt),         0, L_style);
            grid->Add(new MyStaticText(this, -1, err->action), 0, R_style);
        }

        if (err->example != nullptr) {
            grid->Add(new MyStaticText(this, -1, "Example"), 0, L_style);
            MyStaticText *X_text = new MyStaticText(this, -1, err->example);
            X_text->SetFont(fixedfont);
            grid->Add(X_text, 0, R_style);
        }

        if (err->fix != nullptr) {
            grid->Add(new MyStaticText(this, -1, "Possible\nCorrection"), 0, L_style);
            MyStaticText *PC_text = new MyStaticText(this, -1, err->fix);
            PC_text->SetFont(fixedfont);
            grid->Add(PC_text, 0, R_style);
        }
    }

    // need to wrap it all in a sizer to make the dlg stretchable
    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);
//  top_sizer->AddStretchSpacer(1);
    top_sizer->Add(grid, 0, wxALL, edge_margin);
//  top_sizer->AddStretchSpacer(1);
    top_sizer->SetSizeHints(this);       // honor minimum size
    SetSizer(top_sizer);
    SetAutoLayout(true);

    // make sure it is on the screen
    wxRect dlg_rect = this->GetRect();
    const int screen_w = wxSystemSettings::GetMetric(wxSYS_SCREEN_X);
    const int screen_h = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
    // check bottom right
    if (dlg_rect.GetBottom() > screen_h-1) {
        dlg_rect.Offset(0, screen_h-1 - dlg_rect.GetBottom());
    }
    if (dlg_rect.GetRight() > screen_w-1) {
        dlg_rect.Offset(screen_w-1 - dlg_rect.GetRight(), 0);
    }
    Move(dlg_rect.GetX(), dlg_rect.GetY());

    // event routing table
    Bind(wxEVT_LEFT_DOWN,   &CrtErrorDlg::OnClick, this);
    Bind(wxEVT_MIDDLE_DOWN, &CrtErrorDlg::OnClick, this);
    Bind(wxEVT_RIGHT_DOWN,  &CrtErrorDlg::OnClick, this);
}


// any mouse button click on dialog
void
CrtErrorDlg::OnClick(wxMouseEvent& WXUNUSED(event))
{
    EndModal(0);
}

// vim: ts=8:et:sw=4:smarttab
