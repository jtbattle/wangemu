# Purpose: class for verifying and listing Wang BASIC and BASIC-2 data files
# Author:  Jim Battle
#
# Version: 1.0, 2018/09/15, JTB
#     massive restructuring of the old wvdutil code base
# Version: 1.1, 2021/06/19, JTB
#     get rid of bilingualism (aka python2 support);
#     convert to inline type hints instead of type hint pragma comments
# Version: 1.2, 2021/06/20, JTB
#     declare and use type aliases Sector and SectorList for clarity

from typing import List, Dict, Any, Tuple  # pylint: disable=unused-import
from wvdTypes import Sector, SectorList, Options
from wvdHandler_base import WvdHandler_base
from wvdlib import valid_bcd_byte

########################################################################
# class definition

class WvdHandler_data(WvdHandler_base):

    def __init__(self):
        WvdHandler_base.__init__(self)

    @staticmethod
    def name() -> str:
        return "data"

    @staticmethod
    def nameLong() -> str:
        return "Wang BASIC and BASIC-2 data files"

    ########################################################################
    # a data file consists of a series of records, each of which is a series
    # of sectors.  this routine doesn't parse the value stream to see if it
    # makes sense, but does some simple checks on the control bytes.
    #
    #  1st sector of file: 0x81 01  (this contains the file name)
    #
    #  1st sector of a logical record: 0x82 01
    #  2nd sector of a logical record: 0x82 02
    #  3rd sector of a logical record: 0x82 03
    #  etc.
    #  last sector of a logical record: 0x81 nn (where nn is the sector # of record)
    #
    #  last sector of active file: 0xa0 (the other 255 bytes are ignored)
    #  This is written via DATASAVE DC END, but not all programs do this,
    #  so it might exist or not for a given file.
    #
    #  last sector of allocated file: 0xa0 nnnn (nnnn = # of sectors in use)
    #
    # pylint: disable=too-many-return-statements, too-many-branches
    def checkBlocks(self, blocks: SectorList, opts: Options) -> Dict[str, Any]:

        if 'warnlimit' not in opts: opts['warnlimit'] = 0

        start = opts['sector']  # sector offset of first sector of file

        self.clearErrors()

        state = 0
        sec = start
        for offset, blk in enumerate(blocks):
            sec = start + offset

