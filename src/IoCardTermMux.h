// This is a skeleton file for patterning future I/O devices

#ifndef _INCLUDE_IOCARD_TERM_MUX_H_
#define _INCLUDE_IOCARD_TERM_MUX_H_

#include "IoCard.h"

class Cpu2200;

class IoCardTermMux : public IoCard
{
public:
    // see the base class for the definition of the public functions
    CANT_ASSIGN_OR_COPY_CLASS(IoCardTermMux);

    // ----- common IoCard functions -----
    IoCardTermMux(Cpu2200 &cpu, int baseaddr, int cardslot);
    ~IoCardTermMux();

    vector<int> getAddresses() const override;

    void  reset(int hard_reset=1) override;
    void  select() override;
    void  deselect() override;
    void  OBS(int val) override;
    void  CBS(int val) override;
    int   getIB5() const override;
    void  CPB(bool busy) override;

    // ----- IoCardTermMux specific functions -----
    // ...

private:
    // command byte from 2200 to mux controller.
    // there are other commands for MXE, which is not currently emulated.
    // these commands arrive on 2200 I/O address 06 using a CBS strobe.
    // commands which supply further bytes send them via OBS strobes.
    enum {
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
        CMD_REFILL_LIN_REQ       = 0x09,
        CMD_END_OF_LINE_REQ      = 0x0A,
        CMD_QUERY_LINE_REQ       = 0x0B,
        CMD_ACCEPT_LIN_REQ_DATA  = 0x0C,
        CMD_REQ_CRT_BUFFER       = 0x0D,
        CMD_REQ_PRINT_BUFFER     = 0x0E,
        CMD_ERROR_LINE_REQ       = 0x0F,
        CMD_TERMINATE_LINE_REQ   = 0x10,
    };

    // ---- card properties ----
    const string getDescription() const override;
    const string getName() const override;
    vector<int> getBaseAddresses() const override;

    // ...
    Cpu2200    &m_cpu;            // associated CPU
    const int   m_baseaddr;       // the address the card is mapped to
    const int   m_slot;           // which slot the card is plugged into
    bool        m_selected;       // the card is currently selected
    bool        m_cpb;            // the cpu is busy
    bool        m_card_busy;      // the card is busy doing something

    bool        m_vp_mode;        // true in vp mode, false in mvp mode
    int         m_current_term;   // currently selected terminal (1-4)

    // these are the size of various MXD buffers, as noted in
    // 2236MXE_Documentation.8-83.pdf, p. 5
    static const int CrtBufferSize      = 250;
    static const int PrinterBufferSize  = 160;
    static const int LineReqBufferSize  = 480;
    static const int KeyboardBufferSize =  36;

    struct {
        // display related:
        UI_gui_handle_t   wndhnd;      // opaque handle to UI window
        // keyboard related:
        bool              key_ready;   // key_code is valid
        int               key_code;    // keycode of most recently received keystroke
        // controller related:
        std::queue<uint8> crt_buf;
        std::queue<uint8> printer_buf;
        std::queue<uint8> line_req_buf;
        std::queue<uint8> keyboard_buf;
    } m_term[4];
};

#endif // _INCLUDE_IOCARD_TERM_MUX_H_

// vim: ts=8:et:sw=4:smarttab
