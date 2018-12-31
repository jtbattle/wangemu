// This file contains the logic of the disk controller for IoCardDisk.
// It is broken out into a separate file simply for clarity.
//
// The information was derived from these sources, in chronological order that
// I obtained them, and thus in order of its impact on writing this code:
//
// * Wang 7180 disk controller internal document containing microcode for
//   the floppy disk controller:
//      http://www.wang2200.org/2200tech/mrg-no-2.pdf
//   This, plus many hours of reverse engineering of the microcode and some
//   trial and error modeling in the emulator, was good enough to get the
//   first generation (dumb) disk controller protocol working.
//
// * Paul Szudzik OCR'd a section of the SDS internal documentation on their
//   understanding of the Wang disk channel protocol (not online)
//      disk_protocol/Disk Handshake Sequences.rtf
//   This allowed adding the smart disk protocol to the emulator.
//
// * Wang's internal LVP disk controller document:
//      http://www.wang2200.org/docs/internal/LvpDiskCommandSequences.5-81.pdf
//   This exposed a missing command and described the "read status" extended
//   command format.
//
// This code doesn't emulate any specific controller that Wang made.
// In fact, it even allows things which no Wang controller supported,
// such as arbitrarily mixing floppy and hard disk images on a drive
// by drive basis.  This causes a few corner case issues (should the
// controller claim to be smart or dumb?) but it saves some configuration
// difficulties for the user.

#include "DiskCtrlCfgState.h"
#include "host.h"         // for dbglog()
#include "IoCardDisk.h"
#include "Ui.h"           // for UI_Alert()
#include "Cpu2200.h"
#include "Scheduler.h"
#include "SysCfgState.h"
#include "Wvd.h"

#include <algorithm>      // for std::min, std::max

#ifdef _DEBUG
    extern int iodisk_noisy;
    #define NOISY  (iodisk_noisy) // turn on some alert messages
    extern int iodisk_dbg;
    #define DBG    (iodisk_dbg)   // turn on some debug logging
#else
    #define NOISY  (0)          // turn on some alert messages
    #define DBG    (0)          // turn on some debug logging
#endif
#ifdef _MSC_VER
    #pragma warning( disable: 4127 )  // conditional expression is constant
#endif


// after the card selection phase (ABS), the address bus, AB, can contain
// an arbitrary 8b value without affecting selection.  most cards don't use
// AB after the ABS strobe, but the disk controllers use it as a sideband.
// To initiate a new command, the CPU drives AB to 0xA0 and sends a "wakeup"
// byte to the disk controller, identifying the type of CPU making the
// connection: 00=2200T, 01=VP, 02=MVP.  the disk controller sends back a
// status byte indicating if it is ready, and what its capabilities are.
//
// the cpu then sets AB to 0x40, indicating entry into the command phase.
// AB will stay at this value for the rest of the command, unless something
// goes wrong.  for instance, all command bytes sent by the 2200 are echoed
// back by the disk controller.  if the 2200 receives one of the echo bytes
// and it has an unexpected value, the 2200 must abort the command.  it does
// so by changing the address bus to 0xA0 and retrying the command from the
// start.
//
bool
IoCardDisk::caxInit() noexcept
{
    // return true if AB indicates this is command initiation
    return (m_cpu->getAB() & 0xA0) == 0xA0;
}


std::string
IoCardDisk::stateName(int state) const
{
    switch (state) {
        case CTRL_WAKEUP:               return "CTRL_WAKEUP";
        case CTRL_STATUS1:              return "CTRL_STATUS1";
        case CTRL_GET_BYTES:            return "CTRL_GET_BYTES";
        case CTRL_GET_BYTES2:           return "CTRL_GET_BYTES2";
        case CTRL_SEND_BYTES:           return "CTRL_SEND_BYTES";
        case CTRL_COMMAND:              return "CTRL_COMMAND";
        case CTRL_COMMAND_ECHO:         return "CTRL_COMMAND_ECHO";
        case CTRL_COMMAND_ECHO_BAD:     return "CTRL_COMMAND_ECHO_BAD";
        case CTRL_COMMAND_STATUS:       return "CTRL_COMMAND_STATUS";
        case CTRL_READ1:                return "CTRL_READ1";
        case CTRL_READ2:                return "CTRL_READ2";
        case CTRL_READ3:                return "CTRL_READ3";
        case CTRL_WRITE1:               return "CTRL_WRITE1";
        case CTRL_WRITE2:               return "CTRL_WRITE2";
        case CTRL_VERIFY1:              return "CTRL_VERIFY1";
        case CTRL_VERIFY2:              return "CTRL_VERIFY2";
        case CTRL_COPY1:                return "CTRL_COPY1";
        case CTRL_COPY2:                return "CTRL_COPY2";
        case CTRL_COPY3:                return "CTRL_COPY3";
        case CTRL_COPY4:                return "CTRL_COPY4";
        case CTRL_COPY5:                return "CTRL_COPY5";
        case CTRL_COPY6:                return "CTRL_COPY6";
        case CTRL_COPY7:                return "CTRL_COPY7";
        case CTRL_FORMAT1:              return "CTRL_FORMAT1";
        case CTRL_FORMAT2:              return "CTRL_FORMAT2";
        case CTRL_FORMAT3:              return "CTRL_FORMAT3";
        case CTRL_MSECT_WR_START:       return "CTRL_MSECT_WR_START";
        case CTRL_MSECT_WR_END1:        return "CTRL_MSECT_WR_END1";
        case CTRL_MSECT_WR_END2:        return "CTRL_MSECT_WR_END2";
        case CTRL_VERIFY_RANGE1:        return "CTRL_VERIFY_RANGE1";
        case CTRL_VERIFY_RANGE2:        return "CTRL_VERIFY_RANGE2";
        case CTRL_VERIFY_RANGE3:        return "CTRL_VERIFY_RANGE3";
        case CTRL_VERIFY_RANGE4:        return "CTRL_VERIFY_RANGE4";
        case CTRL_VERIFY_RANGE5:        return "CTRL_VERIFY_RANGE5";
        default: /*assert(false);*/     return "CTRL_???";
    }
}


// indicate if the controller state machine is idle or busy
bool
IoCardDisk::inIdleState() const noexcept
{
    return  (m_state == CTRL_WAKEUP) ||
           ((m_state == CTRL_COMMAND) && (m_state_cnt==0));
}


// report if a given drive is occupied and has media that is suitable
// for the intelligent disk protocol, namely disks with > 32K sectors,
// or multiplatter disks.  these aren't necessarily opposite, as a
// drive might be empty.
bool
IoCardDisk::driveIsSmart(int drive) const
{
    return (drive >= numDrives())                   ||
           (m_d[drive].state == DRIVE_EMPTY)        ||
           (m_d[drive].wvd->getNumPlatters() > 1)   ||
           (m_d[drive].wvd->getNumSectors() > 32768);
}


bool
IoCardDisk::driveIsDumb(int drive) const
{
    return (drive >= numDrives())                    ||
           (m_d[drive].state == DRIVE_EMPTY)         ||
           (m_d[drive].wvd->getNumPlatters() == 1)   ||
           (m_d[drive].wvd->getNumSectors() <= 32768);
}


