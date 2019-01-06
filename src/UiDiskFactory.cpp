// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "IoCardDisk.h"         // disk access routines
#include "Ui.h"                 // emulator interface
#include "UiDiskFactory.h"
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "Wvd.h"
#include "host.h"               // for Config* functions
#include "system2200.h"         // disk access routines

#include <wx/notebook.h>        // wxNotebook

// define to 1 to have a static box drawn around the disk properties
// define to 0 to have a heading line over the disk properties
// no functional difference, just appearance tuning
#ifdef __WXMAC__
    #define STATICBOX_DISK_PROPS 1
#else
    #define STATICBOX_DISK_PROPS 0
#endif

// IDs for the controls and the menu commands
enum
{
    ID_BTN_Cancel = 1,
    ID_BTN_Save,
    ID_BTN_SaveAs,
};


static struct disk_choice_t {
    std::string description;
    int    disk_type;
    int    platters;
    int    sectors_per_platter;
} disk_choices[] = {
                                                         // tracks*sectors
    { "PCS 5.25\" floppy disk (88 KB)", Wvd::DISKTYPE_FD5,   1,   35*10  },  // =   350
    { "2270 8\" floppy disk (260 KB)",  Wvd::DISKTYPE_FD8,   1,   64*16  },  // =  1024
    { "2270A 8\" floppy disk (308 KB)", Wvd::DISKTYPE_FD8,   1,   77*16  },  // =  1232

    { "2260 5 MB disk",                 Wvd::DISKTYPE_HD60,  1,  816*24  },  // = 19584
    { "2260 8 MB disk",                 Wvd::DISKTYPE_HD60,  1,   32767  },  // damn -- partial track.  32760 would have been best

    { "2280-1 13 MB * 1 platter disk",  Wvd::DISKTYPE_HD80,  1,  822*64  },  // = 52608
    { "2280-3 13 MB * 3 platter disk",  Wvd::DISKTYPE_HD80,  3,  822*64  },  // = 52608
    { "2280-5 13 MB * 5 platter disk",  Wvd::DISKTYPE_HD80,  5,  822*64  },  // = 52608

// these are real products described in the document "CS-2200 Ramblings.pdf", page 21:
//  { "DS-20 10 MB * 2 platter disk",   Wvd::DISKTYPE_HD80,  2,  640*64  },  // = 40960
//  { "DS-32 16 MB * 2 platter disk",   Wvd::DISKTYPE_HD80,  2, 1023*64  },  // = 65472 (65536 is too big)
//  { "DS-64 16 MB * 4 platter disk",   Wvd::DISKTYPE_HD80,  4, 1023*64  },  // = 65472
    { "DS-112 16 MB * 7 platter disk",  Wvd::DISKTYPE_HD80,  7, 1023*64  },  // = 65472
    { "DS-140 10 MB * 14 platter disk", Wvd::DISKTYPE_HD80, 14,  640*64  },  // = 40960
// this is one I just made up -- it is the largest possible disk:
    { "DS-224 16 MB * 14 platter disk", Wvd::DISKTYPE_HD80, 14, 1023*64  },  // = 65472
};
static const int num_disk_types = sizeof(disk_choices) / sizeof(disk_choice_t);

// ------------------------------------------------------------------------
//  Tab 1 -- disk properties
// ------------------------------------------------------------------------

class PropPanel : public wxPanel
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(PropPanel);
    PropPanel(DiskFactory *df, wxWindow *parent,
              std::shared_ptr<Wvd> diskdata);
//    ~PropPanel();

    void refresh();

private:

    // callbacks
    void OnDiskTypeButton(wxCommandEvent& WXUNUSED(event));
    void OnWriteProt(wxCommandEvent& WXUNUSED(event));

    // controls
    DiskFactory  *m_parent      = nullptr;  // owning dialog
    wxStaticText *m_path        = nullptr;  // path to virtual disk file
    wxRadioBox   *m_disktype    = nullptr;  // which type of disk (5.25", 8", hard disk)
    wxStaticText *m_st_type     = nullptr;  // type of drive
    wxStaticText *m_st_plats    = nullptr;  // # platters
    wxStaticText *m_st_tracks   = nullptr;  // # tracks/platter
    wxStaticText *m_st_secttrk  = nullptr;  // display of sectors/track
    wxStaticText *m_st_sect     = nullptr;  // display of # of sectors per platter
    wxStaticText *m_st_steptime = nullptr;  // display of time to step between tracks
    wxStaticText *m_st_rpm      = nullptr;  // display of rotational speed
    wxCheckBox   *m_write_prot  = nullptr;  // write protect checkbox

    // data
    std::shared_ptr<Wvd> m_disk_data;
};


