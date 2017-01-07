
#include "CardInfo.h"

// utility function to map card name to cardtype index
IoCard::card_t
CardInfo::getCardTypeFromName(const std::string &name)
{
    for(int i=0; i<(int)IoCard::NUM_CARDTYPES; i++) {
        IoCard::card_t ii = static_cast<IoCard::card_t>(i);
        std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(ii));
        assert(tmpcard != nullptr);
        std::string thisname = tmpcard->getName();
        if (name == thisname) {
            return ii;
        }
    }
    return IoCard::card_t::none;
}


// return the name of the card type, eg, "2711b"
std::string
CardInfo::getCardName(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legal_card_t(cardtype));
    std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(cardtype));
    std::string name = tmpcard->getName();
    return name;
}


// return the description of the card type, eg, "64x16 CRT controller"
std::string
CardInfo::getCardDesc(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legal_card_t(cardtype));
    std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(cardtype));
    std::string desc = tmpcard->getDescription();
    return desc;
}


// return a list of the base addresses the card type can be mapped to
std::vector<int>
CardInfo::getCardBaseAddresses(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legal_card_t(cardtype));
    std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(cardtype));

    std::vector<int> addresses( tmpcard->getBaseAddresses() );
    return addresses;
}


// is card configurable?
bool
CardInfo::isCardConfigurable(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legal_card_t(cardtype));
    std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(cardtype));
    bool rv = tmpcard->isConfigurable();
    return rv;
}


// retrieve a pointer to a CardCfgState specific to a given kind of card
CardCfgState*
CardInfo::getCardCfgState(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legal_card_t(cardtype));
    std::unique_ptr<IoCard> tmpcard(IoCard::makeTmpCard(cardtype));
    CardCfgState *rv = tmpcard->getCfgState();
    return rv;
}

// vim: ts=8:et:sw=4:smarttab
