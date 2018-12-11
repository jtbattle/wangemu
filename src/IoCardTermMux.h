// Function MXD Terminal Mux card emulation

#ifndef _INCLUDE_IOCARD_TERM_MUX_H_
#define _INCLUDE_IOCARD_TERM_MUX_H_

#include "IoCard.h"

class Cpu2200;
class Scheduler;
class Timer;
class Terminal;

class IoCardTermMux : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardTermMux);

    // ----- common IoCard functions -----
    IoCardTermMux(std::shared_ptr<Scheduler> scheduler,
                  std::shared_ptr<Cpu2200> cpu,
                  int baseaddr, int cardslot);
    ~IoCardTermMux();

    std::vector<int> getAddresses() const override;

    void  reset(bool hard_reset=true) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    int   getIB() const override;
    void  CPB(bool busy) override;

    // a keyboard event has happened
    void receiveKeystroke(int term_num, int keycode);

private:

    static const unsigned int MAX_TERMINALS = 4;

    // ---- card properties ----
    const std::string getDescription() const override;
    const std::string getName() const override;
    std::vector<int> getBaseAddresses() const override;

    // i8080 hal interface
    static int  i8080_rd_func(int addr, void *user_data);
    static void i8080_wr_func(int addr, int byte, void *user_data);
    static int  i8080_in_func(int addr, void *user_data);
    static void i8080_out_func(int addr, int byte, void *user_data);

    // perform one i8080 instruction
    int execOneOp();

    // update the board's !ready/busy status (if selected)
    void update_rbi();

    // raise an interrupt if any uart has an rx char ready
    void update_interrupt();

    void checkTxBuffer(int term_num);
    void mxdToTermCallback(int term_num, int byte);

    // ---- board state ----
    std::shared_ptr<Scheduler> m_scheduler; // shared event scheduler
    std::shared_ptr<Cpu2200>   m_cpu;       // associated CPU
    void       *m_i8080;             // control processor
    uint8       m_ram[4096];         // i8080 RAM
    const int   m_baseaddr;          // the address the card is mapped to
    const int   m_slot;              // which slot the card is plugged into
    bool        m_selected;          // the card is currently selected
    bool        m_cpb;               // the cpu is busy
    int         m_io_offset;         // (selected io addr & 7), for convenience
    bool        m_prime_seen;        // !PRMS (reset) strobe received
    bool        m_obs_seen;          // OBS strobe received
    bool        m_cbs_seen;          // CBS strobe received
    int         m_obscbs_offset;     // io_offset at time of obs or cbs strobe
    int         m_obscbs_data;       // data captured at obs or cbs strobe
    int         m_rbi;               // 0=ready, 1=busy
    int         m_uart_sel;          // currently addressed uart, 0..3
    bool        m_interrupt_pending; // one of the uarts has an rx byte

    // ---- per terminal state ----
    struct m_term_t {
        // display related:
        std::unique_ptr<Terminal> terminal; // terminal model
        // uart receive state
        bool                   rx_ready;    // received a byte
        int                    rx_byte;     // value of received byte
        // uart transmit state
        bool                   tx_ready;    // room to accept a byte (1 deep FIFO)
        int                    tx_byte;     // value of tx byte
        std::shared_ptr<Timer> tx_tmr;      // model uart rate & delay
    } m_terms[MAX_TERMINALS];
};

#endif // _INCLUDE_IOCARD_TERM_MUX_H_

// vim: ts=8:et:sw=4:smarttab
