// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "UiCrtConfigDlg.h"
#include "UiCrtFrame.h"
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "host.h"               // for Config*()

#include "wx/slider.h"

enum
{
    ID_FONT_CHOICE = 1,
    ID_COLOR_CHOICE,
    ID_CONTRAST_SLIDER,
    ID_BRIGHTNESS_SLIDER,
};


// Layout:
//      top_sizer (V)
//      |
//      +-- m_font_choice
//      +-- m_color_choice
CrtConfigDlg::CrtConfigDlg(wxFrame *parent, const wxString &title,
                                            const wxString &subgroup) :
        wxDialog(parent, -1, title,
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_font_choice(nullptr),
        m_color_choice(nullptr),
        m_contrast_slider(nullptr),
        m_brightness_slider(nullptr),
        m_subgroup(subgroup)
{
    const int h_text_margin = 8;

    m_font_choice = new wxChoice(this, ID_FONT_CHOICE);
    const int num_fonts = CrtFrame::getNumFonts();
    for (int i=0; i < num_fonts; i++) {
        int         size  = CrtFrame::getFontNumber(i);
        std::string label = CrtFrame::getFontName(i);
        m_font_choice->Append(label, (void*)size);
    }

    m_color_choice = new wxChoice(this, ID_COLOR_CHOICE);
    const int num_schemes = CrtFrame::getNumColorSchemes();
    for (int j=0; j < num_schemes; j++) {
        std::string label = CrtFrame::getColorSchemeName(j);
        m_color_choice->Append(label, (void*)j);
    }

    m_contrast_slider = new wxSlider(this, ID_CONTRAST_SLIDER,
                                100,            // initial value
                                0, 200);        // min, max

    m_brightness_slider = new wxSlider(this, ID_BRIGHTNESS_SLIDER,
                                0,              // initial value
                                0, 80);         // min, max

    // the grid on the left contains CPU related configuration
#if __WXMAC__
    // it seems like the only distinction wxMAC makes is zero vs. non-zero
    // making it 1 or 20 produces the same results.
    const int vgap = 4;
#else
    const int vgap = 4;
#endif
    wxFlexGridSizer *hGrid = new wxFlexGridSizer(0, 2, vgap, 0);

    const int label_flags = wxALIGN_RIGHT | wxLEFT | wxRIGHT; // right aligned text with left and right margin
    const int ctl_flags   = wxALIGN_LEFT  | wxRIGHT;          // left aligned control with right margin

    hGrid->AddSpacer(5);
    hGrid->AddSpacer(5);

    hGrid->Add(new wxStaticText(this, -1, "CRT Font"), 0, label_flags, h_text_margin);
    hGrid->Add(m_font_choice, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "CRT Color"), 0, label_flags, h_text_margin);
    hGrid->Add(m_color_choice, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "Contrast"), 0, label_flags, h_text_margin);
    hGrid->Add(m_contrast_slider, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "Brightness"), 0, label_flags, h_text_margin);
    hGrid->Add(m_brightness_slider, 1, ctl_flags, h_text_margin);

    hGrid->AddSpacer(5);
    hGrid->AddSpacer(5);

    // config grids on top, confirmation buttons on the bottom
    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(hGrid, 1, wxEXPAND);          // vertically stretchable

    updateDlg();        // select current options

    // tell the thing to get to work
    SetSizerAndFit(top_sizer);          // use the sizer for layout
    top_sizer->SetSizeHints(this);      // set size hints to honor minimum size

    getDefaults();              // get default size & location

    // event routing table
    Bind(wxEVT_CHOICE, &CrtConfigDlg::OnFontChoice,  this, ID_FONT_CHOICE);
    Bind(wxEVT_CHOICE, &CrtConfigDlg::OnColorChoice, this, ID_COLOR_CHOICE);
    Bind(wxEVT_SCROLL_THUMBTRACK, &CrtConfigDlg::OnContrastSlider,   this, ID_CONTRAST_SLIDER);
    Bind(wxEVT_SCROLL_THUMBTRACK, &CrtConfigDlg::OnBrightnessSlider, this, ID_BRIGHTNESS_SLIDER);
}


// save screen location on shut down
CrtConfigDlg::~CrtConfigDlg()
{
    saveDefaults();
}


void
CrtConfigDlg::OnFontChoice(wxCommandEvent &event)
{
    const int size = reinterpret_cast<int>(event.GetClientData());
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    assert(pp);
    pp->setFontSize(size);
}


void
CrtConfigDlg::OnColorChoice(wxCommandEvent& WXUNUSED(event))
{
    const int selection = m_color_choice->GetSelection();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    assert(pp);
    pp->setDisplayColorScheme(selection);
}


void
CrtConfigDlg::OnContrastSlider(wxScrollEvent &event)
{
    const int pos = event.GetPosition();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    assert(pp);
    pp->setDisplayContrast(pos);
}


void
CrtConfigDlg::OnBrightnessSlider(wxScrollEvent &event)
{
    const int pos = event.GetPosition();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    assert(pp);
    pp->setDisplayBrightness(pos);
}


// update the display to reflect the current state
void
CrtConfigDlg::updateDlg()
{
    const CrtFrame * const pp = wxStaticCast(GetParent(), CrtFrame);
    assert(pp);

    // figure out mapping of font size to index
    const int font_size = pp->getFontSize();
    int font_choice = -1;
    for (unsigned int i=0; i<m_font_choice->GetCount(); i++) {
        if (reinterpret_cast<int>(m_font_choice->GetClientData(i)) == font_size) {
            font_choice = i;
        }
    }
    m_font_choice->SetSelection(font_choice);

    const int color_choice = pp->getDisplayColorScheme();
    m_color_choice->SetSelection(color_choice);

    const int contrast = pp->getDisplayContrast();
    m_contrast_slider->SetValue(contrast);

    const int brightness = pp->getDisplayBrightness();
    m_brightness_slider->SetValue(brightness);
}


// save dialog options to the config file
void
CrtConfigDlg::saveDefaults()
{
    const std::string subgroup = m_subgroup + "/cfgscreendlg";
    // save position and size
    host::configWriteWinGeom(this, subgroup);
}


void
CrtConfigDlg::getDefaults()
{
    const std::string subgroup = m_subgroup + "/cfgscreendlg";
    // see if we've established a favored location and size
    host::configReadWinGeom(this, subgroup);
}

// vim: ts=8:et:sw=4:smarttab
