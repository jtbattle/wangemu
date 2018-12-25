// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "CardInfo.h"
#include "Cpu2200.h"
#include "host.h"
#include "IoCard.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiSystemConfigDlg.h"
#include "system2200.h"

enum
{
    ID_CPU_CHOICE = 1,
    ID_MEMSIZE_CHOICE,

    ID_CHK_DISK_REALTIME,
    ID_CHK_WARN_IO,

    ID_SLOT0_CARD_CHOICE = 100,
    // ... leave room for other slots
    ID_SLOTN_CARD_CHOICE = 112,

    ID_SLOT0_ADDR_CHOICE = 200,
    // ... leave room for other slots
    ID_SLOTN_ADDR_CHOICE = 212,

    ID_SLOT0_BTN_CONFIG = 250,
    // ... leave room for other slots
    ID_SLOTN_BTN_CONFIG = 262,

    ID_BTN_REVERT = 400
};


// Layout:
//      top_sizer (V)
//      |
//      +-- lr_sizer (H)
//      |   |
//      |   +-- leftgroup (V)
//      |   |   |
//      |   |   +-- left_grid
//      |   |   +-- m_disk_realtime
//      |   |   +-- m_warn_io
//      |   |
//      |   +-- right_grid
//      |
//      +-- button_sizer (H)
//          |
//          +-- m_btn_revert
//          +-- m_btn_ok
//          +-- m_btn_cancel
SystemConfigDlg::SystemConfigDlg(wxFrame *parent) :
        wxDialog(parent, -1, "System Configuration",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) ,
        m_cpu_type(nullptr),
        m_card_desc{nullptr},
        m_card_addr{nullptr},
        m_card_cfg{nullptr},
        m_mem_size(nullptr),
        m_kb_type(nullptr),
        m_disk_realtime(nullptr),
        m_warn_io(nullptr),
        m_btn_revert(nullptr),
        m_btn_ok(nullptr),
        m_btn_cancel(nullptr),
        m_cfg    (system2200::config()),  // the one we will be editing
        m_old_cfg(system2200::config())   // the existing configuration
{
    const int v_text_margin = 4;
    const int h_text_margin = 8;

    // the grid on the left contains CPU related configuration
    wxFlexGridSizer *left_grid = new wxFlexGridSizer(5, 2, 0, 0);

    // leaf controls for left_grid
    m_cpu_type = new wxChoice(this, ID_CPU_CHOICE);
    for (auto &cpu_cfg : system2200::m_cpu_configs) {
        m_cpu_type->Append(cpu_cfg.label.c_str(), (void *)cpu_cfg.cpu_type);
    }

    m_mem_size = new wxChoice(this, ID_MEMSIZE_CHOICE);
    setMemsizeStrings();

    left_grid->Add(new wxStaticText(this, -1, ""), 0,
                   wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    left_grid->Add(new wxStaticText(this, -1, ""), 1,
                   wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);

    left_grid->Add(new wxStaticText(this, -1, "CPU"), 0,
                   wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    left_grid->Add(m_cpu_type, 1,
                   wxGROW | wxALIGN_CENTER_VERTICAL);

    left_grid->Add(new wxStaticText(this, -1, "RAM"), 0,
                   wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    left_grid->Add(m_mem_size, 1,
                   wxGROW | wxALIGN_CENTER_VERTICAL);

    // continuing on down the left side, we get this option
    m_disk_realtime = new wxCheckBox(this, ID_CHK_DISK_REALTIME,
                                           "Realtime Disk Emulation");
    // and this option
    m_warn_io = new wxCheckBox(this, ID_CHK_WARN_IO,
                                     "Warn on Invalid IO Device Access");

    // and we get a box sizer to group all things on the left
    wxBoxSizer *leftgroup = new wxBoxSizer(wxVERTICAL);
    leftgroup->Add(left_grid);
    leftgroup->Add(m_disk_realtime, 0, wxTOP, 15);
    leftgroup->Add(m_warn_io, 0, wxTOP, 15);

    // the grid on the right contains Slot related configuration
    wxFlexGridSizer *right_grid = new wxFlexGridSizer(1+NUM_IOSLOTS, 4, 0, 0);
    right_grid->AddGrowableCol(1, 3); // col #1: description
    right_grid->AddGrowableCol(2, 1); // col #2: address

    right_grid->Add(new wxStaticText(this, -1, ""), 0,
                    wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    right_grid->Add(new wxStaticText(this, -1, "Device Type"), 1,
                    wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    right_grid->Add(new wxStaticText(this, -1, "I/O Address"), 1,
                    wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    right_grid->Add(new wxStaticText(this, -1, ""), 1,
                    wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);

    for (int slot=0; slot<NUM_IOSLOTS; slot++) {

        m_card_desc[slot] = new wxChoice(this, ID_SLOT0_CARD_CHOICE+slot);
        // FIXME: for some reason, if I use -1, it reads as 0 (!) in
        //        OnCardChoice().  this was true in 2.6, and is true in 2.8.8
        m_card_desc[slot]->Append("(vacant)", (void*)(-2));

        for (int ctype=0; ctype<IoCard::NUM_CARDTYPES; ctype++) {
            const IoCard::card_t ct = IoCard::card_types[ctype];
            const std::string card_name = CardInfo::getCardName(ct);
            const std::string card_desc = CardInfo::getCardDesc(ct);
            const std::string str(card_name + " (" + card_desc + ")");
            m_card_desc[slot]->Append(str, (void*)ctype);
        }

        m_card_addr[slot] = new wxChoice(this, ID_SLOT0_ADDR_CHOICE+slot);
        m_card_cfg[slot] = new wxButton(this, ID_SLOT0_BTN_CONFIG+slot, "Config");

        // each row of the right grid has: label, description, ioaddr, config
        wxString label;
        label.Printf("Slot #%d", slot);
        right_grid->Add(new wxStaticText(this, -1, label), 0,
            wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
        right_grid->Add(m_card_desc[slot], 2, wxGROW | wxALIGN_CENTER_VERTICAL);
        right_grid->Add(m_card_addr[slot], 1, wxGROW | wxALIGN_CENTER_VERTICAL);
        right_grid->Add(m_card_cfg [slot], 0,          wxALIGN_CENTER_VERTICAL, h_text_margin);
    }

    // group the CPU and IO configuration side by side
    wxBoxSizer *lr_sizer = new wxBoxSizer(wxHORIZONTAL);

#define PUT_BOX_AROUND_IO_CONFIG 0
#if PUT_BOX_AROUND_IO_CONFIG
    wxStaticBox      *rsb  = new wxStaticBox(this, -1, "io");
    wxStaticBoxSizer *rsbs = new wxStaticBoxSizer(rsb, wxHORIZONTAL);
    rsbs->Add(right_grid, 1, wxEXPAND);
#endif

    lr_sizer->Add(leftgroup,  0, wxALL, 10);     // horizontally unstretchable
#if PUT_BOX_AROUND_IO_CONFIG
    lr_sizer->Add(rsbs,       1, wxALL, 10);     // horizontally stretchable
#else
    lr_sizer->Add(right_grid, 1, wxALL, 10);     // horizontally stretchable
#endif

    // put three buttons side by side
    m_btn_revert = new wxButton(this, ID_BTN_REVERT, "Revert");
    m_btn_ok     = new wxButton(this, wxID_OK,       "OK");
    m_btn_cancel = new wxButton(this, wxID_CANCEL,   "Cancel");

    wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
    button_sizer->Add(m_btn_revert, 0, wxALL, 10);
    button_sizer->Add(m_btn_ok,     0, wxALL, 10);
    button_sizer->Add(m_btn_cancel, 0, wxALL, 10);
#ifdef __WXMAC__
    // the cancel button was running into the window resizing grip
    button_sizer->AddSpacer(10);
#endif

    // config grids on top, confirmation buttons on the bottom
    wxBoxSizer *top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(lr_sizer,      1, wxEXPAND);       // vertically stretchable
    top_sizer->Add(button_sizer, 0, wxALIGN_RIGHT);  // vertically unstretchable

    updateDlg();        // select current options
    updateButtons();

    // tell the thing to get to work
    SetSizer(top_sizer);                 // use the sizer for layout
    top_sizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();  // get default size & location

    // event routing table
    Bind(wxEVT_CHOICE,   &SystemConfigDlg::OnCpuChoice,     this, ID_CPU_CHOICE);
    Bind(wxEVT_CHOICE,   &SystemConfigDlg::OnMemsizeChoice, this, ID_MEMSIZE_CHOICE);
    Bind(wxEVT_CHECKBOX, &SystemConfigDlg::OnDiskRealtime,  this, ID_CHK_DISK_REALTIME);
    Bind(wxEVT_CHECKBOX, &SystemConfigDlg::OnWarnIo,        this, ID_CHK_WARN_IO);
    Bind(wxEVT_BUTTON,   &SystemConfigDlg::OnButton,        this, -1);

    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &SystemConfigDlg::OnCardChoice, this,
         ID_SLOT0_CARD_CHOICE, ID_SLOTN_CARD_CHOICE);
    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &SystemConfigDlg::OnAddrChoice, this,
         ID_SLOT0_ADDR_CHOICE, ID_SLOTN_ADDR_CHOICE);
}


// set the memory size choices to just what is legal for this CPU type.
void
SystemConfigDlg::setMemsizeStrings()
{
    m_mem_size->Clear();  // erase any existing strings

    const int cpu_type = m_cfg.getCpuType();
    auto cpu_cfg = system2200::getCpuConfig(cpu_type);
    assert(cpu_cfg != nullptr);

    for (auto const kb : cpu_cfg->ram_size_options) {
        char label[10];
        if (kb < 1024) {
            sprintf(&label[0], "%3d KB", kb);
        } else {
            sprintf(&label[0], "%3d MB", kb/1024);
        }
        m_mem_size->Append(&label[0], (void*)kb);
    }
}


// update the display to reflect the current state
void
SystemConfigDlg::updateDlg()
{
    const int cpu_type = m_cfg.getCpuType();
    bool found = false;
    for (unsigned int n=0; n<system2200::m_cpu_configs.size(); ++n) {
        if (system2200::m_cpu_configs[n].cpu_type == cpu_type) {
            m_cpu_type->SetSelection(n);
            found = true;
            break;
        }
    }
    assert(found);

    m_mem_size->SetSelection(-1); // just in case there is no match, make it obvious
    const int ram_size = m_cfg.getRamKB();
    for (unsigned int i=0; i<m_mem_size->GetCount(); i++) {
        if (reinterpret_cast<int>(m_mem_size->GetClientData(i)) == ram_size) {
            m_mem_size->SetSelection(i);
        }
    }

    m_disk_realtime->SetValue(m_cfg.getDiskRealtime());

    m_warn_io->SetValue(m_cfg.getWarnIo());

    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        const IoCard::card_t card_type = m_cfg.getSlotCardType(slot);
        const int int_cardtype = static_cast<int>(card_type);
        if (card_type == IoCard::card_t::none) {
            m_card_desc[slot]->SetSelection(0);
        } else {
            m_card_desc[slot]->SetSelection(int_cardtype + 1);
        }
        setValidIoChoices(slot, int_cardtype);
    }
}


// called after various state changes, this updates the buttons to indicate
// if reverting state is possible, whether the current configuration is
// legal, and whether the configuration requires a reboot.
void
SystemConfigDlg::updateButtons()
{
    // if nothing has changed, disable the revert button
    m_btn_revert->Enable(m_cfg != m_old_cfg);

    // if the current configuration state isn't legal, change the button text.
    // we could disable it, but then the user couldn't hit OK and be told
    // why it is disabled.  It would also be possible to add a tooltip to
    // serve this purpose.
    std::string label;
    if (!configStateOk(false /*don't warn*/)) {
        label = "not OK";
    } else if (m_old_cfg.needsReboot(m_cfg)) {
        label = "OK, reboot";
    } else {
        label = "OK";
    }
    m_btn_ok->SetLabel(label);

    // it might be that we remove the last card with a config button,
    // or add the first, in which case we should resize the dialog to fit.
    // I'm not sure this is the best way to do this, but it works well enough.
    Layout();
    Fit();
    const wxSize sz(GetSize());
    SetMinSize(sz);
}


void
SystemConfigDlg::OnCpuChoice(wxCommandEvent& WXUNUSED(event))
{
    const int selection = m_cpu_type->GetSelection();
    const int cpu_type  = reinterpret_cast<int>(m_cpu_type->GetClientData(selection));

    m_cfg.setCpuType(cpu_type);

    // update the choices for memory size
    setMemsizeStrings();

    // try to map current memory size to legal one
    auto cpu_cfg = system2200::getCpuConfig(cpu_type);
    assert(cpu_cfg != nullptr);

    const int ram_choices = cpu_cfg->ram_size_options.size();
    const int min_ram = cpu_cfg->ram_size_options[0];
    const int max_ram = cpu_cfg->ram_size_options[ram_choices-1];
    int cur_mem = m_cfg.getRamKB();
    if (cur_mem < min_ram) { cur_mem = min_ram; }
    if (cur_mem > max_ram) { cur_mem = max_ram; }

    int i = 0;
    for (const int kb : cpu_cfg->ram_size_options) {
        if (cur_mem <= kb) {
            // round up to the next biggest ram in the list of valid sizes
            m_mem_size->SetSelection(i);
            m_cfg.setRamKB(kb);
            break;
        }
        i++;
    }

    updateButtons();
}


void
SystemConfigDlg::OnMemsizeChoice(wxCommandEvent& WXUNUSED(event))
{
    const int selection = m_mem_size->GetSelection();
    m_cfg.setRamKB(reinterpret_cast<int>(m_mem_size->GetClientData(selection)));
    updateButtons();
}


void
SystemConfigDlg::OnDiskRealtime(wxCommandEvent& WXUNUSED(event))
{
    const bool checked = m_disk_realtime->IsChecked();
    m_cfg.setDiskRealtime(checked);
    updateButtons();
}

void
SystemConfigDlg::OnWarnIo(wxCommandEvent& WXUNUSED(event))
{
    const bool checked = m_warn_io->IsChecked();
    m_cfg.setWarnIo(checked);
    updateButtons();
}

void
SystemConfigDlg::OnCardChoice(wxCommandEvent &event)
{
    const int id = event.GetId();
    assert(id >= ID_SLOT0_CARD_CHOICE && id <= ID_SLOTN_CARD_CHOICE);
    const int slot = id - ID_SLOT0_CARD_CHOICE;
    const wxChoice *hCtl = m_card_desc[slot];
    assert(hCtl != nullptr);
    const int selection = hCtl->GetSelection();
    int idx = reinterpret_cast<int>(hCtl->GetClientData(selection));
    if (idx < 0) { idx = -1; }  // hack due to -2 hack earlier
    const IoCard::card_t card_type = static_cast<IoCard::card_t>(idx);

    m_cfg.setSlotCardType(slot, card_type);
    m_cfg.setSlotCardAddr(slot, -1);       // user must set io later

    // update the associated io_addr picker
    setValidIoChoices(slot, idx);

    updateButtons();
}


void
SystemConfigDlg::OnAddrChoice(wxCommandEvent &event)
{
    const int id = event.GetId();
    assert(id >= ID_SLOT0_ADDR_CHOICE && id <= ID_SLOTN_ADDR_CHOICE);
    const int slot = id - ID_SLOT0_ADDR_CHOICE;
    const int addr_sel = m_card_addr[slot]->GetSelection();
    const int card_sel = m_card_desc[slot]->GetSelection();
    const int card_type_idx = reinterpret_cast<int>(m_card_desc[slot]->GetClientData(card_sel));
    assert(card_type_idx >= 0);

    std::vector<int> base_addresses = CardInfo::getCardBaseAddresses(static_cast<IoCard::card_t>(card_type_idx));
    m_cfg.setSlotCardAddr(slot, base_addresses[addr_sel]);

    updateButtons();
}


// return true if it is OK to commit the state as it is
bool
SystemConfigDlg::configStateOk(bool warn)
{
    // make sure all io addresses have been selected
    for (int slot=0; slot<NUM_IOSLOTS; slot++) {
        const int card_sel = m_card_desc[slot]->GetSelection();
        if (card_sel == 0) {
            continue;   // not occupied
        }
        const int addr_sel = m_card_addr[slot]->GetSelection();
        if (addr_sel < 0) {
            if (warn) {
                UI_error("Please select an I/O address for slot %d", slot);
            }
            return false;
        }
    }

    // see if config mgr is happy with things
    return m_cfg.configOk(warn);
}


// used for all dialog button presses
void
SystemConfigDlg::OnButton(wxCommandEvent &event)
{
    switch (event.GetId()) {

        case ID_BTN_REVERT:
            m_cfg = m_old_cfg;          // revert state
            setMemsizeStrings();        // in case we switched cpu types
            updateDlg();                // select current options
            updateButtons();            // reevaluate button state
            m_btn_revert->Disable();
            break;

        case wxID_OK:
            if (configStateOk(true)) {
                saveDefaults();         // save location & size of dlg
                system2200::setConfig(m_cfg);
                EndModal(0);
            }
            break;

        case wxID_CANCEL:
            saveDefaults();             // save location & size of dlg
            EndModal(1);
            break;

        default:
            if ((event.GetId() >= ID_SLOT0_BTN_CONFIG) &&
                (event.GetId() <= ID_SLOTN_BTN_CONFIG)) {
                // one of the per-card configuration buttons was pressed
                const int slot = event.GetId() - ID_SLOT0_BTN_CONFIG;
                m_cfg.editCardConfig(slot);
                updateButtons();
            } else {
                // we don't recognize the ID -- let default handler take it
                event.Skip();
            }
            break;
    }
}


// save dialog options to the config file
void
SystemConfigDlg::saveDefaults()
{
    const std::string subgroup("ui/configdlg");

    // save position and size
    host::configWriteWinGeom(this, subgroup);
}


void
SystemConfigDlg::getDefaults()
{
    // see if we've established a favored location and size
    const std::string subgroup("ui/configdlg");
    host::configReadWinGeom(this, subgroup);
}


// this routine refreshes the list of choices of valid IO addresses
// for given slot.  it is called on init of the dialog for each slot
// and after any time a new card is chosen for a slot.
void
SystemConfigDlg::setValidIoChoices(int slot, int card_type_idx)
{
    wxChoice *hAddrCtl = m_card_addr[slot];
    assert(hAddrCtl != nullptr);
    wxButton *hCfgCtl  = m_card_cfg[slot];
    assert(hCfgCtl != nullptr);
    const IoCard::card_t ct = static_cast<IoCard::card_t>(card_type_idx);
    hAddrCtl->Clear();      // wipe out any previous list

    const bool occupied = m_cfg.isSlotOccupied(slot);
    hAddrCtl->Enable(occupied);

    if (occupied) {
        std::vector<int> base_addresses = CardInfo::getCardBaseAddresses(ct);

        int addr_mtch_idx = -1;
        for (unsigned int j=0; j<base_addresses.size(); j++) {
            const int io_addr = base_addresses[j];
            wxString str;
            str.Printf("0x%03X", io_addr);
            hAddrCtl->Append(str);
            if ((io_addr & 0xFF) == (m_cfg.getSlotCardAddr(slot) & 0xFF)) {
                addr_mtch_idx = j;
            }
        }
        //assert(addr_mtch_idx > -1);
        // if the card changes and the old io address isn't valid for the
        // new card, we let -1 ride and it causes the choice to be blanked.
        hAddrCtl->SetSelection(addr_mtch_idx);
    } else {
        // although disabled, we assign some text to this control, otherwise
        // weird resizing behavior was observed.  outside of a grid, the empty
        // wxChoice box appears to want a fairly wide box, but the occupied
        // slots desire something much less, like half.  however, in this
        // context the column of wxChoice I/O address controls are contained
        // inside the wxFlexGridSizer, which tries to make them all the same
        // size.  As the dialog is made narrower or wider, the division between
        // the space allocated to column 2 and column 3 had a dramatic jump
        // in the weighting.  so, this is why this space is here.
        hAddrCtl->Append("    ");
    }

    // hide the configuration button if there is nothing to configure
    const bool show_btn = occupied && CardInfo::isCardConfigurable(ct);
    hCfgCtl->Show(show_btn);
}

// vim: ts=8:et:sw=4:smarttab