// helper routine to set the conditions to receive and echo bytes from the host
void
IoCardDisk::getBytes(int count, disk_sm_t return_state) noexcept
{
    m_calling_state = m_state;
    m_return_state  = return_state;
    m_byte_count    = count;
    m_get_bytes_ptr = 0;
    m_state         = CTRL_GET_BYTES;
}


// helper routine to set the conditions to send bytes to the host
void
IoCardDisk::sendBytes(int count, disk_sm_t return_state) noexcept
{
    m_calling_state  = m_state;
    m_return_state   = return_state;
    m_byte_count     = count;
    m_send_bytes_ptr = 0;
    m_state          = CTRL_SEND_BYTES;
}


// this is a centralized place to update emulation state of the disk controller.
// this is event driven.  the events that advance state are:
//    reset                        (event == EVENT_RESET)
//    cpu sent a byte              (event == EVENT_OBS)
//    cpu ready to receive a byte  (event == EVENT_IBS)
bool
IoCardDisk::advanceState(disk_event_t event, const int val)
{
    const bool poll_before = (!m_cpb && !m_card_busy);
    const bool rv = advanceStateInt(event, val);
    const bool poll_after  = (!m_cpb && !m_card_busy);

    if (!poll_before && poll_after) {
        checkDiskReady();  // causes reentrancy to this function
    }

    return rv;
}


// return a pointer to a string describing a known extended command
// which the emulator doesn't support.  return nullptr if it is either
// supported or is unknown.
std::string
IoCardDisk::unsupportedExtendedCommandName(int cmd)
{
    switch (cmd) {
        // known but unsupported
        case SPECIAL_FORMAT_SECTOR:        return "FORMAT_SECTOR";
        case SPECIAL_READ_SECTOR_HEADER:   return "READ_SECTOR_HEADER";
        case SPECIAL_CLEAR_ERROR_COUNT:    return "CLEAR_ERROR_COUNT";
        case SPECIAL_READ_ERROR_COUNT:     return "READ_ERROR_COUNT";
        case SPECIAL_READ_SECTOR_AND_HANG: return "READ_SECTOR_AND_HANG";
        case SPECIAL_READ_STATUS:          return "READ_STATUS";
        case SPECIAL_FORMAT_TRACK:         return "FORMAT_TRACK";
        // supported
        case SPECIAL_FORMAT:
        case SPECIAL_COPY:
        case SPECIAL_MULTI_SECTOR_WRITE_START:
        case SPECIAL_MULTI_SECTOR_WRITE_END:
        case SPECIAL_VERIFY_SECTOR_RANGE:
            return "";
        // unknown
        default:
            return "";
    }
}