// ------------------------------------------------------------------------
//  Tab 2 -- label edit
// ------------------------------------------------------------------------

class LabelPanel : public wxPanel
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(LabelPanel);
    LabelPanel(DiskFactory *df, wxWindow *parent,
               std::shared_ptr<Wvd> diskdata);
//    ~LabelPanel();

    void refresh();

    std::string getLabelString();

private:

    // event handlers
    void OnLabelEdit(wxCommandEvent &event);

    // controls
    DiskFactory * const m_parent;      // owning dialog
    wxTextCtrl  *m_text = nullptr;

    // data
    std::shared_ptr<Wvd> m_disk_data;
};


// ------------------------------------------------------------------------
//  back to DiskFactory
// ------------------------------------------------------------------------

DiskFactory::DiskFactory(wxFrame *parent, const std::string &filename) :
        wxDialog(parent, -1, "Disk Factory",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    const bool new_disk = filename.empty();

    m_disk_data = std::make_shared<Wvd>();
    if (new_disk) {
        // blank disk, default type
        m_disk_data->create(disk_choices[0].disk_type,
                            disk_choices[0].platters,
                            disk_choices[0].sectors_per_platter);
    } else {
        // existing disk
        const bool ok = m_disk_data->open(filename);
        assert(ok);
    }

    // the frame contains a panel containing a single notebook
    wxPanel    *panel     = new wxPanel(this, -1);
    wxNotebook *notebook  = new wxNotebook(panel, -1, wxDefaultPosition, wxSize(400, -1));
    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);

    // the top part of topvbox is the notebook control; it might be the only item
    top_sizer->Add(notebook, 1, wxEXPAND | wxALL, 4);    // add 4px padding all around

    // add tabs to the notebook
    m_tab1 = new PropPanel(this, notebook, m_disk_data);
    notebook->AddPage(m_tab1, "Properties", true);
    m_tab2 = new LabelPanel(this, notebook, m_disk_data);
    notebook->AddPage(m_tab2, "Label", true);
    notebook->SetSelection(0);

    // create buttons along the bottom, aligned to the right
    wxBoxSizer *m_botbox = new wxBoxSizer(wxHORIZONTAL);
    m_btn_cancel = new wxButton(panel, ID_BTN_Cancel, "Cancel");
    m_btn_save   = new wxButton(panel, new_disk ? ID_BTN_SaveAs : ID_BTN_Save,
                                       new_disk ? "Save As" : "Save");
    m_botbox->Add(1, 1, 1);          // 1 pixel stretchable spacer
    m_botbox->Add(m_btn_save, 0);    // non-stretchable
    m_botbox->Add(15, 1, 0);         // 15 pixel non-stretchable spacer
    m_botbox->Add(m_btn_cancel, 0);  // non-stretchable
#ifdef __WXMAC__
    m_botbox->Add(20, 1, 0);         // make sure resizing grip doesn't overdraw cancel button
#endif

    // add the buttons to the bottom of the top vbox
    top_sizer->Add(m_botbox, 0, wxEXPAND | wxALL, 4);    // add 4px padding all around

    // make the dialog autosizing
    panel->SetSizer(top_sizer);
    top_sizer->Fit(this);
    top_sizer->SetSizeHints(this);
    const wxRect rc = GetRect();  // window size as declared by sizer

    // pick up screen location and size
    const std::string subgroup("ui/disk_dialog");
    wxRect default_geom(rc.GetX(), rc.GetY(), rc.GetWidth(), rc.GetHeight());
    host::configReadWinGeom(this, subgroup, &default_geom);

    updateDlg();

    // event routing table
    Bind(wxEVT_BUTTON,       &DiskFactory::OnButton_Cancel, this, ID_BTN_Cancel);
    Bind(wxEVT_BUTTON,       &DiskFactory::OnButton_Save,   this, ID_BTN_Save);
    Bind(wxEVT_BUTTON,       &DiskFactory::OnButton_SaveAs, this, ID_BTN_SaveAs);
    Bind(wxEVT_SIZE,         &DiskFactory::OnSize,          this);
    Bind(wxEVT_CLOSE_WINDOW, &DiskFactory::OnClose,         this);
}


