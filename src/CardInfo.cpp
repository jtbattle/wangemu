
#include "CardInfo.h"

// utility function to map card name to cardtype index
IoCard::card_type_e
CardInfo::getCardTypeFromName(const string &name)
{
    for(int i=0; i<(int)IoCard::NUM_CARDTYPES; i++) {
        IoCard::card_type_e ii = static_cast<IoCard::card_type_e>(i);
        IoCard *tmpcard = IoCard::makeTmpCard(ii);
        string thisname = tmpcard->getName();
        if (name == thisname) {
            delete tmpcard;
            return ii;
        }
        delete tmpcard;
    }
    return IoCard::card_none;
}


// return the name of the card type, eg, "2711b"
string
CardInfo::getCardName(IoCard::card_type_e cardtype)
{
    ASSERT((cardtype >= 0) && (cardtype < (int)IoCard::NUM_CARDTYPES));
    IoCard *tmpcard = IoCard::makeTmpCard(cardtype);
    string name = tmpcard->getName();
    delete tmpcard;
    return name;
}


// return the description of the card type, eg, "64x16 CRT controller"
string
CardInfo::getCardDesc(IoCard::card_type_e cardtype)
{
    ASSERT((cardtype >= 0) && (cardtype < (int)IoCard::NUM_CARDTYPES));
    IoCard *tmpcard = IoCard::makeTmpCard(cardtype);
    string desc = tmpcard->getDescription();
    delete tmpcard;
    return desc;
}


// return a list of the base addresses the card type can be mapped to
vector<int>
CardInfo::getCardBaseAddresses(IoCard::card_type_e cardtype)
{
    ASSERT((cardtype >= 0) && (cardtype < (int)IoCard::NUM_CARDTYPES));
    IoCard *tmpcard = IoCard::makeTmpCard(cardtype);

    vector<int> addresses( tmpcard->getBaseAddresses() );
    delete tmpcard;
    return addresses;
}


// is card configurable?
bool
CardInfo::isCardConfigurable(IoCard::card_type_e cardtype)
{
    ASSERT((cardtype >= 0) && (cardtype < (int)IoCard::NUM_CARDTYPES));
    IoCard *tmpcard = IoCard::makeTmpCard(cardtype);
    bool rv = tmpcard->isConfigurable();
    delete tmpcard;
    return rv;
}


// retrieve a pointer to a CardCfgState specific to a given kind of card
CardCfgState*
CardInfo::getCardCfgState(IoCard::card_type_e cardtype)
{
    ASSERT((cardtype >= 0) && (cardtype < (int)IoCard::NUM_CARDTYPES));
    IoCard *tmpcard = IoCard::makeTmpCard(cardtype);
    CardCfgState *rv = tmpcard->getCfgState();
    delete tmpcard;
    return rv;
}