bool
IoCardDisk::advanceStateInt(disk_event_t event, const int val)
{
    bool rv = false;  // return value for EVENT_IBS_POLL

    if (DBG > 1) {
        if (event == EVENT_OBS) {
            dbglog("State %s, received OBS(0x%02x)\n", stateName(m_state).c_str(), val);
        } else {
            std::string msg;
            switch (event) {
                case EVENT_RESET:     msg = "EVENT_RESET";     break;
                case EVENT_OBS:       msg = "EVENT_OBS";       break;
                case EVENT_IBS_POLL:  msg = "EVENT_IBS_POLL";  break;
                case EVENT_DISK:      msg = "EVENT_DISK";      break;
                default:              msg = "???";             break;
            }
            dbglog("State %s, received %s\n", stateName(m_state).c_str(), msg.c_str());
        }
    }

    // init things on reset
    if (event == EVENT_RESET) {
        if (DBG > 2) { dbglog("Reset\n"); }
        m_state = CTRL_WAKEUP;
        m_selected = false;
        setBusyState(false);
        return rv;
    }

    // the 2200 sets the address bus to 0xA0 to initiate the command
    // sequence.  this happens in normal conditions, but it can also
    // happen if the 2200 detects a problem in the handshake in order
    // to abort whatever command is going on.
    if (event == EVENT_OBS && caxInit()) {
        if (!inIdleState()) {
            // we are aborting something in progress
            if (DBG > 0) {
                dbglog("Warning: CAX aborted command state %s, cnt=%d\n",
                       stateName(m_state).c_str(), m_bufptr);
            }
        }
        m_state = CTRL_WAKEUP;
        setBusyState(false);
    }

    // this is for diagnostic purposes only
    // if a state which isn't expecting an OBS gets one, report it
    bool expecting_obs = false;
    switch (m_state) {
        case CTRL_WAKEUP:
        case CTRL_GET_BYTES:
        case CTRL_COMMAND:
        case CTRL_READ1:
        case CTRL_WRITE1:
        case CTRL_VERIFY1:
        case CTRL_COPY4:
        case CTRL_FORMAT1:
        case CTRL_MSECT_WR_START:
        case CTRL_MSECT_WR_END1:
        case CTRL_VERIFY_RANGE3:
            expecting_obs = true;
            break;
        default:
            expecting_obs = false;
            break;
    }
    if (!expecting_obs && (event == EVENT_OBS)) {
        if (NOISY > 0) {
            UI_info("Unexpected OBS in state %s", stateName(m_state).c_str());
        }
    }

    switch (m_state) {

    // ---------------------------- WAKEUP ----------------------------

    // in this state we waiting for the start of a command sequence.
    // for us to receive an OBS strobe implies we've already been selected.
    // We expect the CAX condition (namely, AB=0xA0, although at least some
    // of the early disk controllers only ensure A8=A6=1).  If CAX isn't
    // true, something is wrong.
    // The data sent along with the CAX && OBS condition is:
    //
    //    0x00: Model 'T' hardware or PROM mode on other 2200 system.
    //          Data transmission should be in slow mode.
    //
    //    0x01: 2200 VP machine.  Use fast data transmission mode.
    //
    //    0x02: 2200 MVP machine.  Use fast data transmission mode.
    //
    case CTRL_WAKEUP:
        assert(m_card_busy == false);
        if (event == EVENT_OBS) {
            if (caxInit()) {
                // we must be selected if we got the OBS
                setBusyState(false);
                m_host_type = val;
                // we act dumb if configured that way, or if host is 2200T
                switch (m_host_type) {
                    case 0x00: // 2200 T
                        m_acting_intelligent = false;
                        break;
                    case 0x01: // 2200 VP
                    case 0x02: // 2200 MVP
                    default: // GIO can send anything; assume non-zero is intelligent
                        switch (intelligence()) {
                            default:
                                assert(false);
                                // fall through
                            case DiskCtrlCfgState::DISK_CTRL_DUMB:
                                m_acting_intelligent = false;
                                break;
                            case DiskCtrlCfgState::DISK_CTRL_INTELLIGENT:
                                m_acting_intelligent = true;
                                break;
                            case DiskCtrlCfgState::DISK_CTRL_AUTO:
                                // if we know that all occupied drives are
                                // dumb, or all are smart, there is a clear
                                // answer to give.  if there is a mix of disks,
                                // we don't know the right choice until after
                                // we've received the command (and remember
                                // that COPY addresses two different drives,
                                // which might be a mix of dumb and smart),
                                // but that comes after this WAKEUP phase.
                                if (driveIsDumb(0) && driveIsDumb(1) &&
                                    driveIsDumb(2) && driveIsDumb(3)) {
                                    m_acting_intelligent = false;
                                } else if (driveIsSmart(0) && driveIsSmart(1) &&
                                           driveIsSmart(2) && driveIsSmart(3)) {
                                    m_acting_intelligent = true;
                                } else {
                                    // who knows what will happen?
                                    // hope for the best...
                                    m_acting_intelligent = true;
                                }
                                break;
                        }
                        break;
                }
                if ((NOISY > 0) && (m_host_type > 0x02)) {
                    UI_warn("CTRL_WAKEUP got bad host type of 0x%02x", val);
                }
                m_state = CTRL_STATUS1;
            } else {
                if (NOISY > 0) {
                    UI_warn("Unexpected cax condition in WAKEUP state");
                }
            }
        }
        break;

    // the controller is expected to tell the 2200 whether it is operational,
    // and whether it is a dumb or intelligent controller.
    //    0x01 == drive error (eg, it failed self-diagnostics)
    //            this results in an I90 error code.
    //    0xC0 == dumb controller
    //    0xD0 == smart controller
    case CTRL_STATUS1:
        assert(m_card_busy == false);
        if (event == EVENT_IBS_POLL) {
            // indeed, we have data
            m_byte_to_send = (m_acting_intelligent) ? 0xD0 : 0xC0;
            rv = true;
            m_state_cnt = 0;    // accumulate command header bytes
            m_state = CTRL_COMMAND;
        }
        break;

    // --------------------------- GET_BYTES ---------------------------
    // This subroutine waits for m_byte_count bytes to arrive, and echoes
    // each one back to the host.  the bytes are saved in m_data_get[].
    // When all the bytes have been received, it returns to m_return_state.

    case CTRL_GET_BYTES:
        assert(m_card_busy == false);
        if (event == EVENT_OBS) {
            m_get_bytes[m_get_bytes_ptr] = static_cast<uint8>(val);
            m_state = CTRL_GET_BYTES2;
        }
        break;

    case CTRL_GET_BYTES2:
        if (event == EVENT_IBS_POLL) {
            rv = true;  // we have data to return
            m_byte_to_send = m_get_bytes[m_get_bytes_ptr++];
            m_state = (m_get_bytes_ptr < m_byte_count) ? CTRL_GET_BYTES
                                                       : m_return_state;
        }
        break;

    // --------------------------- SEND_BYTES ---------------------------
    // This subroutine sends m_byte_count bytes to the host.
    // The bytes come from m_data_send[].
    // When all the bytes have been received, it returns to m_return_state.

    case CTRL_SEND_BYTES:
        assert(m_card_busy == false);
        if (event == EVENT_IBS_POLL) {
            rv = true;
            m_byte_to_send = m_send_bytes[m_send_bytes_ptr++];
            if (m_send_bytes_ptr >= m_byte_count) {
                m_state = m_return_state;
            }
        }
        break;

    // ---------------------------- COMMAND ----------------------------

    // The address bus will be 0x40 (instead of 0xA0) now.
    // We are receiving a sequence of header bytes, each of which is echoed
    // back to the 2200 as an integrity check.  For normal commands, the
    // bytes are:
    //
    //     byte 0: command byte
    //     byte 1: 1st sector byte  (most significant)
    //     byte 2: 2nd sector byte
    //     byte 3: 3rd sector byte  (only for intelligent disk controllers)
    //
    // Byte 0, the command byte, has these packed fields:
    //
    //     C C C R   H H H H
    //
    //     CCC = 000: read command
    //         = 010: write command
    //         = 100: read after write
    //         = 001: special command (intelligent controllers only)
    //
    //     R = 0: fixed drive
    //       = 1: removable drive
    //
    //     HHHH = head (platter) for drives with addressable platters
    //
    // If this is a special command, byte 1 contains the special command
    // encoding.  The number of meaning of the bytes after that depend
    // on which special command is being processed.
    //
    case CTRL_COMMAND:
        assert(m_card_busy == false);
        if (event == EVENT_OBS) {
            m_header[m_state_cnt] = static_cast<uint8>(val);
            m_state = CTRL_COMMAND_ECHO;
            if (m_state_cnt == 0) {
                m_command =  (val >> 5) & 7;
                m_drive   = ((val >> 4) & 1) + ((m_primary) ? 0 : 2);
                m_platter =  (val >> 0) & 15;
                // how many subsequent command bytes to accumulate.
                // this may be overridden later.
                m_xfer_length =
                    (m_acting_intelligent) ? 4  // cmd, 3 secaddr bytes
                                           : 3; // cmd, 2 secaddr bytes
#if 0
                // check that the disk in the indicated drive is compatible
                // with the intelligence level we are operating at
                if (m_acting_intelligent && driveIsDumb(m_drive)) {
// this is a good point to complain, but we don't want to complain on every
// disk operation
                } else if (!m_acting_intelligent && driveIsSmart(m_drive)) {
// this is a good point to complain, but we don't want to complain on every
// disk operation
                }
#endif
            } else if (m_state_cnt == 1 && m_command == CMD_SPECIAL) {
                // CMD_SPECIAL has variable length headers
                m_special_command = m_header[1];
                switch (m_special_command) {
                case SPECIAL_COPY:
                case SPECIAL_VERIFY_SECTOR_RANGE:
                case SPECIAL_FORMAT_TRACK:
                        // command, subcommand, 3 secaddr bytes (start sector)
                        m_xfer_length = 5;
                        break;
                case SPECIAL_FORMAT:
                case SPECIAL_MULTI_SECTOR_WRITE_START:
                case SPECIAL_MULTI_SECTOR_WRITE_END:
                        // command, subcommand
                        m_xfer_length = 2;
                        break;
#if 0
                case SPECIAL_READ_STATUS:
                        m_xfer_length = 2;
                        break;
#endif
                default: {
                    static bool reported[256] = { false };
                    if (!reported[m_special_command]) {
                        std::string msg = unsupportedExtendedCommandName(m_special_command);
                        if (msg != "") {
                            UI_warn("ERROR: disk controller received unimplemented special command 0x%02x (%s)\n"
                                    "Please notify the program developer if you want this feature added",
                                     m_special_command, msg.c_str());
                        } else {
                            UI_warn("ERROR: disk controller received unknown special command 0x%02x",
                                     m_special_command);
                        }
                        reported[m_special_command] = true;
                    }
                    m_state = CTRL_COMMAND_ECHO_BAD;
                    }
                    break;
                }
            }
        }
        break;

    // every command byte we receive is echo back for integrity checks
    case CTRL_COMMAND_ECHO_BAD:  // special case; fall through
    case CTRL_COMMAND_ECHO:
        if (event == EVENT_IBS_POLL) {
            const bool bad_spcl_cmd = (m_state == CTRL_COMMAND_ECHO_BAD);
            rv = true;  // we have data to return
            m_state = CTRL_COMMAND;  // may be overridden
            m_byte_to_send = m_header[m_state_cnt++];
            if (bad_spcl_cmd) {
                // page 3 of LvpDiskCommandSequences.5-81.pdf says if the
                // command isn't recognized, the DPU echoes the command byte
                // bit inverted.  The host should abort the command.
                m_byte_to_send ^= 0xFF;
            } else if (m_state_cnt == m_xfer_length) {
                m_state_cnt = 0;        // prepare it for the next command

                // header is complete -- decode it, presuming READ, WRITE, VERIFY
                if (m_acting_intelligent) {
                    m_secaddr = (m_header[1] << 16) |
                                (m_header[2] <<  8) |
                                 m_header[3];
                } else {
                    m_secaddr = (m_header[1] << 8) | m_header[2];
                }

                if (m_command == CMD_SPECIAL) {

                    // some, but not all commands, expect sector data here
                    m_secaddr = (m_header[2] << 16) |
                                (m_header[3] <<  8) |
                                 m_header[4];

                    // a COPY is supposed to always be followed by READ
                    if (m_copy_pending != false) {
                        UI_warn("Disk controller got unexpected command following COPY\n"
                                 "Ignoring the COPY command");
                        m_copy_pending = false;
                    }

                    switch (m_special_command) {
                    case SPECIAL_COPY:
                            m_state = CTRL_COPY1;
                            break;
                    case SPECIAL_FORMAT:
                            m_state = CTRL_FORMAT1;
                            break;
                    case SPECIAL_MULTI_SECTOR_WRITE_START:
                            m_state = CTRL_MSECT_WR_START;
                            break;
                    case SPECIAL_MULTI_SECTOR_WRITE_END:
                            m_state = CTRL_MSECT_WR_END1;
                            break;
                    case SPECIAL_VERIFY_SECTOR_RANGE:
                            m_range_platter = m_platter;
                            m_range_drive   = m_drive;
                            m_range_start   = m_secaddr;
                            m_state = CTRL_VERIFY_RANGE1;
                            break;
                    default:
                        assert(false);
                        m_state = CTRL_COMMAND;
                        break;
                    }

                } else if (m_command == CMD_READ && m_copy_pending) {
                    // a COPY command should be followed by a READ command.
                    // the READ is really a means of providing more parameters.
                    m_dest_drive   = m_drive;
                    m_dest_platter = m_platter;
                    m_dest_start   = m_secaddr;
                    m_copy_pending = false;
                    m_command      = CMD_SPECIAL;  // tcbTrack cares about this
                    m_state = CTRL_COPY3;

                } else {
                    // (m_command != CMD_SPECIAL) -- READ, WRITE, or VERIFY
                    assert(m_copy_pending == false);

                    m_state = CTRL_COMMAND_STATUS;

                    // spin up the drive (if req'd); step to the target track
                    if ((m_drive >= numDrives()) ||  // non-existent
                        (m_secaddr >= m_d[m_drive].wvd->getNumSectors())) {
                        setBusyState(false);  // empirically, returns immediately
                    } else {
                        // even if empty, we wait for motor to spin up
                        setBusyState(true);
                        wvdStepToTrack();
                        wvdTickleMotorOffTimer();
                    }
                } // not CMD_SPECIAL
            }
        }
        break;

    // return status byte, indicating if the disk controller is ready to
    // carry out the requested command
    case CTRL_COMMAND_STATUS:

        if (event == EVENT_DISK) {
            assert(m_card_busy == true);
            // provoke IBS
            setBusyState(false);
        } else if (event == EVENT_IBS_POLL) {
            if ((m_drive >= numDrives()) ||
                (m_platter >= m_d[m_drive].wvd->getNumPlatters())) {
                // sudzik doc states:
                //    0x02 -> I91, if drive is not in ready state,
                //                 or if the head selection isn't legal
                m_byte_to_send = 0x02;
            } else if (m_secaddr >= m_d[m_drive].wvd->getNumSectors()) {
                // 0x01 -> ERR 64 (2200T) or I98 (VP) : sector not on disk
                m_byte_to_send = 0x01;
            } else if (m_d[m_drive].state == DRIVE_EMPTY) {
                m_byte_to_send = 0x01;      // sector not on disk
            } else {
                m_byte_to_send = 0x00;  // OK
                // other values tested out on 2200T emulator:
                //    0x02 -> ERR 65 (disk hardware malfunction)
                //    0x04 -> ERR 66 (format key engaged)
            }
            rv = true;

            m_bufptr = 0;  // we'll be reading or writing it shortly
            if (m_byte_to_send != 0x00) {
                // we've bailed out
                m_state = CTRL_COMMAND;
            } else  {
                // let the UI know that selection might have changed
                for (int d=0; d<numDrives(); d++) {
                    UI_diskEvent(m_slot, d);
                }

                switch (m_command) {
                case CMD_READ:
                    if (DBG > 1) dbglog("CMD: CMD_READ, drive=%d, head=%d, sector=%d\n",
                                        m_drive, m_platter, m_drive, m_secaddr);
                    m_state = CTRL_READ1;
                    break;
                case CMD_WRITE:
                    if (DBG > 1) dbglog("CMD: CMD_WRITE, drive=%d, head=%d, sector=%d\n",
                                        m_drive, m_platter, m_secaddr);
                    m_state = CTRL_WRITE1;
                    break;
                case CMD_VERIFY:
                    if (DBG > 1) dbglog("CMD: CMD_VERIFY, drive=%d, head=%d, sector=%d\n",
                                        m_drive, m_platter, m_secaddr);
                    m_compare_err = false;
                    m_state = CTRL_READ1;  // yes, READ1 -- it shares logic
                    break;
                default:
                    assert(false);
                    m_state = CTRL_COMMAND;
                    break;
                }
            }
        }
        break;

    // ---------------------------- READ ----------------------------

    // after the 2nd status byte, the 2200 sends a byte with unknown purpose.
    // perhaps it is simply a chance for the 2200 to cancel the command,
    // as a fair amount of time may have passed due to motor spin-up and such.
    //
    // LvpDiskCommandSequences.5-81.pdf comments that the purpose of this byte
    // is "signal disk to check IOBs to insure not restarting disk sequence."
    //
    // NB: LvpDiskCommandSequences mentions on page 4 that even if an error
    //     code is returned at this point, the controller should proceed to
    //     deliver the questionable data if the host CPU requests it.  It is
    //     up to the CPU to decide whether to abort the sequence or not.
    case CTRL_READ1:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("CTRL_READ1 received mystery byte of 0x%02x", val);
            }
            m_state = CTRL_READ2;
            const bool ok = iwvdReadSector();  // really read the data
            m_byte_to_send = (ok) ? 0x00 : 0x01;
            // 0x00 = status OK
            // 0x01 -> ERR 71/I95  (cannot find sector/protected platter)
            // 0x02 -> ERR 67/I93  (disk format error)
            // 0x04 -> ERR 72/I96  (cyclic read error)
            setBusyState(false);
        }
        break;

    // by now the sector that was requested has been read off the disk,
    // and we return a status code indicating if it was successful
    case CTRL_READ2:
        if (event == EVENT_IBS_POLL) {
            // send status byte after having read the data
            rv = true;
            m_bufptr = 0;   // next byte to send
            // up to now, compare has shared read's path
            m_state = (m_command == CMD_READ) ? CTRL_READ3
                                              : CTRL_VERIFY1;
        }
        break;

    // return all the bytes that were read, including the final LRC byte
    case CTRL_READ3:
        rv = true;
        m_byte_to_send = m_buffer[m_bufptr++];   // next byte
        m_state = (m_bufptr < 257) ? CTRL_READ3 : CTRL_COMMAND;
        break;

    // ---------------------------- WRITE ----------------------------

    // expect to receive 256 data bytes plus one LRC byte
    case CTRL_WRITE1:
        if (event == EVENT_OBS) {
            m_buffer[m_bufptr++] = static_cast<uint8>(val);
            if (m_bufptr == 257) {
                int cksum = 0;
                for (int i=0; i<256; i++) {
                    cksum += m_buffer[i];
                }
                cksum &= 0xFF;  // LRC
                if (cksum != val) {
                    m_byte_to_send = 0x04;  // error status
                    // 0x00 -> OK
                    // 0x01 -> ERR 71  (cannot find sector/protected platter)
                    // 0x02 -> ERR 67  (disk format error)
                    // 0x04 -> ERR 72  (cyclic read error)
                    // or
                    // 00 if OK
                    // 01 if seek error   (ERR I95)
                    // 02 if format error (ERR I93)
                    // 04 if LRC error    (ERR I96)
                } else if (m_d[m_drive].wvd->getWriteProtect()) {
                    // 0x01 -> ERR 71  (cannot find sector/protected platter)
                    m_byte_to_send = 0x01;  // error status
                } else {
                    // actually update the virtual disk
                    const bool ok = iwvdWriteSector();
                    m_byte_to_send = (ok) ? 0x00 : 0x02;
                }
                // finished receiving data and LRC, send status byte
                // after the sector has been reached
                m_state = CTRL_WRITE2;
                if (realtimeDisk()) {
                    setBusyState(true);
                    wvdSeekSector();    // we are already on the right track
                } else {
                    setBusyState(false);
                }
            } // if (last byte of transfer)
        } // OBS
        break;

    case CTRL_WRITE2:
        if (event == EVENT_DISK) {
            m_state = CTRL_WRITE2;
            setBusyState(false);
        } else if (event == EVENT_IBS_POLL) {
            rv = true;  // value to return was set in WRITE1
            m_state = CTRL_COMMAND;
        }
        break;

    // ---------------------------- VERIFY ----------------------------

    case CTRL_VERIFY1:
        if (event == EVENT_OBS) {
            // check incoming data against the sector data we read;
            // the 257th byte is an LRC on the host data
            if ((m_bufptr < 256) &&         // check just the data
                (m_buffer[m_bufptr] != val)) {
                m_compare_err = true;       // mismatch
            }
            m_bufptr++;
            if (m_bufptr == 257) {
                // finished receiving data and LRC, now send status byte
                m_byte_to_send = (m_compare_err) ? 0x01 : 0x00;
                // 0x00 -> OK
                // 0x01,0x02,0x03,0x04,0x08,0x10 -> ERR 85 (read after write failure)
                // 0x20,0x40,0x80 -> like 0x00
            #if 1
                // this is the right thing to do, although the disk controller
                // microcode in the Module Repair Guide #2 ignores the LRC byte
                int cksum = 0;
                for (int i=0; i<256; i++) {
                    cksum += m_buffer[i];
                }
                cksum &= 0xFF;
                if (cksum != val) {
                    m_byte_to_send = 0x04;
                }
            #endif
                m_state = CTRL_VERIFY2;
            }
        }
        break;

    case CTRL_VERIFY2:
        if (event == EVENT_IBS_POLL) {
            rv = true;  // return m_byte_to_send from previous state
        }
        m_state = CTRL_COMMAND;
        break;

    // ------------------------------- COPY -------------------------------
    // the copy command copies a range of sectors from one platter to
    // another location on the same platter, or to a different platter.
    // these copies are done within the controller, without shuffling
    // the data through the 2200 processor.
    //
    // the command sequence is somewhat complicated.  first, the source
    // start and end sectors are communicated:
    //
    //     receive
    //        byte 0: <special command, source drive, head>
    //        byte 1: <special command "copy" token>
    //        byte 2-4: source start sector
    //     send status
    //        00=ok, 01=bad sector address, 02=not ready or bad head selection
    //     receive
    //        byte 5-7: source end sector
    //     send status
    //        00=ok, 01=bad sector address, 02=not ready or bad head selection
    //
    // next, the controller is re-addressed, and a read command sequence
    // is issued, but with the following interpretation:
    //     receive:
    //        byte 0: <normal read command, dest drive, head>
    //        byte 1-3: source start sector
    //     send status:
    //        00=ok, 01=bad sector range, 02=not ready or bad head selection
    //     receive:
    //        00 byte
    //     drop ready, complete the copy
    //     raise ready, send status:
    //        00=ok
    //        01=dest is write protected or seek error (ERR I95)
    //        02=format error (ERR I93)
    //        04=CRC error    (ERR I96)
    //
    // As we are modeling an intelligent disk controller, assume there is
    // a source track buffer and a dest track buffer to minimize on the
    // amount of head shuttling.  For the 2280, that would be two 16KB
    // buffers, not unreasonable for the 1979 date that it was introduced.
    //
    // The copy algorithm approximates what a real disk controller would do,
    // and doesn't attempt to do a sector-by-sector modeling.
    //
    // 1) select source disk
    //    set a timer to seek to the next source track, plus one revolution
    // 2) select dest disk
    //    set a timer to seek to the next dest track, plus one revolution
    // 3) read all of the sectors in the source track, copy them to their
    //    destination for real.  increment source track counter,
    //    and return to step #1 or drop busy and quit

    // send status after the source start
    case CTRL_COPY1:
        m_range_drive   = m_drive;
        m_range_platter = m_platter;
        m_range_start   = m_secaddr;
        if (event == EVENT_IBS_POLL) {
            rv = true;
            if (m_drive >= numDrives()) {
                m_byte_to_send = 0x01;
            } else {
                const int num_platters = m_d[m_drive].wvd->getNumPlatters();
                const int num_sectors  = m_d[m_drive].wvd->getNumSectors();
                m_byte_to_send = (m_d[m_drive].state == DRIVE_EMPTY) ? 0x01
                               : (m_range_start   >= num_sectors)    ? 0x01
                               : (m_range_platter >= num_platters)   ? 0x02
                                                                     : 0x00;
            }
            if (m_byte_to_send == 0x00) {
                getBytes(3, CTRL_COPY2);
            } else {
                m_state = CTRL_COMMAND;
            }
        }
        break;

    // look at the source end sector and return status
    case CTRL_COPY2:
        m_range_end = (m_get_bytes[0] << 16) |
                      (m_get_bytes[1] <<  8) |
                       m_get_bytes[2];
        if (event == EVENT_IBS_POLL) {
            rv = true;
            const int num_sectors = m_d[m_drive].wvd->getNumSectors();
            m_byte_to_send = (m_d[m_drive].state == DRIVE_EMPTY) ? 0x01
                           : (m_range_end >= num_sectors)        ? 0x01
                                                                 : 0x00;
            m_copy_pending = (m_byte_to_send == 0x00);
        }
        // we now return to normal command interpretation.
        // if the next command is a read and m_copy_pending is true,
        // the story continues at CTRL_COPY3.
        m_state = CTRL_COMMAND;
        break;

    // the header of the READ command contains the destination of the copy.
    // we must ensure the command is legal, and return status indicating this.
    case CTRL_COPY3:
        if (event == EVENT_IBS_POLL) {
            const int sector_count = m_range_end - m_range_start + 1;
            const int final_dst = m_secaddr + sector_count - 1;
            rv = true;
            if (m_drive >= numDrives()) {
                m_byte_to_send = 0x01;
            } else {
                const int num_platters = m_d[m_drive].wvd->getNumPlatters();
                const int num_sectors  = m_d[m_drive].wvd->getNumSectors();
                m_byte_to_send = (m_d[m_drive].state == DRIVE_EMPTY) ? 0x01
                               : (final_dst >= num_sectors)          ? 0x01
                               : (m_platter >= num_platters)         ? 0x02
                                                                     : 0x00;
            }
            m_state = CTRL_COPY4;
        }
        break;

    // we expect to receive a 0x00 byte here from the 2200.
    // the copy doesn't start until we get this token.
    case CTRL_COPY4:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("CTRL_COPY4 received mystery byte of 0x%02x", val);
            }
            if (m_d[m_drive].wvd->getWriteProtect()) {
                m_byte_to_send = 0x01;  // signal write protect
                m_state = CTRL_COPY7;
            } else {
                setBusyState(true);
                for (int d=0; d<numDrives(); d++) {
                    UI_diskEvent(m_slot, d);
                }
                m_drive = m_range_drive;
                m_state = CTRL_COPY5;
                // seek the first track
                m_drive   = m_range_drive;
                m_secaddr = m_range_start;
                wvdStepToTrack();
            }
        }
        break;

    // the source head has reached the target track.
    // model the delay of one revolution for reading the source track,
    // plus the seek time for the destination track.
    // this is OK if src and dst are on the same disk pack, but if they are
    // separate drives, a truly intelligent controller would overlap the seek.
    // on the other hand, if they are on separate drives, the seek times are
    // minimal after the first one (always to adjacent track).
    case CTRL_COPY5:
        {
            // model the delay of one revolution for reading the source track,
            // plus the delay of stepping to the destination track.
            // this isn't right if the src and dst platters have a different
            // number of sectors/track.
            const int64 src_ns_per_trk = m_d[m_range_drive].ns_per_sector
                                       * m_d[m_range_drive].sectors_per_track;
            const int dst_cur_track = m_dest_start
                                    / m_d[m_dest_drive].sectors_per_track;

            // wvdGetNsToTrack() and wvdSeekTrack() need m_drive set
            m_drive = m_dest_drive;
            for (int d=0; d<numDrives(); d++) {
                UI_diskEvent(m_slot, d);
            }

            const int64 delay = src_ns_per_trk  // time reading source track
                              + wvdGetNsToTrack(dst_cur_track);  // seeking dst track
            m_d[m_dest_drive].track = dst_cur_track;

            m_state = CTRL_COPY6;
            wvdTickleMotorOffTimer();  // make sure motor keeps going
            wvdSeekTrack(delay);
        }
        break;

    // we get called when the destination track has been reached.
    // in this state we actually carry out the copy of all the sectors from
    // the source track to the dest disk.  to keep things simple, we ignore
    // what happens if the src and dst disks don't have the same sectors/track.
    case CTRL_COPY6:
        {
            const int src_sec_per_trk    = m_d[m_range_drive].sectors_per_track;
            const int src_cur_track      = m_range_start / src_sec_per_trk;
            const int first_sec_of_track = src_cur_track * src_sec_per_trk;
            const int last_sec_of_track  = first_sec_of_track + src_sec_per_trk - 1;
            const int64 dst_ns_per_trk   = m_d[m_dest_drive].ns_per_sector
                                         * m_d[m_dest_drive].sectors_per_track;
            const int first = std::max(m_range_start, first_sec_of_track);
            const int last  = std::min(m_range_end,    last_sec_of_track);
            const int count = last - first + 1;

            // copy the source track to the destination track(s)
            bool ok = true;
            m_byte_to_send = 0x00;
            uint8 data[256];

            for (int n=0; ok && (n < count); n++) {
                ok = m_d[m_range_drive].wvd->readSector
                                    (m_range_platter, m_range_start+n, &data[0]);
                if (!ok) {
                    m_byte_to_send = 0x02;  // generic error
                } else if (m_d[m_drive].wvd->getWriteProtect()) {
                    m_byte_to_send = 0x01;  // write protect
                } else {
                    ok = m_d[m_dest_drive].wvd->writeSector
                                    (m_dest_platter, m_dest_start+n, &data[0]);
                    if (!ok) {
                        m_byte_to_send = 0x02;  // generic error
                    }
                }
            }

            // update sector pointers with number of sectors copied
            m_range_start += count;
            m_dest_start  += count;

            // model the delay of one revolution for writing the dest track,
            // plus whatever delays are incurred for stepping to the next
            // source track
            if (ok && (m_range_start <= m_range_end)) {
                m_state = CTRL_COPY5;
                // account for one rotation of disk, plus step time
                for (int d=0; d<numDrives(); d++) {
                    UI_diskEvent(m_slot, d);
                }
                m_drive = m_range_drive;
                const int64 delay = dst_ns_per_trk
                                  + wvdGetNsToTrack(src_cur_track+1);
                m_d[m_drive].track = src_cur_track+1;
                wvdTickleMotorOffTimer();  // make sure motor keeps going
                wvdSeekTrack(delay);
            } else {
                // either success or failure
                m_state = CTRL_COPY7;
                setBusyState(false);
            }
        }
        break;

    // everything is done; we must return final status
    // 00=ok, 01=write protect, 02=format (or other) error
    // (set in previous state)
    case CTRL_COPY7:
        if (event == EVENT_IBS_POLL) {
            rv = true;
        }
        m_state = CTRL_COMMAND;
        break;

    // ------------------------------ FORMAT ------------------------------
    // the dumb disk controllers don't have a software-controlled mechanism
    // for formatting a drive.  in some ways, this is good, since there is
    // no way for an errant program to format a drive.  in such systems a
    // front panel key was used to format a disk in the drive.
    //
    // in the smart disk controllers, the formatting process also detected
    // bad sectors and remapped them to spare sectors on the drive.  in this
    // emulation, there is no such thing as a bad sector, so there is no
    // such mapping.
    //
    // the command stream looks like this:
    //     receive
    //        byte 0: <special command, source drive, head>
    //        byte 1: <special command "format" token>
    //        byte 2: unused 00 byte  (not echoed!)
    //     drop ready, perform operation
    //     raise ready, send status:
    //        00=ok
    //        01=seek error   (ERR I95)
    //        02=format error (ERR I93)
    //        04=CRC error    (ERR I96)
    //
    // We model the format operation's timing a track at a time for simplicity.
    //
    // 1) select disk.
    //    set a timer to seek to the next track, plus one disk revolution
    // 2) erase all sectors of the track.
    //    increment track counter, and return to step 1 or drop busy and quit

    // we have received CMD_SPECIAL and the SPECIAL_FORMAT bytes, and are
    // expecting the 0x00 byte. the 0x00 bytes it isn't echoed.
    case CTRL_FORMAT1:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("FORMAT1 was expecting a 0x00 padding byte, but got 0x%02x", val);
            }
            if (m_drive >= numDrives()) {
                // bad drive selection
                m_byte_to_send = 0x01;
                m_state = CTRL_FORMAT3;
            } else {
                setBusyState(true);
                m_state = CTRL_FORMAT2;
                // seek track 0
                m_secaddr = 0;  // spoof it
                wvdStepToTrack();
            }
        }
        break;

    // write to all the sectors of the current track
    case CTRL_FORMAT2:
        if (event == EVENT_DISK) {
            const int   tracks      = m_d[m_drive].tracks_per_platter;
            const int   sec_per_trk = m_d[m_drive].sectors_per_track;
            const int64 ns_per_trk  = m_d[m_drive].ns_per_sector
                                    * sec_per_trk;
            bool ok = true;
            m_byte_to_send = 0x00;

            if (m_d[m_drive].wvd->getWriteProtect()) {
                // return with a write protect error
                m_state = CTRL_FORMAT3;
                m_byte_to_send = 0x01;
                ok = false;
            } else {
                // fill all sectors with 0x00
                uint8 data[256];
                memset(&data[0], static_cast<uint8>(0x00), 256);
                for (int n=0; ok && n<sec_per_trk; n++) {
                    ok = m_d[m_drive].wvd->writeSector(m_platter, n, &data[0]);
                }
                if (!ok) {
                    m_byte_to_send = 0x02;
                }
            }

            const int next_track = m_d[m_drive].track + 1;
            if (ok && (next_track < tracks)) {
                m_state = CTRL_FORMAT2; // stay
                // account for one rotation of disk, plus step time
                const int64 delay = ns_per_trk + wvdGetNsToTrack(next_track);
                m_d[m_drive].track = next_track;
                wvdTickleMotorOffTimer();  // make sure motor keeps going
                wvdSeekTrack(delay);
            } else {
                // either failure or complete
                m_state = CTRL_FORMAT3;
                setBusyState(false);
            }
        }
        break;

    // everything is done; we must return final status
    // 00=ok, 01=write protect, 02=formatting error
    case CTRL_FORMAT3:
        if (event == EVENT_IBS_POLL) {
            rv = true;
        }
        m_state = CTRL_COMMAND;
        break;

    // --------------------- MULTI-SECTOR WRITE START ---------------------
    // this is a performance hint, indicating the controller should expect
    // a number of consecutive writes to the same platter.  the controller
    // doesn't have to honor this request.  the intent is that all these
    // writes will be buffered, and then either at an opportune time, or
    // when forced, all will get written efficiently.  for instance, if
    // N writes in a row all map to the same cylinder, they could all be
    // buffered until it was time to move the head, at which point they
    // could be streamed out in an optimal order.
    //
    // there are complications: the idea of "optimal" is heuristic, and
    // depends on future behavior.  Although the OS intends to use this
    // hint wisely, $GIO commands can specify this hint yet do things
    // in an arbitrary order.  in that case, if an error occurs writing
    // the cached sectors, it may end up associated with the interloping
    // command, instead of the original write.  the user may eject a drive
    // (at least on the emulator) at an arbitrary time.
    //
    // therefore, this emulator will simply parse the command but ignore it.
    //
    // the command stream looks like this:
    //     receive
    //        byte 0: <special command, source drive, head>
    //        byte 1: <special command "start multisector write mode" token>
    //        byte 2: unused 00 byte  (not echoed!)

    case CTRL_MSECT_WR_START:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("MULTI-SECTOR-START was expecting a 0x00 padding byte, but got 0x%02x", val);
            }
            // m_multisector_mode = true;
        }
        m_state = CTRL_COMMAND;
        break;

    // ---------------------- MULTI-SECTOR WRITE END ----------------------
    // see the explanation for MULTI-SECTOR WRITE START first.  this command
    // terminates the mode, commanding the controller to flush any deferred
    // sector writes.
    //
    // the command stream looks like this:
    //     receive
    //        byte 0: <special command, source drive, head>
    //        byte 1: <special command "end multisector write mode" token>
    //        byte 2: unused 00 byte  (not echoed!)
    //     drop ready, perform operation
    //     raise ready, send status
    //        00=ok
    //        01=bad sector address/seek error (ERR I95)
    //        02=not ready or bad head selection, format error (ERR I93)
    //        04=CRC error (ERR I96)

    case CTRL_MSECT_WR_END1:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("MULTI-SECTOR-END was expecting a 0x00 padding byte, but got 0x%02x", val);
            }
            // m_multisector_mode = false;
            // setBusyState(true);
            // ... flush write sector cache ...
            // setBusyState(false);
            m_state = CTRL_MSECT_WR_END2;
        }
        break;

    // everything is done; we must return final status
    // 00=ok, 01=write protect, 02=any other error
    case CTRL_MSECT_WR_END2:
        if (event == EVENT_IBS_POLL) {
            rv = true;
            // do we need to worry about m_drive not being the right one?
            m_byte_to_send = (m_d[m_drive].wvd->getWriteProtect()) ? 0x01 : 0x00;
        }
        m_state = CTRL_COMMAND;
        break;

    // --------------------------- VERIFY RANGE ---------------------------
    // this command reads a range of sectors and reports back any sectors
    // that are not readable.
    //
    // Note that the Paul Szudzik SDS document claims the response consists
    // of three bytes: two sector bytes and a one byte status code, but the
    // LvpDiskCommandSequences document says the response is four bytes:
    // a three byte sector value and a one byte status code.  The latter makes
    // more sense, so the emulation follows that pattern.
    //
    // the command stream looks like this:
    //
    //     receive
    //        byte 0: <special command, drive, head>
    //        byte 1: <special command "verify range" token>
    //        byte 2-4: start sector
    //     send status
    //        00=ok, 01=bad sector address, 02=not ready or bad head selection
    //     receive
    //        byte 5-7: source end sector
    //     send status
    //        00=ok, 01=bad sector address, 02=not ready or bad head selection
    //     receive
    //        00 byte  -- but don't echo
    //     drop ready, read indicated sectors
    //     raise ready, send status:
    //        bytes 0-2: number of sector in error, ms byte first
    //        byte  3:   reason: 00=OK
    //                           01=seek error
    //                           02=defective header
    //                           04=ecc/crc
    //
    // after a sector is reported, ready is dropped again, and more sectors
    // are scanned, reporting all found in error.  when no more are found,
    // or if none were found at all, a final status sequence of
    // 0x00, 0x00, 0x00 is sent.
    //
    // We model verify's timing a track at a time for simplicity.
    //
    // 1) select disk.
    //    set a timer to seek to the next track, plus one disk revolution
    // 2) read all sectors of the track.
    //    increment track counter, and return to step 1 or drop busy and quit

    // send status after the start
    case CTRL_VERIFY_RANGE1:
        if (event == EVENT_IBS_POLL) {
            rv = true;
            if (m_drive >= numDrives()) {
                m_byte_to_send = 0x01;  // non-existent drive
            } else {
                const int num_platters = m_d[m_drive].wvd->getNumPlatters();
                const int num_sectors  = m_d[m_drive].wvd->getNumSectors();
                m_byte_to_send = (m_d[m_drive].state == DRIVE_EMPTY) ? 0x01
                               : (m_range_start   >= num_sectors)    ? 0x01
                               : (m_range_platter >= num_platters)   ? 0x02
                                                                     : 0x00;
            }
            if (m_byte_to_send == 0x00) {
                getBytes(3, CTRL_VERIFY_RANGE2);
            } else {
                m_state = CTRL_COMMAND;
            }
        }
        break;

    // look at the source end sector and return status
    case CTRL_VERIFY_RANGE2:
        m_range_end = (m_get_bytes[0] << 16) |
                      (m_get_bytes[1] <<  8) |
                       m_get_bytes[2];
        if (event == EVENT_IBS_POLL) {
            rv = true;
            m_byte_to_send = (m_d[m_drive].state == DRIVE_EMPTY)                ? 0x01
                           : (m_range_end >= m_d[m_drive].wvd->getNumSectors()) ? 0x02
                                                                                : 0x00;
        }
        m_state = (m_byte_to_send == 0x00) ? CTRL_VERIFY_RANGE3
                                           : CTRL_COMMAND;
        break;

    // wait for the 0x00 byte
    case CTRL_VERIFY_RANGE3:
        if (event == EVENT_OBS) {
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("VERIFY_RANGE3 was expecting a 0x00 padding byte, but got 0x%02x", val);
            }
            setBusyState(true);
            m_state = CTRL_VERIFY_RANGE4;
            // seek the first track
            m_drive   = m_range_drive;
            m_secaddr = m_range_start;
            wvdStepToTrack();
        }
        break;

    // read all the sectors on the current track that fall in range
    case CTRL_VERIFY_RANGE4:
        {
            const int cur_track     = m_d[m_drive].track;
            const int sec_per_trk   = m_d[m_drive].sectors_per_track;
            const int64 ns_per_trk  = m_d[m_drive].ns_per_sector * sec_per_trk;
            const int last_track    = m_range_end / sec_per_trk;
            const int first_sec_of_track = cur_track * sec_per_trk;
            const int last_sec_of_track  = first_sec_of_track + sec_per_trk - 1;
            const int first = std::max(m_range_start, first_sec_of_track);
            const int last  = std::min(m_range_end,    last_sec_of_track);

            bool ok = true;
            m_byte_to_send = 0x00;
            uint8 data[256];

            for (m_secaddr = first; ok && (m_secaddr <= last); m_secaddr++) {
                ok = m_d[m_drive].wvd->readSector(m_range_platter, m_secaddr, &data[0]);
            }
            if (!ok) {
                m_byte_to_send = 0x01;  // seek error
            }

            const int next_track = m_d[m_drive].track + 1;
            if (ok && (next_track <= last_track)) {
                m_state = CTRL_VERIFY_RANGE4; // stay
                // account for one rotation of disk, plus step time
                const int64 delay = ns_per_trk + wvdGetNsToTrack(next_track);
                m_d[m_drive].track = next_track;
                wvdTickleMotorOffTimer();  // make sure motor keeps going
                wvdSeekTrack(delay);
            } else {
                // either success or failure
                m_state = CTRL_VERIFY_RANGE5;
                setBusyState(false);
            }
        }
        break;

    // return status
    case CTRL_VERIFY_RANGE5:
        if (event == EVENT_IBS_POLL) {
            rv = true;
            if (m_byte_to_send == 0x00) {
                // no errors
                m_send_bytes[0] = m_send_bytes[1] = m_send_bytes[2] = 0x00;
                m_send_bytes[3] = 0x00;
            } else {
                // 0x01=seek error (ERR I95)
                // 0x02=bad sector header, format error (ERR I93)
                // 0x04=bad ecc/crc (ERR I96)
                // 0x09=beyond limits error (ERR I98)
                m_send_bytes[0] = (m_secaddr >> 16) & 0xff;  // sector msb
                m_send_bytes[1] = (m_secaddr >>  8) & 0xff;  // sector mid
                m_send_bytes[2] = (m_secaddr >>  0) & 0xff;  // sector lsb
                m_send_bytes[3] = m_byte_to_send;            // error code
            }
            sendBytes(4, CTRL_COMMAND);
        }
        break;

