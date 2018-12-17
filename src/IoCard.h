// IoCard class is abstract, with actual card types inheriting from it.
// It does provide a real service, though, through the static makeCard()
// and makeTmpCard() factory functions.
//
// There is an aspect to the class design that dovetails with the friend class,
// CardInfo, that should be explained.  CardInfo's member functions take a
// card_t enum and return some attribute of cards of that type.
// Not wanting to have a bunch of switch statements that need to be edited
// each time a new card is added to the emulator, instead the makeTmpCard()
// function of this class provides a card of the given type.  The various
// card attributes are looked up by protected virtual functions.
//
// Thus, when a new card type is introduced, three things must be added:
//   (1) the new class must be created (see IoCardXXX.[ch] for a skeleton)
//   (2) a new enum value must be added to card_t below
//   (3) one switch statement, in makeCardImpl(), must be edited to
//       map the enum to an instance of the new card type
// Cardinfo knows nothing about the new card.

#ifndef _INCLUDE_IOCARD_H_
#define _INCLUDE_IOCARD_H_

#include "w2200.h"

class CardInfo;
class CardCfgState;
class Cpu2200;
class Scheduler;

class IoCard
{
    friend class CardInfo;

public:
    // interface class destructors must be virtual
    virtual ~IoCard() {};

    // ------------------------ informative ------------------------

    // return the list of addresses that this specific card responds to
    virtual std::vector<int> getAddresses() const = 0;

    // change card configuration
    virtual void setConfiguration(const CardCfgState &) { };

    // configuration management:
    // invoke GUI to edit state handed to us
    virtual void editConfiguration(CardCfgState *) { };

    // ------------------------ operational ------------------------

    // this is called when the machine is reset.
    // hard_reset is true on cold boot, and false on warm reset.
    virtual void reset(bool hard_reset=true) = 0;

    // this is called when a card is addressed via an -ABS strobe.
    virtual void select() = 0;

    // this is called when a card that was previously addressed
    // is no longer addressed due to another card getting the -ABS.
    // this will be called before the new one being addressed will
    // see its Select().
    virtual void deselect() = 0;

    // a byte, has been output to device.
    virtual void OBS(int val) = 0;

    // a request to return status.
    // the manual says that a card receiving CBS is expected to
    // return IBS later.  however, different IO devices use it
    // in different ways.  some completely ignore the bit.  some
    // use the CBS strobe to trigger the IBS strobe.  others use
    // it as a secondary OBS strobe to allow sending data to a
    // second register.  each IoCard will have to figure out what
    // is appropriate for that card.
    virtual void CBS(int val) = 0;

    // a request to return IB5.
    // this feature supports an ugly hack.  I'm quite surprised
    // that Wang did this.  a certain CPU status bit reflects the
    // state of IB5 (the bus that peripherals use to drive data
    // to the CPU) without any associated strobe.
    //
    // One known use is that the 64x16 CRT controller doesn't
    // support this function, so the bit appears as a 0 to the CPU,
    // but the 80x24 CRT controller always drives IB5 with a 1.
    // The microcode uses this to set whether device 005 is either
    // 64 or 80 characters wide.
    //
    // This bit is polled in a number of other places in the ucode,
    // but their use is so far undetermined.  I do know that if one
    // does a LOAD or SAVE on a tape device, the bit is somehow used.
    virtual int getIB() const { return 0; };

    // a request to return data.
    // when the CPU ST1.2 (CPB, "CPU Busy") is set low, it signals
    // that it is waiting for the selected IO device to produce
    // an IBS (In Bus Strobe).  when the device has data to return,
    // IBS is pulsed, which sets CPB back high, and simultaneously
    // the data on the In bus is clocked into the K register.  when
    // the microcode sees CPB again, it knows that K contains valid
    // data from the selected device.  the device uses the function
    // IoCardCbIbs() to supply the IBS data to the CPU.
    virtual void CPB(bool busy) = 0;

    // --------------- static member functions ---------------

    // the types of cards that may by plugged into a slot
    enum class card_t
    {
        none = -1,         // signifies empty slot
        keyboard,
        disp_64x16,
        disp_80x24,
        term_mux,
        printer,
        disk
    };

    // initialized after the class declaration:
    static const int NUM_CARDTYPES = 6;
    constexpr static card_t card_types[NUM_CARDTYPES] = {
        IoCard::card_t::keyboard,
        IoCard::card_t::disp_64x16,
        IoCard::card_t::disp_80x24,
        IoCard::card_t::term_mux,
        IoCard::card_t::printer,
        IoCard::card_t::disk
    };

    // return true if the argument is a legal card enum
    static bool legal_card_t(card_t c) {
        for (auto &ct : card_types) {
            if (c == ct)
                return true;
        }
        return false;
    }

    // create an instance of the specified card; the card configuration,
    // if it has one, will come from the ini file
    static std::unique_ptr<IoCard> makeCard(
                            std::shared_ptr<Scheduler> scheduler,
                            std::shared_ptr<Cpu2200>   cpu,
                            card_t type, int baseaddr, int cardslot,
                            const CardCfgState *cfg);

    // make a temporary card in order to query its properties.
    // the IoCard* functions know to do only partial construction.
    static std::unique_ptr<IoCard> makeTmpCard( card_t type,
                                                int baseaddr=0x000);

protected:  // these are used by the CardInfo class

    // return a string describing the card type
    virtual const std::string getDescription() const = 0;

    // return a string of Wang card number for the device
    virtual const std::string getName() const = 0;

    // return a list of the various base addresses a card can map to
    virtual std::vector<int> getBaseAddresses() const = 0;

    // is card configurable?  overridden by subclass if it is
    virtual bool isConfigurable() const { return false; }

    // subclass returns its own type of configuration object
    virtual std::shared_ptr<CardCfgState> getCfgState() { return nullptr; }

private:
    // shared implementation the other make*Card function use.
    // if cardslot is -1, this is a temp card that is incompletely
    // initialized simply so we can use the methods to look up card
    // properties (ugly).  finally, if we provide real cardslot
    // information and existing_card is false, we return a new card
    // that has default state associated with it.
    static std::unique_ptr<IoCard> makeCardImpl(
                                std::shared_ptr<Scheduler> scheduler,
                                std::shared_ptr<Cpu2200>   cpu,
                                card_t type, int baseaddr, int cardslot,
                                const CardCfgState *cfg);
};

#endif  // _INCLUDE_IOCARD_H_

// vim: ts=8:et:sw=4:smarttab
