// This is a helper class to IoCard, performing tasks related to IoCards
// but which shouldn't be part of the IoCard interface since it doesn't make
// sense for these interfaces to be inherited by the concrete IoCard types.
//
// Most of the functions return some attribute of a card type given the
// card's card_t enum value.  One function allows looking up this enum
// value from the card's name.  This is used in constructing objects as the
// system configuration state is read from the ini file.

#ifndef _INCLUDE_CARDINFO_H_
#define _INCLUDE_CARDINFO_H_

#include "w2200.h"
#include "IoCard.h"     // to pick up card_t

// CardInfo could be a namespace, except one can't declare a friend namespace.
// instead, each function would have to be made a friend.

class CardInfo  // is a friend of class IoCard
{
public:
    // utility function to map card name to cardtype index
    static IoCard::card_t  getCardTypeFromName(const std::string &name);

    // returns info about a given card type.
    // attr is the attribute being requested, n an optional parameter,
    // and the returned value of that attribute is in void* value.
    static std::string      getCardName(IoCard::card_t cardtype);
    static std::string      getCardDesc(IoCard::card_t cardtype);
    static std::vector<int> getCardBaseAddresses(IoCard::card_t cardtype);
    static bool             isCardConfigurable(IoCard::card_t cardtype);
    static std::shared_ptr<CardCfgState> getCardCfgState(IoCard::card_t cardtype);
};

#endif // _INCLUDE_CARDINFO_H_

// vim: ts=8:et:sw=4:smarttab