#           I don't remember where I saw the header byte from; comment out until I understand it better
#           if (s == 0) and (blk[0] == 0x6a):
#               # print("%s is some kind of raw binary file; assuming OK" % str_fname)
#               return False

            if state == 0:
                # looking for either end of data or start of record
                if (blk[0] & 0xa0) == 0xa0:
                    state = 2
                    break
                if (blk[0] == 0x81) and (blk[1] == 0x01):
                    # this is a single sector record
                    state = 0
                elif (blk[0] == 0x82) and (blk[1] == 0x01):
                    # this starts a multisector record; expect another sector
                    expect = 0x02
                    state = 1
                else:
                    self.error(sec, "sector %d has bad control bytes 0x%02x%02x" \
                                    % (sec, blk[0], blk[1]))
                    break

            elif state == 1:
                # expect to see next (and possibly last) sector of record
                if blk[1] != expect:
                    self.error(sec, "sector %d expected to have sequence number 0x%02x, got 0x%02x" \
                                    % (sec, expect, blk[1]))
                    break
                if blk[0] == 0x82:
                    # expect more sectors in this logical record
                    expect += 1
                    state = 1  # that is, no change
                elif blk[0] == 0x81:
                    # this is the last one of this record
                    state = 0  # expect either end of data or new record
                else:
                    self.error(sec, "sector %d has bad control bytes 0x%02x%02x" \
                                    % (sec, blk[0], blk[1]))
                    break

            elif state == 2:
                # we've seen the end of data marker, so there shouldn't be more
                self.warning(sec, "sector %d, end-of-data marker seen before final used sector" \
                                  % sec)
                break

            # the control bytes are OK; now check the contents
            self.checkDataRecord(sec, blk)

            # if (error or warning count exceeds limit)
            if self._errors or (len(self._warnings) > opts['warnlimit']):
                break

        return self.status(sec, opts)

    ########################################################################
    # check if the record contents, ignoring the control bytes, appear to
    # contain a valid data record.  specifically, check that
    #    1) it contains a sequence of valid SOV bytes,
    #    2) the data associated with the SOV byte is consistent
    #    3) there is a terminator byte immediately after some value
    # returns True on error, False if it is OK
    # pylint: disable=too-many-branches
    def checkDataRecord(self, sec: int, blk: Sector) -> None:
        cp = 2  # skip control bytes
        while (cp < 255) and (blk[cp] <= 0xFC):
            c = blk[cp]

            if c == 0x08:  # numeric
                if cp+8 > 254:  # 254 because we still need a terminator
                    self.error(sec, "sector %d, SOV indicates a number, but there aren't enough bytes left in the sector" \
                                    % sec)
                    return
                fp = blk[cp+1 : cp+9]
                # make sure all digits are BCD
                # pylint: disable=too-many-boolean-expressions
                if not valid_bcd_byte(fp[1] & 0x0F) or \
                   not valid_bcd_byte(fp[1]) or \
                   not valid_bcd_byte(fp[2]) or \
                   not valid_bcd_byte(fp[3]) or \
                   not valid_bcd_byte(fp[4]) or \
                   not valid_bcd_byte(fp[5]) or \
                   not valid_bcd_byte(fp[6]) or \
                   not valid_bcd_byte(fp[7]):
                    self.error(sec, "sector %d, fp value has non-BCD digits" % sec)
                    return
                # TODO: check for denorms, negative zero, unexpected flag bits?
                #expSign  = (fp[0] & 0x80) != 0
                #mantSign = (fp[0] & 0x10) != 0
                cp += 9

            elif c < 128:
                self.error(sec, "sector %d, unexpected SOV value of 0x%02X" % (sec, c))
                return

            else:
                strlen = c-128
                if cp+strlen > 254:  # 254 because we still need a terminator
                    self.error(sec, "sector %d; SOV indicates string length that doesn't fit in sector" % sec)
                    return
                cp += 1 + strlen

        # check that we ended with a terminator
        if blk[cp] != 0xFD:
            self.error(sec, "sector %d doesn't contain 0xFD terminator" % sec)
            return

    ########################################################################
    # given a one sector from a structured data file (that is, produced by
    # DATASAVE), produce a human readable listing.  The return bool is True
    # if the block terminates the file, and is otherwise False.
    # pylint: disable=too-many-locals
    def listOneBlock(self, blk: Sector, opts: Options) -> Tuple[bool, List[str]]:
        listing = []

        if (blk[0] & 0xa0) == 0xa0:
            return (True, [])

        cp = 2  # skip control bytes
        while (cp < 255) and (blk[cp] < 0xFC):
            c = blk[cp]

            if c == 0x08:  # numeric
                if cp+8 > 254:  # 254 because we still need a terminator
                    listing.append("# SOV indicates a number, but there aren't enough bytes left in the sector")
                    return (False, listing)
                fp = blk[cp+1 : cp+9]
                expSign  = (fp[0] & 0x80) != 0
                mantSign = (fp[0] & 0x10) != 0
                exp =  1 * ((fp[0] >> 0) & 0xF) \
                    + 10 * ((fp[1] >> 4) & 0xF)
                mant = ("%d" % ((fp[1] >> 0) & 0xF)) + "."
                for d in fp[2:8]:
                    mant += ("%02X" % d)
                fpascii = (" ", "-")[mantSign] + mant + \
                          ("+", "-")[ expSign] + ("%02X" % exp)
                listing.append(fpascii)
                cp += 9

            elif c < 128:
                listing.append("Unexpected SOV value of 0x%02X" % c)
                return (False, listing)

            else:
                strlen = c-128
                if cp+strlen > 254:  # 254 because we still need a terminator
                    listing.append("# SOV indicates string length that doesn't fit in sector")
                    return (False, listing)
                dataobj = blk[cp+1 : cp+1+strlen]
                # expand non-ascii characters
                tmp = [chr(byt) if (32 <= byt < 128) else ("\\%02X" % byt) for byt in dataobj]
                strng = ''.join(tmp)
                listing.append('"' + strng + '"')
                cp += 1 + strlen

        return (False, listing)


    ########################################################################
    def listBlocks(self, blocks: SectorList, opts: Options) -> List[str]:
        # same opts as listOneBlock

        logicalRecNum = 0
        listing = []

        for offset, blk in enumerate(blocks):
            # print(">>> sector control bytes 0x%02X 0x%02X" % (blk[0], blk[1]))
            if (blk[0] & 0x80) == 0:
                listing.append('Sector %d of data file has bad header byte 0x%02X' % (offset, blk[0]))
                return listing
            if blk[1] == 0x01:
                listing.append('### Logical Record %d' % logicalRecNum)
                logicalRecNum += 1

            done, morelines = self.listOneBlock(blk, opts)
            listing.extend(morelines)
            if done: break

        return listing
