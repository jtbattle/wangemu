// this is class interface for the 2200 CPUs

#ifndef _INCLUDE_CPU2200_H_
#define _INCLUDE_CPU2200_H_

#include "w2200.h"

class Scheduler;
class Timer;

// ============================= base class =============================
class Cpu2200
{
public:
    // destructor
    Cpu2200() : m_status(CPU_HALTED) { };

    // destructor
    virtual ~Cpu2200() { };

    // report which type of CPU is in use
    enum { CPUTYPE_2200B, CPUTYPE_2200T,
           CPUTYPE_VP, CPUTYPE_MVP, CPUTYPE_MVPC, CPUTYPE_MICROVP };
    virtual int getCpuType() const noexcept = 0;

    // true=hard reset, false=soft reset
    virtual void reset(bool hard_reset) noexcept = 0;

    // indicates if cpu is running or halted
    enum { CPU_RUNNING=0, CPU_HALTED=1 };
    int status() const noexcept { return m_status; }

    // the disk controller is odd in that it uses the AB bus to signal some
    // information after the card has been selected.  this lets it peek into
    // that part of the cpu state.
    virtual uint8 getAB() const noexcept = 0;

    // when a card is selected, or its status changes, it uses this function
    // to notify the core emulator about the new status.
    // I'm not sure what the names should be in general, but these are based
    // on what the keyboard uses them for.  data_avail shows up at
    // haltstep:  st3 bit 2 and is used to indicate the halt/step key is pressed.
    //   (perhaps this is always connected and doesn't depend on device selection)
    // ready: st3 bit 0 and is used to indicate the device is ready to accept a
    //        new command (and perhaps has data ready from an earlier command)
    virtual void setDevRdy(bool ready) noexcept = 0;

    // when a card gets an IOhdlr_getbyte and it decides to return the data
    // request, this function is called to return that data.  it also takes
    // care of the necessary low-level handshake emulation.
    virtual void IoCardCbIbs(int data) = 0;

    // run for ticks*100ns
    virtual int execOneOp() = 0;

    // this is a signal that in theory any card could use to set a
    // particular status flag in a cpu register, but the only role
    // I know it is used for is when the keyboard HALT key is pressed.
    virtual void halt() noexcept = 0;

protected:
    int m_status;  // whether the cpu is running or halted

private:
};


// ============================= 2200T cpu =============================
class Cpu2200t: public Cpu2200
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Cpu2200t);
    // ---- see base class for description of these members: ----
    Cpu2200t(std::shared_ptr<Scheduler> scheduler,
             int ramsize, int subtype);
    ~Cpu2200t();
    int   getCpuType() const noexcept override;
    void  reset(bool hard_reset) noexcept override;
    uint8 getAB() const noexcept override;
    void  setDevRdy(bool ready) noexcept override;
    void  IoCardCbIbs(int data) override;
    int   execOneOp() override;  // simulate one instruction
    void  halt() noexcept override;

private:
    // ---- member functions ----

    // store the microcode word to the given microstore address
    void write_ucode(int addr, uint32 uop) noexcept;

    // dump the most important contents of the uP state
    void dump_state(int fulldump);

    // dump a floating point number (16 nibbles)
    void dump_16n(int addr);

    // read from the specified address
    uint8 mem_read(uint16 addr) const noexcept;

    // write to the specified address.
    void mem_write(uint16 addr, uint4 wr_value, int write2) noexcept;

    // reading ST3 is a subroutine because it must return state that wasn't
    // what was written
    uint4 read_st3() const;

    // setting ST1.1 can have more complicated side effects
    void set_st1(uint4 value);

    // store the 4b value to the place selected by the C field.
    // return a flag if the op is illegal.
    int store_C_operand(uint32 uop, uint4 value);

    // add two BCD nibbles
    uint8 decimal_add(uint4 a_op, uint4 b_op, int ci) const noexcept;

    // subtract two BCD nibbles
    uint8 decimal_sub(uint4 a_op, uint4 b_op, int ci) const noexcept;

    // ---- data members ----

    std::shared_ptr<Scheduler>  m_scheduler;  // shared system event scheduler
    const int                   m_cpuType;    // type cpu flavor, eg CPUTYPE_2200T

    // these shouldn't have to be changed; they are just symbolic defines
    // to make the code more readable.
    static const int MAX_RAM   = 32768; // max # bytes of main memory
    static const int MAX_UCODE = 32768; // max # words in ucode store
    static const int MAX_KROM  = 2048;  // max # words in constant rom store

    static const int ICSTACK_SIZE = 16;                // 16 words in return stack
    static const int ICSTACK_TOP  = (ICSTACK_SIZE-1);  // index of top of stack
    static const int ICSTACK_MASK = 0xF;

    typedef struct {
        uint32 ucode;       // 19:0 stores raw ucode word
                            // 24:20 stores the repacked B field specifier
                            // 31:30 stores flags about required operands
        uint8  op;          // predecode: specific instruction
        uint8  p8;          // unused
        uint16 p16;         // predecode: instruction specific
    } ucode_t;

    ucode_t   m_ucode[MAX_UCODE]; // microcode store
    const int m_ucode_size;       // size of ucode store, in words

    uint8     m_kROM[MAX_KROM];   // constant/keyword ROM
    const int m_krom_size;        // size of kROM, in bytes

    // The micromachine uses A[15:0] as a nibble address.
    // We pack RAM[addr][3:0] = {WANGRAM[2*addr+1],WANGRAM[2*addr+0]}
    // That is, each byte of this RAM holds consecutive WANG RAM nibbles,
    // with the lower addressed nibble in the lsbs of the RAM byte.
    const int m_memsize;        // size, in bytes
    uint8     m_RAM[MAX_RAM];

    // this contains the CPU state
    struct cpu2200_t {
        uint16  pc;             // working address ("pc register")
        uint16  aux[16];        // PC scratchpad
        uint8   reg[8];         // eight 4b file registers
        uint16  ic;             // microcode instruction counter
        uint16  icstack[ICSTACK_SIZE];  // microcode subroutine stack
        int     icsp;           // icstack pointer
        uint8   c;              // data memory read register
        uint8   k;              // i/o data register
        uint8   ab;             // i/o address bus latch
        uint8   ab_sel;         // ab at time of last ABS
        uint8   st1;            // status reg 1 state
        uint8   st2;            // status reg 2 state
        uint8   st3;            // status reg 3 state
        uint8   st4;            // status reg 4 state
        bool    prev_sr;        // previous instruction was SR
    } m_cpu;

    // debugging feature
    bool m_dbg;
};


