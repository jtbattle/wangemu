// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiCrtFrame.h"
#include "UiCrtConfigDlg.h"
#include "host.h"               // for Config*()

#include "wx/slider.h"

enum
{
    ID_FONT_CHOICE = 1,
    ID_COLOR_CHOICE,
    ID_CONTRAST_SLIDER,
    ID_BRIGHTNESS_SLIDER,
};


// dialog events to catch
BEGIN_EVENT_TABLE(CrtConfigDlg, wxDialog)
    EVT_CHOICE(ID_FONT_CHOICE,          CrtConfigDlg::OnFontChoice)
    EVT_CHOICE(ID_COLOR_CHOICE,         CrtConfigDlg::OnColorChoice)
    EVT_CHOICE(ID_COLOR_CHOICE,         CrtConfigDlg::OnColorChoice)
    EVT_COMMAND_SCROLL_THUMBTRACK(ID_CONTRAST_SLIDER,    CrtConfigDlg::OnContrastSlider)
    EVT_COMMAND_SCROLL_THUMBTRACK(ID_BRIGHTNESS_SLIDER,  CrtConfigDlg::OnBrightnessSlider)
END_EVENT_TABLE()


// Layout:
//      topsizer (V)
//      |
//      +-- m_FontChoice
//      +-- m_ColorChoice
CrtConfigDlg::CrtConfigDlg(wxFrame *parent, const wxString &title,
                                            const wxString &subgroup) :
        wxDialog(parent, -1, title,
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_FontChoice(nullptr),
        m_ColorChoice(nullptr),
        m_ContrastSlider(nullptr),
        m_BrightnessSlider(nullptr),
        m_subgroup(subgroup)
{
    const int h_text_margin = 8;
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    pp = pp;    // MSVC++ 6.0 complains that pp isn't referenced
                // because the accesses below are static functions

    m_FontChoice = new wxChoice(this, ID_FONT_CHOICE);
    // FIXME: the encodings should come from the enum
    for (int i=0; i<pp->getNumFonts(); i++) {
        int         size  = pp->getFontNumber(i);
        std::string label = pp->getFontName(i);
        m_FontChoice->Append(label, (void*)size);
    }

    m_ColorChoice = new wxChoice(this, ID_COLOR_CHOICE);
    for (int j=0; j<pp->getNumColorSchemes(); j++) {
        std::string label = pp->getColorSchemeName(j);
        m_ColorChoice->Append(label, (void*)j);
    }

    m_ContrastSlider = new wxSlider(this, ID_CONTRAST_SLIDER,
                                100,            // initial value
                                0, 200);        // min, max

    m_BrightnessSlider = new wxSlider(this, ID_BRIGHTNESS_SLIDER,
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

    hGrid->AddSpacer(5); hGrid->AddSpacer(5);

    hGrid->Add(new wxStaticText(this, -1, "CRT Font"), 0, label_flags, h_text_margin);
    hGrid->Add(m_FontChoice, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "CRT Color"), 0, label_flags, h_text_margin);
    hGrid->Add(m_ColorChoice, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "Contrast"), 0, label_flags, h_text_margin);
    hGrid->Add(m_ContrastSlider, 1, ctl_flags, h_text_margin);

    hGrid->Add(new wxStaticText(this, -1, "Brightness"), 0, label_flags, h_text_margin);
    hGrid->Add(m_BrightnessSlider, 1, ctl_flags, h_text_margin);

    hGrid->AddSpacer(5); hGrid->AddSpacer(5);

    // config grids on top, confirmation buttons on the bottom
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(hGrid, 1, wxEXPAND);          // vertically stretchable

    updateDlg();        // select current options

    // tell the thing to get to work
    SetSizerAndFit(topsizer);           // use the sizer for layout
    topsizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();              // get default size & location
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
    pp->setFontSize(size);
}

void
CrtConfigDlg::OnColorChoice(wxCommandEvent& WXUNUSED(event))
{
    const int selection = m_ColorChoice->GetSelection();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    pp->setDisplayColorScheme(selection);
}

void
CrtConfigDlg::OnContrastSlider(wxScrollEvent &event)
{
    const int pos = event.GetPosition();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    pp->setDisplayContrast(pos);
}

void
CrtConfigDlg::OnBrightnessSlider(wxScrollEvent &event)
{
    const int pos = event.GetPosition();
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);
    pp->setDisplayBrightness(pos);
}

// update the display to reflect the current state
void
CrtConfigDlg::updateDlg()
{
    CrtFrame *pp = wxStaticCast(GetParent(), CrtFrame);

    // figure out mapping of font size to index
    const int font_size = pp->getFontSize();
    int font_choice = -1;
    for (unsigned int i=0; i<m_FontChoice->GetCount(); i++) {
        if (reinterpret_cast<int>(m_FontChoice->GetClientData(i)) == font_size) {
            font_choice = i;
        }
    }
    m_FontChoice->SetSelection(font_choice);

    const int color_choice = pp->getDisplayColorScheme();
    m_ColorChoice->SetSelection(color_choice);

    const int contrast = pp->getDisplayContrast();
    m_ContrastSlider->SetValue(contrast);

    const int brightness = pp->getDisplayBrightness();
    m_BrightnessSlider->SetValue(brightness);
}


// save dialog options to the config file
void
CrtConfigDlg::saveDefaults()
{
    const std::string subgroup = m_subgroup + "/cfgscreendlg";
    // save position and size
    host::ConfigWriteWinGeom(this, subgroup);
}


void
CrtConfigDlg::getDefaults()
{
    const std::string subgroup = m_subgroup + "/cfgscreendlg";
    // see if we've established a favored location and size
    host::ConfigReadWinGeom(this, subgroup);
}

// vim: ts=8:et:sw=4:smarttab