#if 0
    // -------------- GET CONTROLLER STATUS AND PROM REVISION --------------
    // this command is documented (mostly) in LvpDiskCommandSequences, p.11
    // the implementation here is just a placeholder, as what the DPU type,
    // protocol level, and microprogram release fields should be is unknown.
    case CTRL_READ_STATUS:
        {
            static bool reported = false;
            if (!reported) {
                reported = true;
                UI_warn("Unimplemented special command: READ STATUS");
            }
        }
        // at this point we expect to see a 0x00 dummy byte from the CPU
        if (event == EVENT_OBS) {
            int num_sectors = 0;
            if ((NOISY > 0) && (val != 0x00)) {
                UI_warn("READ_STATUS was expecting a 0x00 padding byte, but got 0x%02x", val);
            }
            if (m_drive < numDrives()) {
                num_sectors = m_d[m_drive].wvd->getNumSectors();
            }
            m_send_bytes[0] = 0x0F;     // count of bytes in message (not including this one)
            m_send_bytes[1] = 0x41;     // DPU type (1st ascii digit) = 'A'
            m_send_bytes[2] = 0x42;     // DPU type (2nd ascii digit) = 'B'
            m_send_bytes[3] = 0x31;     // protocol level (1 ASCII digit) = '1'
            m_send_bytes[4] = 0x30;     // microprogram release (1st ascii digit) = '0'
            m_send_bytes[5] = 0x31;     // microprogram release (2nd ascii digit) = '1'
            m_send_bytes[6] = (num_sectors >> 16) & 0xFF;
            m_send_bytes[7] = (num_sectors >>  8) & 0xFF;
            m_send_bytes[8] = (num_sectors >>  0) & 0xFF;
            m_send_bytes[9] = 0x00;     // future expansion
            m_send_bytes[10] = 0x00;    // future expansion
            m_send_bytes[11] = 0x00;    // future expansion
            m_send_bytes[12] = 0x00;    // future expansion
            m_send_bytes[13] = 0x00;    // future expansion
            m_send_bytes[14] = 0x00;    // future expansion
            m_send_bytes[15] = 0x00;    // future expansion
        }
        sendBytes(16, CTRL_COMMAND);
        break;
#endif

#if 0
    // ------------------- TURN OFF RETRY AND ADDRESS CHECK -------------------
    // this is documented in LvpDiskCommandSequences, p11
    // NB: the checks are re-enabled via reset.
    // NB: LvpDiskCommandSequences, p15, says that $GIO command 4501
    //     (which is a CBS/IMM=01) clears this condition.
    // case CTRL_NO_ERROR_CHECKS:
        {
            static bool reported = false;
            if (!reported) {
                reported = true;
                UI_warn("Unimplemented special command: NO_ERROR_CHECKS");
            }
        }
#endif

    // -------------------------- UNKNOWN COMMAND --------------------------
    // should have been filtered out already

    default:
        assert(false);
        break;
    }

    if (DBG > 2) {
        static int prev_state = CTRL_WAKEUP;
        if (prev_state != m_state) {
            dbglog("%s  -->  %s\n", stateName(prev_state).c_str(), stateName(m_state).c_str());
            prev_state = m_state;
        }
        dbglog("---------------------\n");
    }

    if (DBG > 2 && rv) {
        dbglog("   IBS return value will be 0x%02x\n", m_byte_to_send);
    }
    return rv;
}

// vim: ts=8:et:sw=4:smarttab
