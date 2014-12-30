// This is a helper class to IoCard, performing tasks related to IoCards
// but which shouldn't be part of the IoCard interface since it doesn't make
// sense for these interfaces to be inherited by the concrete IoCard types.
//
// Most of the functions return some attribute of a card type given the
// card's card_type_e enum value.  One function allows looking up this enum
// value from the card's name.  This is used in constructing objects as the
// system configuration state is read from the ini file.

#ifndef _INCLUDE_CARDINFO_H_
#define _INCLUDE_CARDINFO_H_

#include "w2200.h"
#include "IoCard.h"     // to pick up card_type_e

// CardInfo could be a namespace, except one can't declare a friend namespace.
// instead, each function would have to be made a friend.

class CardInfo  // is a friend of class IoCard
{
public:
    // utility function to map card name to cardtype index
    static IoCard::card_type_e  getCardTypeFromName(const string &name);

    // returns info about a given card type.
    // attr is the attribute being requested, n an optional parameter,
    // and the returned value of that attribute is in void* value.
    static string        getCardName(IoCard::card_type_e cardtype);
    static string        getCardDesc(IoCard::card_type_e cardtype);
    static vector<int>   getCardBaseAddresses(IoCard::card_type_e cardtype);
    static bool          isCardConfigurable(IoCard::card_type_e cardtype);
    static CardCfgState* getCardCfgState(IoCard::card_type_e cardtype);
};

#endif  // _INCLUDE_CARDINFO_H_