DiskFactory::~DiskFactory()
{
    const std::string subgroup("ui/disk_dialog");
    host::configWriteWinGeom(this, subgroup);

    m_disk_data = nullptr;
}


// ------------------------------------------------------------------------
//
// ------------------------------------------------------------------------

// refresh the dialog buttons
void
DiskFactory::updateButtons()
{
    // disable the Save or SaveAs button if there have been no changes
    const bool new_disk = m_disk_data->getPath().empty();
    const bool modified = m_disk_data->isModified();
    if (m_btn_save != nullptr) {
        m_btn_save->Enable(new_disk || modified);
    }
}


// update the display to reflect the current state
void
DiskFactory::updateDlg()
{
    m_tab1->refresh();
    m_tab2->refresh();
}


void
DiskFactory::OnButton_Save(wxCommandEvent& WXUNUSED(event))
{
    std::string name(m_disk_data->getPath());
    assert(!name.empty());

    m_disk_data->setLabel(m_tab2->getLabelString());
    m_disk_data->save();
    Close();
}


void
DiskFactory::OnButton_SaveAs(wxCommandEvent& WXUNUSED(event))
{
    std::string name;
    if (host::fileReq(host::FILEREQ_DISK, "Virtual Disk Name", false, &name) !=
                      host::FILEREQ_OK) {
        return;
    }
    assert(!name.empty());

    // check if this disk is in a drive already
    int drive, io_addr;
    const bool in_use = system2200::findDisk(name, nullptr, &drive, &io_addr);
    if (in_use) {
        UI_warn("This disk is in use at /%03X, drive %d.\n\n"
                "Either save to a new name or eject the disk first.",
                 io_addr, drive);
        return;
    }

    m_disk_data->setLabel(m_tab2->getLabelString());
    m_disk_data->save(name);

    Close();
}


void
DiskFactory::OnButton_Cancel(wxCommandEvent& WXUNUSED(event))
{
    Close();
}


// called when the window size changes
void
DiskFactory::OnSize(wxSizeEvent &event)
{
    updateDlg();
    event.Skip();       // let the rest of the processing happen
}


// before closing, if the state has changed, confirm that it really is ok
// to close up shop
void
DiskFactory::OnClose(wxCloseEvent& WXUNUSED(event))
{
    bool ok = !m_disk_data->isModified();

    if (!ok) {
        ok = UI_confirm("You will lose unsaved changes.\n"
                        "Do you still want to cancel?");
    }

    if (ok) {
        Destroy();
    }
}


// ------------------------------------------------------------------------
//  Tab 1 -- disk properties
// ------------------------------------------------------------------------

enum
{
    PROP_CHK_WP = 1,
};

// wxBoxSizer(v) *top_sizer
//    +-- wxStaticText *m_path
//    +-- wxBoxSizer(h) *boxh
//    |    +-- wxRadioBox *m_disktype
//    |    +-- wxBoxSizer(v) *prop_sizer
//    |             +-- wxStaticText *st_heading
//    |             +-- wxStaticText *m_st_sect
//    |             +-- wxStaticText *m_st_secttrk
//    |             +-- wxStaticText *m_st_steptime
//    |             +-- wxStaticText *m_st_rpm
//    +-- wxCheckBox *m_write_prot

