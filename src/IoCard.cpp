// Although the IoCard class is abstract for the most part,
// these factory functions live here

#include "CardCfgState.h"
#include "Cpu2200.h"
#include "DiskCtrlCfgState.h"
#include "IoCardDisk.h"
#include "IoCardDisplay.h"
#include "IoCardKeyboard.h"
#include "IoCardPrinter.h"
#include "IoCardTermMux.h"
#include "Scheduler.h"
#include "SysCfgState.h"
#include "Ui.h"
#include "host.h"

// ========================================================================
// card management functions (used by UI)
// ========================================================================

// create an instance of the specified card
std::unique_ptr<IoCard>
IoCard::makeCard(std::shared_ptr<Scheduler> scheduler,
                 std::shared_ptr<Cpu2200>   cpu,
                 card_t type,
                 int base_addr, int card_slot, const CardCfgState *cfg)
{
    return makeCardImpl(scheduler, cpu, type, base_addr, card_slot, cfg);
}


// make a temporary card; this is so code can query the properties of the card;
// as such, the IoCard* functions know to do only partial construction
std::unique_ptr<IoCard>
IoCard::makeTmpCard(card_t type, int base_addr)
{
    std::shared_ptr<Scheduler> dummy_scheduler{nullptr};
    std::shared_ptr<Cpu2200t>  dummy_cpu{nullptr};
    CardCfgState const * const dummy_config{nullptr};

    return makeCardImpl(dummy_scheduler,
                        dummy_cpu,
                        type, base_addr, -1,
                        dummy_config);
}


// this is the shared implementation that the other make*Card functions use
std::unique_ptr<IoCard>
IoCard::makeCardImpl(std::shared_ptr<Scheduler> scheduler,
                     std::shared_ptr<Cpu2200>   cpu,
                     card_t type, int base_addr, int card_slot,
                     const CardCfgState *cfg)
{
    std::unique_ptr<IoCard> crd{nullptr};

    switch (type) {
        case card_t::keyboard:
            crd = std::make_unique<IoCardKeyboard>(
                            scheduler, cpu, base_addr, card_slot);
            break;
        case card_t::disp_64x16:
            crd = std::make_unique<IoCardDisplay>(
                            scheduler, cpu,
                            base_addr, card_slot, UI_SCREEN_64x16);
            break;
        case card_t::disp_80x24:
            crd = std::make_unique<IoCardDisplay>(
                            scheduler, cpu,
                            base_addr, card_slot, UI_SCREEN_80x24);
            break;
        case card_t::term_mux:
            crd = std::make_unique<IoCardTermMux>(
                            scheduler, cpu, base_addr, card_slot, cfg);
            break;
        case card_t::printer:
            crd = std::make_unique<IoCardPrinter>(cpu, base_addr, card_slot);
            break;
        case card_t::disk:
            crd = std::make_unique<IoCardDisk>(
                            scheduler, cpu, base_addr, card_slot, cfg);
            break;
        default:
            assert(false);
            break;
    }

    return crd;
}

// vim: ts=8:et:sw=4:smarttab
