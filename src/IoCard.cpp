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
#include "IoCardTermMux.h"
#include "Scheduler.h"
#include "System2200.h"
#include "SysCfgState.h"

// ========================================================================
// card management functions (used by UI)
// ========================================================================

// create an instance of the specified card
std::unique_ptr<IoCard>
IoCard::makeCard(std::shared_ptr<Scheduler> scheduler,
                 std::shared_ptr<Cpu2200>   cpu,
                 card_t type,
                 int baseaddr, int cardslot, const CardCfgState *cfg)
{
    return makeCardImpl(scheduler, cpu, type, baseaddr, cardslot, cfg);
}


// make a temporary card; this is so code can query the properties of the card;
// as such, the IoCard* functions know to do only partial construction
std::unique_ptr<IoCard>
IoCard::makeTmpCard(card_t type, int baseaddr)
{
    std::shared_ptr<Scheduler> dummy_scheduler{nullptr};
    std::shared_ptr<Cpu2200t>  dummy_cpu{nullptr};
    CardCfgState const * const dummy_config{nullptr};

    return makeCardImpl( dummy_scheduler,
                         dummy_cpu,
                         type, baseaddr, -1,
                         dummy_config );
}


// this is the shared implementation that the other make*Card functions use
std::unique_ptr<IoCard>
IoCard::makeCardImpl(std::shared_ptr<Scheduler> scheduler,
                     std::shared_ptr<Cpu2200>   cpu,
                     card_t type, int baseaddr, int cardslot,
                     const CardCfgState *cfg)
{
    std::unique_ptr<IoCard> crd{nullptr};

    switch (type) {
        case card_t::keyboard:
            crd = std::make_unique<IoCardKeyboard>(
                            scheduler, cpu, baseaddr, cardslot );
            break;
        case card_t::disp_64x16:
            crd = std::make_unique<IoCardDisplay>(
                            scheduler, cpu,
                            baseaddr, cardslot, UI_SCREEN_64x16 );
            break;
        case card_t::disp_80x24:
            crd = std::make_unique<IoCardDisplay>(
                            scheduler, cpu,
                            baseaddr, cardslot, UI_SCREEN_80x24 );
            break;
        case card_t::term_mux:
            crd = std::make_unique<IoCardTermMux>( scheduler, cpu, baseaddr, cardslot );
            break;
        case card_t::printer:
            crd = std::make_unique<IoCardPrinter>( cpu, baseaddr, cardslot );
            break;
        case card_t::disk:
            crd = std::make_unique<IoCardDisk>(
                            scheduler, cpu, baseaddr, cardslot, cfg );
            break;
        default:
            assert(false);
            break;
    }

    return crd;
}

// vim: ts=8:et:sw=4:smarttab
