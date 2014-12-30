// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "Host.h"               // for Config* functions
#include "IoCardDisk.h"         // disk access routines
#include "System2200.h"         // disk access routines
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiDiskFactory.h"
#include "Wvd.h"

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
    char *description;
    int   disk_type;
    int   platters;
    int   sectors_per_platter;
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
//  { "DS-32 16 MB * 2 platter disk",   Wvd::DISKTYPE_HD80,  2, 1024*64  },  // = 65536
//  { "DS-64 16 MB * 4 platter disk",   Wvd::DISKTYPE_HD80,  4, 1024*64  },  // = 65536
    { "DS-112 16 MB * 7 platter disk",  Wvd::DISKTYPE_HD80,  7, 1024*64  },  // = 65536
    { "DS-140 10 MB * 14 platter disk", Wvd::DISKTYPE_HD80, 14,  640*64  },  // = 40960
// this is one I just made up -- it is the largest possible disk:
    { "DS-224 16 MB * 14 platter disk", Wvd::DISKTYPE_HD80, 14, 1024*64  },  // = 65536

};
static const int num_disk_types = sizeof(disk_choices) / sizeof(disk_choice_t);

// ------------------------------------------------------------------------
//  Tab 1 -- disk properties
// ------------------------------------------------------------------------

class PropPanel : public wxPanel
{
public:
    PropPanel(DiskFactory *df, wxWindow *parent, Wvd *diskdata);
//    ~PropPanel();

    void refresh();

private:

    // callbacks
    void OnDiskTypeButton(wxCommandEvent& WXUNUSED(event));
    void OnWriteProt(wxCommandEvent& WXUNUSED(event));

    // controls
    DiskFactory  *m_parent;     // owning dialog
    wxStaticText *m_path;       // path to virtual disk file
    wxRadioBox   *m_disktype;   // which type of disk (5.25", 8", hard disk)
    wxStaticText *m_st_type;    // type of drive
    wxStaticText *m_st_plats;   // # platters
    wxStaticText *m_st_tracks;  // # tracks/platter
    wxStaticText *m_st_secttrk; // display of sectors/track
    wxStaticText *m_st_sect;    // display of # of sectors per platter
    wxStaticText *m_st_steptime;  // display of time to step between tracks
    wxStaticText *m_st_rpm;     // display of rotational speed
    wxCheckBox   *m_write_prot; // write protect checkbox

    // data
    Wvd          *m_diskdata;

    CANT_ASSIGN_OR_COPY_CLASS(PropPanel);
    DECLARE_EVENT_TABLE()
};


// ------------------------------------------------------------------------
//  Tab 2 -- label edit
// ------------------------------------------------------------------------

class LabelPanel : public wxPanel
{
public:
    LabelPanel(DiskFactory *df, wxWindow *parent, Wvd *diskdata);
//    ~LabelPanel();

    void refresh();

    wxString LabelPanel::getLabelString();

private:

    // event handlers
    void OnLabelEdit(wxCommandEvent& event);

    // controls
    DiskFactory * const m_parent;      // owning dialog
    wxTextCtrl  *m_text;

    // data
    Wvd * const m_diskdata;

    CANT_ASSIGN_OR_COPY_CLASS(LabelPanel);
    DECLARE_EVENT_TABLE()
};


// ------------------------------------------------------------------------
//  back to DiskFactory
// ------------------------------------------------------------------------

// dialog events to catch
BEGIN_EVENT_TABLE(DiskFactory, wxDialog)
    EVT_BUTTON    (ID_BTN_Cancel, DiskFactory::OnButton_Cancel)
    EVT_BUTTON    (ID_BTN_Save,   DiskFactory::OnButton_Save)
    EVT_BUTTON    (ID_BTN_SaveAs, DiskFactory::OnButton_SaveAs)
    EVT_SIZE      (               DiskFactory::OnSize)
    EVT_CLOSE     (               DiskFactory::OnClose)
END_EVENT_TABLE()