PropPanel::PropPanel(DiskFactory *df,
                     wxWindow *parent,
                     std::shared_ptr<Wvd> diskdata) :
        wxPanel(parent, -1),
        m_parent(df),
        m_disk_data(diskdata)
{
    const int margin_pixels = 6;
    const int margin_LRT = wxLEFT | wxRIGHT | wxTOP;

    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);

    // ----- row 1: path to virtual disk file, if it exists -----
    bool new_disk = (m_disk_data->getPath()).empty();
    if (!new_disk) {
        m_path = new wxStaticText(this, -1, "../path/to/file/...");
        top_sizer->Add(m_path, 0, wxEXPAND | wxALIGN_LEFT | margin_LRT, margin_pixels);
    }

    // ----- row 2: disk type box | disk properties -----
    wxBoxSizer *boxh = new wxBoxSizer(wxHORIZONTAL);

    if (new_disk) {
        wxString type_choices[num_disk_types];
        for (int i=0; i<num_disk_types; i++) {
            type_choices[i] = disk_choices[i].description;
        }
        m_disktype = new wxRadioBox(this, -1, "Disk Type",
                                    wxDefaultPosition, wxDefaultSize,
                                    num_disk_types,
                                    &type_choices[0],
                                    0, wxRA_SPECIFY_ROWS);
        boxh->Add(m_disktype, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | margin_LRT, margin_pixels);
    }

    {
        // make a heading for the properties information
        const int bottom_margin = 4;

#if !STATICBOX_DISK_PROPS
        wxStaticText *st_heading = new wxStaticText(this, -1, "Disk Properties");
        wxFont heading_font = st_heading->GetFont();
        heading_font.SetUnderlined(true);
        heading_font.SetWeight(wxFONTWEIGHT_BOLD);
        st_heading->SetFont(heading_font);
#endif

        // properties attribute strings
        m_st_type     = new wxStaticText(this, -1, wxEmptyString);
        m_st_plats    = new wxStaticText(this, -1, wxEmptyString);
        m_st_tracks   = new wxStaticText(this, -1, wxEmptyString);
        m_st_secttrk  = new wxStaticText(this, -1, wxEmptyString);
        m_st_sect     = new wxStaticText(this, -1, wxEmptyString);
        m_st_steptime = new wxStaticText(this, -1, wxEmptyString);
        m_st_rpm      = new wxStaticText(this, -1, wxEmptyString);

#if STATICBOX_DISK_PROPS
        wxStaticBox *stbox = new wxStaticBox(this, -1, "Disk Properties");
        wxBoxSizer *prop_sizer = new wxStaticBoxSizer(stbox, wxVERTICAL);
#else
        wxBoxSizer *prop_sizer = new wxBoxSizer(wxVERTICAL);
        prop_sizer->Add(  st_heading,  0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
#endif
        prop_sizer->Add(m_st_type,     0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_plats,    0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_tracks,   0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_secttrk,  0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_sect,     0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_steptime, 0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);
        prop_sizer->Add(m_st_rpm,      0, wxALIGN_LEFT | wxBOTTOM, bottom_margin);

        if (new_disk) {
            // we don't need to show what is shown in the selection box
            m_st_type->Hide();
        }

        const int flags = wxALIGN_LEFT | wxALIGN_TOP | wxRIGHT | wxTOP
                        | ((new_disk) ? wxLEFT : 0);
        boxh->Add(prop_sizer, 0, flags, margin_pixels);
    }
    top_sizer->Add(boxh, 0, wxALIGN_LEFT | margin_LRT, margin_pixels);

    // ----- spacer -----
    top_sizer->AddSpacer(6);

    // ----- row 3: write protect -----
    m_write_prot = new wxCheckBox(this, PROP_CHK_WP, "Write Protected");
    top_sizer->Add(m_write_prot, 0, wxALIGN_LEFT | wxALL, margin_pixels);

    SetSizerAndFit(top_sizer);
    refresh();

    // event routing table
    Bind(wxEVT_RADIOBOX, &PropPanel::OnDiskTypeButton, this, -1);
    Bind(wxEVT_CHECKBOX, &PropPanel::OnWriteProt,      this, PROP_CHK_WP);
}


