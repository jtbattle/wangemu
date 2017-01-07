// Function MXD Terminal Mux card emulation

#ifndef _INCLUDE_IOCARD_TERM_MUX_H_
#define _INCLUDE_IOCARD_TERM_MUX_H_

#include "IoCard.h"

#include <deque>

class Cpu2200;

class IoCardTermMux : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardTermMux);

    // ----- common IoCard functions -----
    IoCardTermMux(std::shared_ptr<Cpu2200> cpu, int baseaddr, int cardslot);
    ~IoCardTermMux();

    std::vector<int> getAddresses() const override;

    void  reset(bool hard_reset=true) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    int   getIB5() const override;
    void  CPB(bool busy) override;

    // ----- IoCardTermMux specific functions -----
    void receiveKeystroke(int keycode);
    // ...

    enum { KEYCODE_SF   = 0x0100,  // special function key flag
           KEYCODE_HALT = 0x0200,  // user pressed the reset button on the keyboard
           KEYCODE_EDIT =    240   // the EDIT key
    };

private:
    // command byte from 2200 to mux controller.
    // there are other commands for MXE, which is not currently emulated.
    // these commands arrive on 2200 I/O address 06 using a CBS strobe.
    // commands which supply further bytes send them via OBS strobes.
    enum class mux_cmd_t {
        CMD_SELECT               = 0xFF,
        CMD_NULL                 = 0x00,
        CMD_POWERON              = 0x01,
        CMD_INIT_CURRENT_TERM    = 0x02,
        CMD_DELETE_LINE_REQ      = 0x03,
        CMD_KEYBOARD_READY_CHECK = 0x04,
        CMD_KEYIN_POLL_REQ       = 0x05,
        CMD_KEYIN_LINE_REQ       = 0x06,
        CMD_LINE_REQ             = 0x07,
        CMD_PREFILL_LINE_REQ     = 0x08,
        CMD_REFILL_LINE_REQ      = 0x09,
        CMD_END_OF_LINE_REQ      = 0x0A,
        CMD_QUERY_LINE_REQ       = 0x0B,
        CMD_ACCEPT_LINE_REQ_DATA = 0x0C,
        CMD_REQ_CRT_BUFFER       = 0x0D,
        CMD_REQ_PRINT_BUFFER     = 0x0E,
        CMD_ERROR_LINE_REQ       = 0x0F,
        CMD_TERMINATE_LINE_REQ   = 0x10,
        CMD_SELECT_TERMINAL      = 0xFF,
    };

    // ---- card properties ----
    const std::string getDescription() const override;
    const std::string getName() const override;
    std::vector<int> getBaseAddresses() const override;

    void OBS_01(int val);
    void OBS_02(int val);
    void OBS_03(int val);
    void OBS_04(int val);
    void OBS_05(int val);
    void OBS_06(int val);
    void OBS_07(int val);

    void CBS_01(int val);
    void CBS_02(int val);
    void CBS_03(int val);
    void CBS_04(int val);
    void CBS_05(int val);
    void CBS_06(int val);
    void CBS_07(int val);

    // test if any key is ready to accept
    void check_keyready();

    // process pending line request command
    void perform_end_of_line_req();

    // state
    std::shared_ptr<Cpu2200> m_cpu;  // associated CPU
    const int   m_baseaddr;       // the address the card is mapped to
    const int   m_slot;           // which slot the card is plugged into
    bool        m_selected;       // the card is currently selected
    bool        m_cpb;            // the cpu is busy
    bool        m_card_busy;      // the card is busy doing something
    int         m_io_offset;      // (selected io addr & 7), for convenience

    // these are the size of various MXD buffers, as noted in
    // 2236MXE_Documentation.8-83.pdf, p. 5
    static const int CrtBufferSize      = 250;
    static const int PrinterBufferSize  = 160;
    static const int LineReqBufferSize  = 480;
    static const int KeyboardBufferSize =  36;

    static const int MAX_TERMINALS = 4;

    bool        m_vp_mode;        // true in vp mode, false in mvp mode

    struct m_term_t {
        // display related:
        CrtFrame         *wndhnd;      // opaque handle to UI window
        // related to offset 1:
        bool              io1_key_ready;   // io1_key_code is valid
        int               io1_key_code;    // keycode of most recently received keystroke
        // related to offset 6:
        unsigned int      io6_field_size;  // CBS(07) XXXX argument
        bool              io6_underline;   // CBS(07) YY argument
        bool              io6_edit;        // CBS(07) YY argument
        int               io6_curcolumn;   // CBS(07) ZZ argument
        // controller related:
        std::deque<uint8>  crt_buf;
        std::deque<uint8>  printer_buf;
        std::deque<uint8>  line_req_buf;
        std::deque<uint8>  keyboard_buf;
        std::vector<uint8> command_buf;
    } m_terms[MAX_TERMINALS];
    m_term_t              &m_term;         // ref to current term
    int                    m_port;         // currently selected terminal (1-4)
};

#endif // _INCLUDE_IOCARD_TERM_MUX_H_

// vim: ts=8:et:sw=4:smarttab
