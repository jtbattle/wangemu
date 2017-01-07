// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "CardInfo.h"
#include "Cpu2200.h"
#include "Host.h"
#include "IoCard.h"
#include "Ui.h"                 // emulator interface
#include "UiSystem.h"           // sharing info between UI_wxgui modules
#include "UiSystemConfigDlg.h"
#include "System2200.h"

#define KB_FEATURE    0 // allow user to specify keyboard type

enum
{
    ID_CPU_CHOICE = 1,
    ID_MEMSIZE_CHOICE,
#if KB_FEATURE
    ID_KEYBOARD_CHOICE,
#endif

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


// dialog events to catch
BEGIN_EVENT_TABLE(SystemConfigDlg, wxDialog)

    EVT_CHOICE(ID_CPU_CHOICE,           SystemConfigDlg::OnCpuChoice)
    EVT_CHOICE(ID_MEMSIZE_CHOICE,       SystemConfigDlg::OnMemsizeChoice)
#if KB_FEATURE
    EVT_CHOICE(ID_KEYBOARD_CHOICE,      SystemConfigDlg::OnKBChoice)
#endif
    EVT_CHECKBOX(ID_CHK_DISK_REALTIME,  SystemConfigDlg::OnDiskRealtime)
    EVT_CHECKBOX(ID_CHK_WARN_IO,        SystemConfigDlg::OnWarnIo)

    EVT_COMMAND_RANGE(ID_SLOT0_CARD_CHOICE, ID_SLOTN_CARD_CHOICE,
        wxEVT_COMMAND_CHOICE_SELECTED, SystemConfigDlg::OnCardChoice)
    EVT_COMMAND_RANGE(ID_SLOT0_ADDR_CHOICE, ID_SLOTN_ADDR_CHOICE,
        wxEVT_COMMAND_CHOICE_SELECTED, SystemConfigDlg::OnAddrChoice)

