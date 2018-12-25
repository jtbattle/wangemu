
#include "CardInfo.h"

// utility function to map card name to cardtype index
IoCard::card_t
CardInfo::getCardTypeFromName(const std::string &name)
{
    for (auto &ct : IoCard::card_types) {
        std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(ct));
        assert(tmp_card != nullptr);
        std::string this_name = tmp_card->getName();
        if (name == this_name) {
            return ct;
        }
    }
    return IoCard::card_t::none;
}


// return the name of the card type, eg, "2711b"
std::string
CardInfo::getCardName(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legalCardType(cardtype));
    std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(cardtype));
    std::string name = tmp_card->getName();
    return name;
}


// return the description of the card type, eg, "64x16 CRT controller"
std::string
CardInfo::getCardDesc(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legalCardType(cardtype));
    std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(cardtype));
    std::string desc = tmp_card->getDescription();
    return desc;
}


// return a list of the base addresses the card type can be mapped to
std::vector<int>
CardInfo::getCardBaseAddresses(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legalCardType(cardtype));
    std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(cardtype));

    std::vector<int> addresses(tmp_card->getBaseAddresses());
    return addresses;
}


// is card configurable?
bool
CardInfo::isCardConfigurable(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legalCardType(cardtype));
    std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(cardtype));
    return tmp_card->isConfigurable();
}


// retrieve a pointer to a CardCfgState specific to a given kind of card
std::shared_ptr<CardCfgState>
CardInfo::getCardCfgState(IoCard::card_t cardtype)
{
    assert(cardtype == IoCard::card_t::none || IoCard::legalCardType(cardtype));
    std::unique_ptr<IoCard> tmp_card(IoCard::makeTmpCard(cardtype));
    return tmp_card->getCfgState();
}

// vim: ts=8:et:sw=4:smarttab