// ============================= 2200VP cpu =============================
class Cpu2200vp: public Cpu2200
{
public:
    CANT_ASSIGN_OR_COPY_CLASS(Cpu2200vp);
    // ---- see base class for description of these members: ----
    Cpu2200vp(std::shared_ptr<Scheduler> scheduler,
              int ramsize, int subtype);
    ~Cpu2200vp();
    int   getCpuType() const noexcept override;
    void  reset(bool hard_reset) noexcept override;
    uint8 getAB() const noexcept override;
    void  setDevRdy(bool ready) noexcept override;
    void  IoCardCbIbs(int data) override;
    int   execOneOp() override;  // simulate one instruction
    void  halt() noexcept override;

    // ---- class-specific members: ----

private:
    // ---- member functions ----
    // predecode uinstruction and write it to store
    void write_ucode(uint16 addr, uint32 uop, bool force=false) noexcept;

    // dump the most important contents of the uP state
    void dump_state(int fulldump);

    // set SH register
    void set_sh(uint8 value);

    // set SL register
    void set_sl(uint8 value) noexcept;

    // 9b result: carry out and 8b result
    uint16 decimal_add8(int a_op, int b_op, int ci) const noexcept;

    // 9b result: carry out and 8b result
    // if ci is 0, it means compute a-b.
    // if ci is 1, it means compute a-b-1.
    // msb of result is new carry bit: 1=borrow, 0=no borrow
    uint16 decimal_sub8(int a_op, int b_op, int ci) const noexcept;

    // return the chosen bits of B and A, returns with the bits
    // of b in [7:4] and the bits of A in [3:0]
    uint8 get_HbHa(int HbHa, int a_op, int b_op) const noexcept;

    // this callback occurs when the 30 ms timeslicing one-shot times out.
    void tcb30msDone() noexcept;

#ifdef HAVE_FILE_DUMP
    void dump_ram(const std::string &filename);
#endif

    // ---- data members ----

    const int                   m_cpu_subtype;
    std::shared_ptr<Scheduler>  m_scheduler;  // shared system timing scheduler object
    bool                        m_hasOneShot; // this cpu supports timeslicing
    std::shared_ptr<Timer>      m_tmr_30ms;   // time slice 30 ms one shot

    static const int MAX_RAM   = 2048*1024; // max # bytes of main memory
    static const int MAX_UCODE =   64*1024; // max # words in ucode store

    static const int STACKSIZE = 96; // number of entries in the return stack

    struct ucode_t {
        uint32 ucode;       // raw ucode word (really 24b)
                            // upper 8b are used to hold flags
        uint8  op;          // predecode: specific instruction
        uint8  p8;          // predecode: instruction specific
        uint16 p16;         // predecode: instruction specific
    } m_ucode[MAX_UCODE];
    int m_ucodeWords;       // number of implemented words

    // main memory
    const int m_memsize;        // size, in bytes
    uint8     m_RAM[MAX_RAM];

    // this contains the CPU state
    struct cpu2200vp_t {
        uint16  pc;             // working address ("pc register")
        uint16  orig_pc;        // copy of pc at start of instruction (not always valid)
        uint16  aux[32];        // PC scratchpad
        uint8   reg[8];         // eight 8b file registers
        uint16  ic;             // microcode instruction counter
        uint16  icstack[STACKSIZE]; // microcode subroutine stack
        int     icsp;           // icstack pointer
        uint8   ch;             // high data memory read register
        uint8   cl;             // low data memory read register
        uint8   k;              // i/o data register
        uint8   ab;             // i/o address bus latch
        uint8   ab_sel;         // ab at time of last ABS
        uint8   sh;             // high status reg
        uint8   sl;             // low  status reg
        int     bank_offset;    // predecoded from sl
    } m_cpu;

    // debugging feature
    bool m_dbg;
};

// microcode disassembly utilities
bool dasm_one   (char *buff, uint16 ic, uint32 ucode);
bool dasm_one_vp(char *buff, uint16 ic, uint32 ucode);

#endif _INCLUDE_CPU2200_H_

// vim: ts=8:et:sw=4:smarttab
