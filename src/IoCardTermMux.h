// Function MXD Terminal Mux card emulation

#ifndef _INCLUDE_IOCARD_TERM_MUX_H_
#define _INCLUDE_IOCARD_TERM_MUX_H_

#include "IoCard.h"
#include <queue>

class Cpu2200;
class Scheduler;
class Timer;

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

    // ----- IoCardTermMux specific functions -----
    void receiveKeystroke(int term_num, int keycode);
    // ...

private:

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

    // send an init sequence from emulated terminal shortly after reset
    void SendInitSeq();

    static const unsigned int MAX_TERMINALS =  4;
    static const unsigned int KB_BUFF_MAX   = 64;
    void CheckKbBuffer(int term_num);
    void UartTxTimerCallback(int term_num);

    // board state
    std::shared_ptr<Scheduler> m_scheduler; // shared event scheduler
    std::shared_ptr<Cpu2200>   m_cpu;       // associated CPU
//  struct i8080              *m_i8080;     // control processor
    void                      *m_i8080;     // control processor
    uint8                      m_ram[4096]; // i8080 RAM
    const int                  m_baseaddr;  // the address the card is mapped to
    const int                  m_slot;      // which slot the card is plugged into
    bool                       m_selected;  // the card is currently selected
    bool                       m_cpb;       // the cpu is busy
    int                        m_io_offset; // (selected io addr & 7), for convenience
    bool                       m_prime_seen; // !PRMS (reset) strobe received
    bool                       m_obs_seen;  // OBS strobe received
    bool                       m_cbs_seen;  // CBS strobe received
    int                        m_obscbs_offset;
    int                        m_obscbs_data;
    int                        m_rbi;       // 0=ready, 1=busy
    int                        m_uart_sel;  // one hot encoding of selected uart
    bool                       m_interrupt_pending;  // one of the uarts has an rx byte
    std::shared_ptr<Timer>     m_init_tmr;  // send init sequence from terminal

    // per terminal state
    struct m_term_t {
        // display related:
        CrtFrame              *wndhnd;      // opaque handle to UI window
	// the transport baud rate is emulated here, as is the terminal keyboard buffer
	std::queue<uint8>      remote_kb_buff;
	std::shared_ptr<Timer> remote_baud_tmr;
        // uart receive state
        bool                   rx_ready;    // received a byte
        int                    rx_byte;     // value of received byte
        // uart transmit state
        bool                   tx_ready;    // room to accept a byte (1 deep FIFO)
        int                    tx_byte;     // value of tx byte
        std::shared_ptr<Timer> tx_tmr;      // model uart xfer rate
    } m_terms[MAX_TERMINALS];
};

#endif // _INCLUDE_IOCARD_TERM_MUX_H_

// vim: ts=8:et:sw=4:smarttab