    EVT_BUTTON(-1, SystemConfigDlg::OnButton)
END_EVENT_TABLE()


// Layout:
//      topsizer (V)
//      |
//      +-- LRsizer (H)
//      |   |
//      |   +-- leftgroup (V)
//      |   |   |
//      |   |   +-- leftgrid
//      |   |   +-- m_diskRealtime
//      |   |   +-- m_warnIo
//      |   |
//      |   +-- rightgrid
//      |
//      +-- button_sizer (H)
//          |
//          +-- m_btnRevert
//          +-- m_btnOk
//          +-- m_btnCancel
SystemConfigDlg::SystemConfigDlg(wxFrame *parent) :
        wxDialog(parent, -1, "System Configuration",
                 wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) ,
        m_cpuType(nullptr),
        m_memSize(nullptr),
        m_kbType(nullptr),
        m_diskRealtime(nullptr),
        m_warnIo(nullptr),
        m_btnRevert(nullptr),
        m_btnOk(nullptr),
        m_btnCancel(nullptr),
        m_oldcfg( System2200().config() ),  // the existing configuration
        m_cfg   ( System2200().config() )   // the one we will be editing
{
    const int v_text_margin = 4;
    const int h_text_margin = 8;

    // the grid on the left contains CPU related configuration
    wxFlexGridSizer *leftgrid = new wxFlexGridSizer( 5, 2, 0, 0);

    // leaf controls for leftgrid
    m_cpuType = new wxChoice(this, ID_CPU_CHOICE);
    m_cpuType->Append("2200B",  (void *)Cpu2200::CPUTYPE_2200B);
    m_cpuType->Append("2200T",  (void *)Cpu2200::CPUTYPE_2200T);
    m_cpuType->Append("2200VP", (void *)Cpu2200::CPUTYPE_2200VP);

    m_memSize = new wxChoice(this, ID_MEMSIZE_CHOICE);
    setMemsizeStrings();

#if KB_FEATURE
    m_kbType = new wxChoice(this, ID_KEYBOARD_CHOICE);
    m_kbType->Append("2222");
    m_kbType->Enable( m_KbType->GetCount() > 1);
#endif

    leftgrid->Add(new wxStaticText(this, -1, ""), 0,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    leftgrid->Add(new wxStaticText(this, -1, ""), 1,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);

    leftgrid->Add(new wxStaticText(this, -1, "CPU"), 0,
                wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    leftgrid->Add(m_cpuType, 1,
                wxGROW | wxALIGN_CENTER_VERTICAL);

    leftgrid->Add(new wxStaticText(this, -1, "RAM"), 0,
                wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    leftgrid->Add(m_memSize, 1,
                wxGROW | wxALIGN_CENTER_VERTICAL);

#if KB_FEATURE
    leftgrid->Add(new wxStaticText(this, -1, "Keyboard"), 0,
                wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    leftgrid->Add(m_kbType, 1,
                wxGROW | wxALIGN_CENTER_VERTICAL);
#endif

    // continuing on down the left side, we get this option
    m_diskRealtime = new wxCheckBox(this, ID_CHK_DISK_REALTIME,
                                         "Realtime Disk Emulation");
    // and this option
    m_warnIo = new wxCheckBox(this, ID_CHK_WARN_IO,
                                   "Warn on Invalid IO Device Access");

    // and we get a box sizer to group all things on the left
    wxBoxSizer *leftgroup = new wxBoxSizer( wxVERTICAL );
    leftgroup->Add(leftgrid);
    leftgroup->Add(m_diskRealtime, 0, wxTOP, 15 );
    leftgroup->Add(m_warnIo, 0, wxTOP, 15);

    // the grid on the right contains Slot related configuration
    wxFlexGridSizer *rightgrid = new wxFlexGridSizer( 1+NUM_IOSLOTS, 4, 0, 0);
    rightgrid->AddGrowableCol(1,3); // col #1: description
    rightgrid->AddGrowableCol(2,1); // col #2: address

    rightgrid->Add(new wxStaticText(this, -1, ""), 0,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
    rightgrid->Add(new wxStaticText(this, -1, "Device Type"), 1,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    rightgrid->Add(new wxStaticText(this, -1, "I/O Address"), 1,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);
    rightgrid->Add(new wxStaticText(this, -1, ""), 1,
                wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL | wxBOTTOM, v_text_margin);

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {

        m_cardDesc[slot] = new wxChoice(this, ID_SLOT0_CARD_CHOICE+slot);
        // FIXME: for some reason, if I use -1, it reads as 0 (!) in
        //        OnCardChoice().  this was true in 2.6, and is true in 2.8.8
        m_cardDesc[slot]->Append("(vacant)", (void*)(-2));

        for(int ctype=0; ctype<IoCard::NUM_CARDTYPES; ctype++) {
            IoCard::card_t ct = static_cast<IoCard::card_t>(ctype);
            std::string cardname = CardInfo::getCardName(ct);
            std::string carddesc = CardInfo::getCardDesc(ct);
            std::string str( cardname + " (" + carddesc + ")" );
            m_cardDesc[slot]->Append(str, (void*)ctype);
        }

        m_cardAddr[slot] = new wxChoice(this, ID_SLOT0_ADDR_CHOICE+slot);
        m_cardCfg[slot] = new wxButton(this, ID_SLOT0_BTN_CONFIG+slot, "Config");

        // each row of the right grid has: label, description, ioaddr, config
        wxString label;
        label.Printf( "Slot #%d", slot );
        rightgrid->Add(new wxStaticText(this, -1, label), 0,
            wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, h_text_margin);
        rightgrid->Add(m_cardDesc[slot], 2, wxGROW | wxALIGN_CENTER_VERTICAL);
        rightgrid->Add(m_cardAddr[slot], 1, wxGROW | wxALIGN_CENTER_VERTICAL);
        rightgrid->Add(m_cardCfg [slot], 0,          wxALIGN_CENTER_VERTICAL, h_text_margin);
    }

    // group the CPU and IO configuration side by side
    wxBoxSizer *LRsizer = new wxBoxSizer( wxHORIZONTAL );

#define PUT_BOX_AROUND_IO_CONFIG 0
#if PUT_BOX_AROUND_IO_CONFIG
    wxStaticBox      *rsb  = new wxStaticBox( this, -1, "io");
    wxStaticBoxSizer *rsbs = new wxStaticBoxSizer( rsb, wxHORIZONTAL );
    rsbs->Add(rightgrid, 1, wxEXPAND);
#endif

    LRsizer->Add(leftgroup, 0, wxALL, 10 );     // horizontally unstretchable
#if PUT_BOX_AROUND_IO_CONFIG
    LRsizer->Add(rsbs,      1, wxALL, 10 );     // horizontally stretchable
#else
    LRsizer->Add(rightgrid, 1, wxALL, 10 );     // horizontally stretchable
#endif

    // put three buttons side by side
    m_btnRevert = new wxButton(this, ID_BTN_REVERT, "Revert");
    m_btnOk     = new wxButton(this, wxID_OK,       "OK");
    m_btnCancel = new wxButton(this, wxID_CANCEL,   "Cancel");

    wxBoxSizer *button_sizer = new wxBoxSizer( wxHORIZONTAL );
    button_sizer->Add(m_btnRevert, 0, wxALL, 10);
    button_sizer->Add(m_btnOk,     0, wxALL, 10);
    button_sizer->Add(m_btnCancel, 0, wxALL, 10);
#ifdef __WXMAC__
    // the cancel button was running into the window resizing grip
    button_sizer->AddSpacer(10);
#endif

    // config grids on top, confirmation buttons on the bottom
    wxBoxSizer *topsizer = new wxBoxSizer( wxVERTICAL );
    topsizer->Add(LRsizer,      1, wxEXPAND);       // vertically stretchable
    topsizer->Add(button_sizer, 0, wxALIGN_RIGHT);  // vertically unstretchable

    updateDlg();        // select current options
    updateButtons();

    // tell the thing to get to work
    SetSizer(topsizer);                 // use the sizer for layout
    topsizer->SetSizeHints(this);       // set size hints to honor minimum size

    getDefaults();  // get default size & location
}


// set the memory size choices to just what is legal for this CPU type.
void
SystemConfigDlg::setMemsizeStrings()
{
    m_memSize->Clear();  // erase any existing strings

    switch (m_cfg.getCpuType()) {
        case Cpu2200::CPUTYPE_2200B:
        case Cpu2200::CPUTYPE_2200T:
            m_memSize->Append(" 4 KB", (void*) 4);
            m_memSize->Append(" 8 KB", (void*) 8);
            m_memSize->Append("12 KB", (void*)12);
            m_memSize->Append("16 KB", (void*)16);
            m_memSize->Append("24 KB", (void*)24);
            m_memSize->Append("32 KB", (void*)32);
            break;
        case Cpu2200::CPUTYPE_2200VP:
            m_memSize->Append("32 KB", (void*)32);
            m_memSize->Append("64 KB", (void*)64);
            m_memSize->Append("128 KB", (void*)128);
            m_memSize->Append("256 KB", (void*)256);
            m_memSize->Append("512 KB", (void*)512);
            break;
        default:
            assert(false);
            break;
    }
}


// update the display to reflect the current state
void
SystemConfigDlg::updateDlg()
{
    m_cpuType->SetSelection(m_cfg.getCpuType());

#if KB_FEATURE
    m_kbType->SetSelection(0);
#endif

    m_memSize->SetSelection(-1); // just in case there is no match, make it obvious
    int ramsize = m_cfg.getRamKB();
    for(unsigned int i=0; i<m_memSize->GetCount(); i++) {
        if ((int)(m_memSize->GetClientData(i)) == ramsize) {
            m_memSize->SetSelection(i);
        }
    }

    m_diskRealtime->SetValue(m_cfg.getDiskRealtime());

    m_warnIo->SetValue(m_cfg.getWarnIo());

    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        IoCard::card_t cardtype = m_cfg.getSlotCardType(slot);
        int int_cardtype = static_cast<int>(cardtype);
        if (cardtype == IoCard::card_t::none) {
            m_cardDesc[slot]->SetSelection(0);
        } else {
            m_cardDesc[slot]->SetSelection(int_cardtype + 1);
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
    m_btnRevert->Enable( m_cfg != m_oldcfg );

    // if the current configuration state isn't legal, change the button text.
    // we could disable it, but then the user couldn't hit OK and be told
    // why it is disabled.  It would also be possible to add a tooltip to
    // serve this purpose.
    std::string label;
    if (!configStateOk(false /*don't warn*/)) {
        label = "not OK";
    } else if (m_oldcfg.needsReboot(m_cfg)) {
        label = "OK, reboot";
    } else {
        label = "OK";
    }
    m_btnOk->SetLabel(label);

    // it might be that we remove the last card with a config button,
    // or add the first, in which case we should resize the dialog to fit.
    // I'm not sure this is the best way to do this, but it works well enough.
    Layout();
    Fit();
    wxSize sz(GetSize());
    SetMinSize(sz);
}


void
SystemConfigDlg::OnCpuChoice( wxCommandEvent& WXUNUSED(event) )
{
    int selection = m_cpuType->GetSelection();
    int cputype   = (int)(m_cpuType->GetClientData(selection));
    int cur_mem   = m_cfg.getRamKB();

    m_cfg.setCpuType(cputype);

    // update the choices for memory size
    setMemsizeStrings();

    // try to map current memory size to legal one
    for(unsigned int i=0; i<m_memSize->GetCount(); i++) {
        int legal_size = (int)m_memSize->GetClientData(i);
        if (cur_mem == legal_size) {
            // perfect mapping -- done
            m_memSize->SetSelection(i);
            break;
        } else if ((cputype != Cpu2200::CPUTYPE_2200VP) && (legal_size == 32)) {
            // in case it was > 32KB before
            m_memSize->SetSelection(i);
            m_cfg.setRamKB(legal_size);
            break;
        } else if (cur_mem < legal_size) {
            // round up to the next memory size
            m_memSize->SetSelection(i);
            m_cfg.setRamKB(legal_size);
            break;
        }
    }

    updateButtons();
}


void
SystemConfigDlg::OnMemsizeChoice( wxCommandEvent& WXUNUSED(event) )
{
    int selection = m_memSize->GetSelection();
    m_cfg.setRamKB( (int)(m_memSize->GetClientData(selection)) );
    updateButtons();
}


void
SystemConfigDlg::OnDiskRealtime( wxCommandEvent& WXUNUSED(event) )
{
    bool checked = m_diskRealtime->IsChecked();
    m_cfg.setDiskRealtime( checked );
    updateButtons();
}

void
SystemConfigDlg::OnWarnIo( wxCommandEvent& WXUNUSED(event) )
{
    bool checked = m_warnIo->IsChecked();
    m_cfg.setWarnIo( checked );
    updateButtons();
}

void
SystemConfigDlg::OnCardChoice( wxCommandEvent &event )
{
    int id = event.GetId();
    assert(id >= ID_SLOT0_CARD_CHOICE && id <= ID_SLOTN_CARD_CHOICE);
    int slot = id - ID_SLOT0_CARD_CHOICE;
    wxChoice *hCtl = m_cardDesc[slot];
    int selection = hCtl->GetSelection();
    int idx = (int)(hCtl->GetClientData(selection));
    if (idx < 0) idx = -1;  // hack due to -2 hack earlier
    IoCard::card_t cardtype = static_cast<IoCard::card_t>(idx);

    m_cfg.setSlotCardType( slot, cardtype );
    m_cfg.setSlotCardAddr( slot, 0x00 );       // user must set io later

    // update the associated io_addr picker
    setValidIoChoices(slot, idx);

    updateButtons();
}


void
SystemConfigDlg::OnAddrChoice( wxCommandEvent &event )
{
    int id = event.GetId();
    assert(id >= ID_SLOT0_ADDR_CHOICE && id <= ID_SLOTN_ADDR_CHOICE);
    int slot = id - ID_SLOT0_ADDR_CHOICE;
    int cardsel      =       m_cardDesc[slot]->GetSelection();
    int cardtype_idx = (int)(m_cardDesc[slot]->GetClientData(cardsel));
    int addrsel      =       m_cardAddr[slot]->GetSelection();
    assert(cardtype_idx >= 0);

    std::vector<int> base_addresses = CardInfo::getCardBaseAddresses(static_cast<IoCard::card_t>(cardtype_idx));
    m_cfg.setSlotCardAddr( slot, base_addresses[addrsel] );

    updateButtons();
}


// return true if it is OK to commit the state as it is
bool
SystemConfigDlg::configStateOk(bool warn)
{
    // make sure all io addresses have been selected
    for(int slot=0; slot<NUM_IOSLOTS; slot++) {
        int cardsel = m_cardDesc[slot]->GetSelection();
        if (cardsel == 0) {
            continue;   // not occupied
        }
        int addrsel = m_cardAddr[slot]->GetSelection();
        if (addrsel < 0) {
            if (warn) {
                UI_Error("Please select an I/O address for slot %d", slot);
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
            m_cfg = m_oldcfg;           // revert state
            setMemsizeStrings();        // in case we switched cpu types
            updateDlg();                // select current options
            updateButtons();            // reevaluate button state
            m_btnRevert->Disable();
            break;

        case wxID_OK:
            if (configStateOk(true)) {
                saveDefaults();         // save location & size of dlg
                System2200().setConfig(m_cfg);
                EndModal(0);
            }
            break;

        case wxID_CANCEL:
            saveDefaults();             // save location & size of dlg
            EndModal(1);
            break;

        default:
            if ( (event.GetId() >= ID_SLOT0_BTN_CONFIG) &&
                 (event.GetId() <= ID_SLOTN_BTN_CONFIG) ) {
                // one of the per-card configuration buttons was pressed
                int slot = event.GetId() - ID_SLOT0_BTN_CONFIG;
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
    Host().ConfigWriteWinGeom(this, subgroup);
}


void
SystemConfigDlg::getDefaults()
{
    // see if we've established a favored location and size
    const std::string subgroup("ui/configdlg");
    Host().ConfigReadWinGeom(this, subgroup);
}


// this routine refreshes the list of choices of valid IO addresses
// for given slot.  it is called on init of the dialog for each slot
// and after any time a new card is chosen for a slot.
void
SystemConfigDlg::setValidIoChoices(int slot, int cardtype_idx)
{
    wxChoice *hAddrCtl = m_cardAddr[slot];
    wxButton *hCfgCtl  = m_cardCfg[slot];
    hAddrCtl->Clear();      // wipe out any previous list

    bool occupied = m_cfg.isSlotOccupied(slot);
    hAddrCtl->Enable(occupied);

    IoCard::card_t ct = static_cast<IoCard::card_t>(cardtype_idx);
    if (occupied) {
        std::vector<int> base_addresses = CardInfo::getCardBaseAddresses(ct);

        int addr_mtch_idx = -1;
        for(unsigned int j=0; j<base_addresses.size(); j++) {
            int io_addr = base_addresses[j];
            wxString str;
            str.Printf( "0x%03X", io_addr);
            hAddrCtl->Append(str);
            if ((io_addr & 0xFF) == (m_cfg.getSlotCardAddr(slot) & 0xFF)) {
                addr_mtch_idx = j;
            }
        }
        //assert(addr_mtch_idx > -1);
        // if the card changes and the old io address isn't valid for the
        // new card, we left -1 ride and it causes the choice to be blanked.
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
    bool show_btn = occupied && CardInfo::isCardConfigurable(ct);
    hCfgCtl->Show(show_btn);
}

// vim: ts=8:et:sw=4:smarttab
