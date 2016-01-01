// Although the IoCard class is abstract for the most part,
// these factory functions live here

#include "Ui.h"
#include "CardCfgState.h"
#include "DiskCtrlCfgState.h"
#include "Host.h"
#include "Cpu2200.h"
#include "IoCardDisk.h"
#include "IoCardDisplay.h"
#include "IoCardKeyboard.h"
#include "IoCardPrinter.h"
#include "Scheduler.h"
#include "System2200.h"
#include "SysCfgState.h"


// ========================================================================
// card management functions (used by UI)
// ========================================================================

// create an instance of the specified card
IoCard *
IoCard::makeCard(Scheduler& scheduler, Cpu2200& cpu, card_type_e type,
                 int baseaddr, int cardslot, const CardCfgState *cfg)
{
    return makeCardImpl(scheduler, cpu, type, baseaddr, cardslot, cfg);
}


// make a temporary card; this is so code can query the properties of the card;
// as such, the IoCard* functions know to do only partial construction
IoCard *
IoCard::makeTmpCard(card_type_e type)
{
    return makeCardImpl( *(Scheduler*)0, *(Cpu2200*)0, type, 0x000, -1,
                         (CardCfgState *)0 );
}


// this is the shared implementation that the other make*Card functions use
IoCard *
IoCard::makeCardImpl(Scheduler& scheduler, Cpu2200& cpu, card_type_e type,
                     int baseaddr, int cardslot,
                     const CardCfgState *cfg)
{
    IoCard* crd = nullptr;

    switch (type) {
        case card_keyboard:
            crd = new IoCardKeyboard( scheduler, cpu, baseaddr, cardslot );
            break;
        case card_disp_64x16:
            crd = new IoCardDisplay( scheduler, cpu, baseaddr, cardslot,
                                     UI_SCREEN_64x16 );
            break;
        case card_disp_80x24:
            crd = new IoCardDisplay( scheduler, cpu, baseaddr, cardslot,
                                     UI_SCREEN_80x24 );
            break;
        case card_printer:
            crd = new IoCardPrinter( cpu, baseaddr, cardslot );
            break;
        case card_disk:
            crd = new IoCardDisk( scheduler, cpu, baseaddr, cardslot, cfg );
            break;
        default:
            assert(0);
            break;
    }

    return crd;
}

// vim: ts=8:et:sw=4:smarttab