DiskFactory::DiskFactory(wxFrame *parent, const string& filename) :
        wxDialog(parent, -1, "Disk Factory",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_menuBar(NULL),
        m_tab1(NULL),
        m_tab2(NULL),
        m_diskdata(NULL),
        m_butCancel(NULL),
        m_butSave(NULL)
{
    bool new_disk = filename.empty();
    m_butCancel = NULL;
    m_butSave   = NULL;

    m_diskdata = new Wvd;
    if (new_disk) {
        // blank disk, default type
        m_diskdata->create( disk_choices[0].disk_type,
                            disk_choices[0].platters,
                            disk_choices[0].sectors_per_platter );
    } else {
        // existing disk
        bool ok = m_diskdata->open(filename);
        ASSERT(ok); ok=ok;
    }

    // the frame contains a panel containing a single notebook
    wxPanel    *panel    = new wxPanel(this, -1);
    wxNotebook *notebook = new wxNotebook(panel, -1, wxDefaultPosition, wxSize(400,-1));
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    // the top part of topvbox is the notebook control; it might be the only item
    topsizer->Add(notebook, 1, wxEXPAND | wxALL, 4);    // add 4px padding all around

    // add tabs to the notebook
    m_tab1 = new PropPanel(this, notebook, m_diskdata);
    notebook->AddPage(m_tab1, "Properties", true);
    m_tab2 = new LabelPanel(this, notebook, m_diskdata);
    notebook->AddPage(m_tab2, "Label", true);
    notebook->SetSelection(0);

    // create buttons along the bottom, aligned to the right
    wxBoxSizer *m_botbox = new wxBoxSizer(wxHORIZONTAL);
    m_butCancel = new wxButton(panel, ID_BTN_Cancel, "Cancel");
    m_butSave   = new wxButton(panel, new_disk ? ID_BTN_SaveAs : ID_BTN_Save,
                                      new_disk ? "Save As" : "Save" );
    m_botbox->Add(1, 1, 1);             // 1 pixel stretchable spacer
    m_botbox->Add(m_butSave,   0 );     // non-stretchable
    m_botbox->Add(15, 1, 0);            // 15 pixel non-stretchable spacer
    m_botbox->Add(m_butCancel, 0 );     // non-stretchable
#ifdef __WXMAC__
    m_botbox->Add(20, 1, 0);            // make sure resizing grip doesn't overdraw cancel button
#endif

    // add the buttons to the bottom of the top vbox
    topsizer->Add(m_botbox, 0, wxEXPAND | wxALL, 4);    // add 4px padding all around

    // make the dialog autosizing
    panel->SetSizer(topsizer);
    topsizer->Fit(this);
    topsizer->SetSizeHints(this);
    wxRect rc = GetRect();  // window size as declared by sizer

    // pick up screen location and size
    const string subgroup("ui/disk_dialog");
    wxRect default_geom(rc.GetX(),rc.GetY(),rc.GetWidth(),rc.GetHeight());
    Host().ConfigReadWinGeom(this, subgroup, &default_geom);

    updateDlg();
}


DiskFactory::~DiskFactory()
{
    const string subgroup("ui/disk_dialog");
    Host().ConfigWriteWinGeom(this, subgroup);

    delete m_diskdata;
    m_diskdata = NULL;
}


// ------------------------------------------------------------------------
//
// ------------------------------------------------------------------------

// refresh the dialog buttons
void
DiskFactory::updateButtons()
{
    // disable the Save or SaveAs button if there have been no changes
    bool new_disk = m_diskdata->getPath().empty();
    bool modified = m_diskdata->isModified();
    if (m_butSave)
        m_butSave->Enable(new_disk || modified);
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
    string name( m_diskdata->getPath() );
    wxASSERT(!name.empty());

    string lab( m_tab2->getLabelString() );
    m_diskdata->setLabel(lab);

    m_diskdata->save();

    Close();
}


void
DiskFactory::OnButton_SaveAs(wxCommandEvent& WXUNUSED(event))
{
    string name;
    if (Host().fileReq(Host::FILEREQ_DISK, "Virtual Disk Name", 0, &name) !=
                       Host::FILEREQ_OK)
        return;
    wxASSERT(!name.empty());

    // check if this disk is in a drive already
    int drive, io_addr;
    bool in_use = System2200().findDisk(name, NULL, &drive, &io_addr);
    if (in_use) {
        UI_Warn("This disk is in use at /%03X, drive %d.\n\n"
                "Either save to a new name or eject the disk first.",
                 io_addr, drive);
        return;
    }

    string lab( m_tab2->getLabelString() );
    m_diskdata->setLabel(lab);

    m_diskdata->save(name);

    Close();
}


void
DiskFactory::OnButton_Cancel(wxCommandEvent& WXUNUSED(event))
{
    Close();
}


// called when the window size changes
void
DiskFactory::OnSize(wxSizeEvent& event)
{
    updateDlg();
    event.Skip();       // let the rest of the processing happen
}


// before closing, if the state has changed, confirm that it really is ok
// to close up shop
void
DiskFactory::OnClose(wxCloseEvent& WXUNUSED(event))
{
    bool ok = !m_diskdata->isModified();

    if (!ok) {
        ok = UI_Confirm("You will lose unsaved changes.\n"
                        "Do you still want to cancel?");
    }

    if (ok)
        Destroy();
}


// ------------------------------------------------------------------------
//  Tab 1 -- disk properties
// ------------------------------------------------------------------------

enum
{
    PROP_CHK_WP = 1,
};

// dialog events to catch
BEGIN_EVENT_TABLE(PropPanel, wxPanel)
    EVT_RADIOBOX (-1,                 PropPanel::OnDiskTypeButton)
    EVT_CHECKBOX (PROP_CHK_WP,        PropPanel::OnWriteProt)
END_EVENT_TABLE()


// wxBoxSizer(v) *topsizer
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

PropPanel::PropPanel(DiskFactory *df, wxWindow *parent, Wvd *diskdata) :
        wxPanel(parent, -1),
        m_parent(df),
        m_path(NULL),
        m_disktype(NULL),
        m_st_type(NULL),
        m_st_plats(NULL),
        m_st_tracks(NULL),
        m_st_secttrk(NULL),
        m_st_sect(NULL),
        m_st_steptime(NULL),
        m_st_rpm(NULL),
        m_write_prot(NULL),
        m_diskdata(diskdata)
{
    const int margin_pixels = 6;
    const int margin_LRT = wxLEFT | wxRIGHT | wxTOP;

    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    // ----- row 1: path to virtual disk file, if it exists -----
    bool new_disk = (m_diskdata->getPath()).empty();
    if (!new_disk) {
        m_path = new wxStaticText(this, -1, "../path/to/file/...");
        topsizer->Add(m_path, 0, wxEXPAND | wxALIGN_LEFT | margin_LRT, margin_pixels);
    }

    // ----- row 2: disk type box | disk properties -----
    wxBoxSizer *boxh = new wxBoxSizer(wxHORIZONTAL);

    if (new_disk) {
        wxString type_choices[num_disk_types];
        for(int i=0; i<num_disk_types; i++)
            type_choices[i] = disk_choices[i].description;
        m_disktype = new wxRadioBox( this, -1, "Disk Type",
                                     wxDefaultPosition, wxDefaultSize,
                                     num_disk_types,
                                     type_choices,
                                     0, wxRA_SPECIFY_ROWS
                                    );
        boxh->Add(m_disktype, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | margin_LRT, margin_pixels);
    }

    {
        // make a heading for the properties information
        int bottom_margin = 4;

#if !STATICBOX_DISK_PROPS
        wxStaticText *st_heading = new wxStaticText(this, -1, "Disk Properties");
        wxFont heading_font = st_heading->GetFont();
        heading_font.SetUnderlined(true);
        heading_font.SetWeight(wxBOLD);
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

        if (new_disk)
            m_st_type->Hide();
            // we don't need to show what is shown in the selection box

        int flags = wxALIGN_LEFT | wxALIGN_TOP | wxRIGHT | wxTOP |
                    ((new_disk) ? wxLEFT : 0);
        boxh->Add(prop_sizer, 0, flags, margin_pixels);
    }
    topsizer->Add(boxh, 0, wxALIGN_LEFT | margin_LRT, margin_pixels);

    // ----- spacer -----
    topsizer->AddSpacer(6);

    // ----- row 3: write protect -----
    m_write_prot = new wxCheckBox(this, PROP_CHK_WP, "Write Protected");
    topsizer->Add(m_write_prot, 0, wxALIGN_LEFT | wxALL, margin_pixels);

    SetSizerAndFit(topsizer);
    refresh();
}

void
PropPanel::refresh()
{
    wxASSERT(m_diskdata != NULL);

    bool new_disk = (m_diskdata->getPath()).empty();
    bool modified = m_diskdata->isModified();

    // update path to disk
    if (!new_disk) {
        wxString label;
        label.Printf("Path: %s", (m_diskdata->getPath()).c_str());
        m_path->SetLabel(label);
    }

    // based on the disk type, print some other information
    int radio_sel;
    int num_platters = m_diskdata->getNumPlatters();
    int num_sectors  = m_diskdata->getNumSectors();
    int disk_type    = m_diskdata->getDiskType();

    int spt, step, rpm;
    IoCardDisk::getDiskGeometry(disk_type, &spt, &step, &rpm, NULL);

    // scan the disk_choices[] table to find a match
    radio_sel = -1;
    if (new_disk) {
        for(int i=0; i < num_disk_types; i++) {
            if ( (disk_type    == disk_choices[i].disk_type) &&
                 (num_platters == disk_choices[i].platters)  &&
                 (num_sectors  == disk_choices[i].sectors_per_platter) 
               ) {
                radio_sel = i;
                break;
            }
        }

        wxASSERT(radio_sel >= 0);
        m_disktype->SetSelection(radio_sel);
    }

    wxString label, type;
    switch (disk_type) {
        case Wvd::DISKTYPE_FD5:  type = "PCS 5.25\" floppy";  break;
        case Wvd::DISKTYPE_FD8:  type = "2270(A) 8\" floppy"; break;
        case Wvd::DISKTYPE_HD60: type = "2260 hard disk";     break;
        case Wvd::DISKTYPE_HD80: type = "2280 hard disk";     break;
        default: ASSERT(0);
    }
    m_st_type->SetLabel(type);

    label.Printf("%d platter%s", num_platters, (num_platters>1) ? "s" : "");
    m_st_plats->SetLabel(label);
    const char *per_platter = (num_platters > 1) ? "/platter" : "";

    int num_tracks = int( num_sectors / spt );
    label.Printf("%d tracks%s", num_tracks, per_platter);
    m_st_tracks->SetLabel(label);

    label.Printf("%d sectors/track", spt);
    m_st_secttrk->SetLabel(label);

    label.Printf("%d sectors%s", num_sectors, per_platter);
    m_st_sect->SetLabel(label);

    label.Printf("%d ms step time", step);
    m_st_steptime->SetLabel(label);

    label.Printf("%d RPM", rpm);
    m_st_rpm->SetLabel(label);

    m_write_prot->SetValue(m_diskdata->getWriteProtect());

    // refreshing the # of sectors triggers the EVT_TEXT action,
    // which set the modified bit.  since we just did an update,
    // set it back to what it was before the update.
    m_diskdata->setModified(modified);

    m_parent->updateButtons();
}


// used for all dialog button presses
void
PropPanel::OnDiskTypeButton(wxCommandEvent& WXUNUSED(event))
{
    int choice = m_disktype->GetSelection();
    wxASSERT(choice < num_disk_types);

    m_diskdata->setDiskType   (disk_choices[choice].disk_type);
    m_diskdata->setNumPlatters(disk_choices[choice].platters);
    m_diskdata->setNumSectors (disk_choices[choice].sectors_per_platter);

    refresh();
}


// used for all dialog button presses
void
PropPanel::OnWriteProt(wxCommandEvent& WXUNUSED(event))
{
    m_diskdata->setWriteProtect(m_write_prot->GetValue());
    refresh();
}


// ------------------------------------------------------------------------
//  Tab 2 -- label edit
// ------------------------------------------------------------------------

enum
{
    LABEL_EDIT_LABEL = 1,
};

BEGIN_EVENT_TABLE(LabelPanel, wxPanel)
    EVT_TEXT     (LABEL_EDIT_LABEL,  LabelPanel::OnLabelEdit)
END_EVENT_TABLE()

LabelPanel::LabelPanel(DiskFactory *df, wxWindow *parent, Wvd *diskdata) :
        wxPanel(parent, -1),
        m_parent(df),
        m_text(NULL),
        m_diskdata(diskdata)
{
    const int style = wxTE_MULTILINE;
    m_text = new wxTextCtrl(this, LABEL_EDIT_LABEL, "",
                            wxDefaultPosition, wxDefaultSize,
                            style);

    wxFont fontstyle = wxFont(10, wxMODERN, wxNORMAL, wxNORMAL);  // fixed pitch
    m_text->SetFont(fontstyle);
    m_text->SetMaxLength(WVD_MAX_LABEL_LEN-1);
    m_text->SetValue(m_diskdata->getLabel());
    m_diskdata->setModified(false);

    // put a sizer around it
    wxBoxSizer *m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_text, 1, wxEXPAND);
    SetSizerAndFit(m_sizer);
}


void
LabelPanel::refresh()
{
    bool modified = m_diskdata->isModified();

    // refreshing the label triggers the EVT_TEXT action,
    // which set the modified bit.  since we just did an update,
    // set it back to what it was before the update.
    m_diskdata->setModified(modified);

    // I tried doing this in the constructor, but all the text was
    // always selected by default.  oh, well, better than nothing.
    int last = m_text->GetLastPosition();
    m_text->SetSelection(last, last);

    m_parent->updateButtons();
}


wxString
LabelPanel::getLabelString()
{
    return m_text->GetValue();
}


// used for all dialog button presses
void
LabelPanel::OnLabelEdit(wxCommandEvent& event)
{
    switch (event.GetId()) {

        case LABEL_EDIT_LABEL:
            m_diskdata->setModified(true);      // note that it has changed
            m_parent->updateButtons();
            break;

        default:
            wxASSERT(0);
            break;
    }
}