void
PropPanel::refresh()
{
    assert(m_disk_data);

    const bool new_disk = (m_disk_data->getPath()).empty();
    const bool modified = m_disk_data->isModified();

    // update path to disk
    if (!new_disk) {
        std::string label = "Path: " + m_disk_data->getPath();
        m_path->SetLabel(label);
    }

    // based on the disk type, print some other information
    const int num_platters = m_disk_data->getNumPlatters();
    const int num_sectors  = m_disk_data->getNumSectors();
    const int disk_type    = m_disk_data->getDiskType();

    int spt, step, rpm;
    IoCardDisk::getDiskGeometry(disk_type, &spt, &step, &rpm, nullptr);

    // scan the disk_choices[] table to find a match
    if (new_disk) {
        int radio_sel = -1;
        for (int i=0; i < num_disk_types; i++) {
            if ((disk_type    == disk_choices[i].disk_type) &&
                (num_platters == disk_choices[i].platters)  &&
                (num_sectors  == disk_choices[i].sectors_per_platter)) {
                radio_sel = i;
                break;
            }
        }

        assert(radio_sel >= 0);
        m_disktype->SetSelection(radio_sel);
    }

    std::string type;
    switch (disk_type) {
        case Wvd::DISKTYPE_FD5:  type = "PCS 5.25\" floppy";  break;
        case Wvd::DISKTYPE_FD8:  type = "2270(A) 8\" floppy"; break;
        case Wvd::DISKTYPE_HD60: type = "2260 hard disk";     break;
        case Wvd::DISKTYPE_HD80: type = "2280 hard disk";     break;
        default: assert(false);
    }
    m_st_type->SetLabel(type);

    std::string label;
    label = std::to_string(num_platters) + " platter" + ((num_platters>1) ? "s" : "");
    m_st_plats->SetLabel(label);

    const int num_tracks = (num_sectors / spt);
    const std::string per_platter = (num_platters > 1) ? "/platter" : "";
    label = std::to_string(num_tracks) + " tracks" + per_platter;
    m_st_tracks->SetLabel(label);

    label = std::to_string(spt) + " sectors/track";
    m_st_secttrk->SetLabel(label);

    label = std::to_string(num_sectors) + " sectors" + per_platter;
    m_st_sect->SetLabel(label);

    label = std::to_string(step) + " ms step time";
    m_st_steptime->SetLabel(label);

    label = std::to_string(rpm) + " RPM";
    m_st_rpm->SetLabel(label);

    m_write_prot->SetValue(m_disk_data->getWriteProtect());

    // refreshing the # of sectors triggers the EVT_TEXT action,
    // which set the modified bit.  since we just did an update,
    // set it back to what it was before the update.
    m_disk_data->setModified(modified);

    m_parent->updateButtons();
}


// used for all dialog button presses
void
PropPanel::OnDiskTypeButton(wxCommandEvent& WXUNUSED(event))
{
    const int choice = m_disktype->GetSelection();
    assert(choice < num_disk_types);

    m_disk_data->setDiskType   (disk_choices[choice].disk_type);
    m_disk_data->setNumPlatters(disk_choices[choice].platters);
    m_disk_data->setNumSectors (disk_choices[choice].sectors_per_platter);

    refresh();
}


// used for all dialog button presses
void
PropPanel::OnWriteProt(wxCommandEvent& WXUNUSED(event))
{
    m_disk_data->setWriteProtect(m_write_prot->GetValue());
    refresh();
}


// ------------------------------------------------------------------------
//  Tab 2 -- label edit
// ------------------------------------------------------------------------

enum
{
    LABEL_EDIT_LABEL = 1,
};

LabelPanel::LabelPanel(DiskFactory *df,
                       wxWindow *parent,
                       std::shared_ptr<Wvd> diskdata) :
        wxPanel(parent, -1),
        m_parent(df),
        m_disk_data(diskdata)
{
    const int style = wxTE_MULTILINE;
    m_text = new wxTextCtrl(this, LABEL_EDIT_LABEL, "",
                            wxDefaultPosition, wxDefaultSize,
                            style);

    wxFont fontstyle = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);  // fixed pitch
    m_text->SetFont(fontstyle);
    m_text->SetMaxLength(WVD_MAX_LABEL_LEN-1);
    m_text->SetValue(m_disk_data->getLabel());
    m_disk_data->setModified(false);

    // put a sizer around it
    wxBoxSizer *m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_text, 1, wxEXPAND);
    SetSizerAndFit(m_sizer);

    // event routing table
    Bind(wxEVT_TEXT, &LabelPanel::OnLabelEdit, this, LABEL_EDIT_LABEL);
}


void
LabelPanel::refresh()
{
    const bool modified = m_disk_data->isModified();

    // refreshing the label triggers the EVT_TEXT action,
    // which set the modified bit.  since we just did an update,
    // set it back to what it was before the update.
    m_disk_data->setModified(modified);

    // I tried doing this in the constructor, but all the text was
    // always selected by default.  oh, well, better than nothing.
    const int last = m_text->GetLastPosition();
    m_text->SetSelection(last, last);

    m_parent->updateButtons();
}


std::string
LabelPanel::getLabelString()
{
    return std::string(m_text->GetValue());
}


// used for all dialog button presses
void
LabelPanel::OnLabelEdit(wxCommandEvent &event)
{
    switch (event.GetId()) {

        case LABEL_EDIT_LABEL:
            m_disk_data->setModified(true);      // note that it has changed
            m_parent->updateButtons();
            break;

        default:
            assert(false);
            break;
    }
}

// vim: ts=8:et:sw=4:smarttab
