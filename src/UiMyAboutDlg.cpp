// I found wxAboutBox to be a bit ugly, so I customized it.  The code in this
// class was lifted and adapted from <wxdir>/src/generic/aboutdlgg.cpp

#include "UiSystem.h"
#include "UiMyStaticText.h"
#include "UiMyAboutDlg.h"

// the emulator needs only the "base" and "core" wx libraries if USE_HYPERLINK
// is 0, but then the About box link to www.wang2200.org below is not live.
// If USE_HYPERLINK is 1, then the URL is live, but the wx adv library is
// required to link as well.

#define USE_HYPERLINK 1

#if USE_HYPERLINK
    #include "wx/hyperlink.h"
#endif

// ============================================================================
// implementation
// ============================================================================

BEGIN_EVENT_TABLE(MyAboutDlg, wxDialog)
    EVT_LEFT_DOWN   ( MyAboutDlg::OnClick )
    EVT_MIDDLE_DOWN ( MyAboutDlg::OnClick )
    EVT_RIGHT_DOWN  ( MyAboutDlg::OnClick )
END_EVENT_TABLE()


MyAboutDlg::MyAboutDlg(wxWindow *parent) :
        wxDialog(parent, -1, "About Wang 2200 Emulator",
                   wxDefaultPosition, wxDefaultSize,
//                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
//                 wxBORDER_SIMPLE
                   wxDEFAULT_DIALOG_STYLE
                 )
{
    const int side_margin = 20;
    const int left_indent = 50;

    // the hierarchy is:
    //    topsizer(V)
    //       + hsizer(H)
    //       |    + icon
    //       |    + vsizer(V)
    //       |        + version
    //       |        + copyright
    //       + blah
    //       + thanks
    //       + blah2
    //       + comevisit
    //       + url
    wxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    topsizer->AddSpacer(5);

    wxSizer *hsizer = new wxBoxSizer(wxHORIZONTAL);

    wxSizer *vsizer = new wxBoxSizer(wxVERTICAL);

    #include "wang_icon48.xpm"
    wxBitmap icon(wang_icon48_xpm);
    hsizer->Add(new wxStaticBitmap(this, wxID_ANY, icon), 0,
                wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, side_margin);

    MyStaticText *version = new MyStaticText(this, wxID_ANY,
        "Wang 2200 Emulator\n"
        "Version 2.2-pre; January 5, 2016");
    wxFont boldfont(*wxNORMAL_FONT);
    boldfont.SetPointSize(boldfont.GetPointSize() + 2);
    boldfont.SetWeight(wxFONTWEIGHT_BOLD);
    version->SetFont(boldfont);
    vsizer->Add(version, 0, wxEXPAND | wxRIGHT, side_margin);

    vsizer->AddSpacer(8);

    MyStaticText *copyright = new MyStaticText(this, wxID_ANY,
        "(c) 2002-2015 Jim Battle, Slash && Burn Software\n"
        "      wxMac port and printer support by Paul Heller");
    vsizer->Add(copyright, 0, wxRIGHT, side_margin);

    hsizer->Add(vsizer, 0);

    topsizer->Add(hsizer);
    topsizer->AddSpacer(12);

    MyStaticText *blah = new MyStaticText(this, wxID_ANY,
        "This software was developed as a hobby activity solely for the fun\n"
        "of learning more about the 2200 and to help others appreciate what\n"
        "the 2200 was like.\n"
        "\n"
        "The authors make no guarantee about the quality of this software.\n"
        "Use it at your own risk.\n"
        "\n"
        "Thanks for crucial information and encouragement:");
    topsizer->Add(blah, 0, wxLEFT | wxRIGHT, side_margin);

    topsizer->AddSpacer(8);

    MyStaticText *thanks = new MyStaticText(this, wxID_ANY,
        "Max Blomme\n"
        "Eilert Brinkmeyer\n"
        "Carl Coffman\n"
#ifdef __WXMSW__
        "Georg Schäfer\n"
#else // WXMAC
        "Georg SchŠfer\n"
#endif
        "Paul Szudzik\n"
        "Smokey Thompson\n"
        "Jan Van de Veen"
        );
    topsizer->Add(thanks, 0, wxLEFT | wxRIGHT, left_indent);

    topsizer->AddSpacer(12);

    wxString msg;
    msg.Printf("Built with %s", wxVERSION_STRING);
    MyStaticText *blah2 = new MyStaticText(this, wxID_ANY, msg);
    topsizer->Add(blah2, 0, wxLEFT, side_margin);

    topsizer->AddSpacer(8);

    MyStaticText *comevisit = new MyStaticText(this, wxID_ANY,
        "Visit this website to get news, manuals, and updates:");
    topsizer->Add(comevisit, 0, wxLEFT | wxRIGHT, side_margin);

    topsizer->AddSpacer(5);

#if USE_HYPERLINK
    wxHyperlinkCtrl *url = new wxHyperlinkCtrl(this, wxID_ANY,
            "http://www.wang2200.org",          // label
            "http://www.wang2200.org");         // url
#else
    MyStaticText *url = new MyStaticText(this, wxID_ANY,
            "http://www.wang2200.org");         // url
#endif
    wxFont urlFont(url->GetFont());
    urlFont.SetPointSize(urlFont.GetPointSize() + 2);
    urlFont.SetFamily(wxFONTFAMILY_SWISS);
    urlFont.SetWeight(wxFONTWEIGHT_BOLD);
    url->SetFont(urlFont);
    topsizer->Add(url, 0, wxALIGN_CENTER);

    topsizer->AddSpacer(16);

    SetSizerAndFit(topsizer);
    CenterOnScreen();
}


// any mouse button click on dialog (except the URL control) will close it
void
MyAboutDlg::OnClick(wxMouseEvent& WXUNUSED(event))
{
    EndModal(0);
}

// vim: ts=8:et:sw=4:smarttab
