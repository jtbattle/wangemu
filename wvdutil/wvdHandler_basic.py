# Purpose: class for verifying and listing Wang BASIC and BASIC-2 program files
# Author:  Jim Battle
#
# Version: 1.0, 2018/09/15, JTB
#     massive restructuring of the old wvdutil code base
# Version: 1.1, 2019/01/26, JTB
#     the "VER(" token was mapped to "VER" (missing paren)
# Version: 1.2, 2019/01/03, JTB
#     fixed an illegal array index when 0xFF (line number token)
#     appears at the end of the block such that the two necessary
#     bytes holding the line number don't exist.
# Version: 1.3, 2020/01/20, JTB
#     failed to run with python 2 because str.find() needs char, not int.
#     handle the case where the program header block does not say protected,
#     yet the program blocks (perhaps just some) are scrambled.
# Version: 1.4, 2020/02/02, JTB
#     added LIST tokens for DATE (0xFA) and TIME (0xFB) (used in later BASIC-2)

from __future__ import print_function
import sys
import re
from typing import List, Dict, Any, Tuple  # pylint: disable=unused-import
from wvdlib import (WvdFilename, valid_bcd_byte, headerName, unscramble_one_sector)
from wvdHandler_base import WvdHandler_base

python2 = (sys.version_info.major == 2)

########################################################################
# table of BASIC atoms
# not all entries are occupied
token = [''] * 256

token[0x80] = 'LIST '
token[0x81] = 'CLEAR '
token[0x82] = 'RUN '
token[0x83] = 'RENUMBER '
token[0x84] = 'CONTINUE '
token[0x85] = 'SAVE '
token[0x86] = 'LIMITS '
token[0x87] = 'COPY '
token[0x88] = 'KEYIN '
token[0x89] = 'DSKIP '
token[0x8A] = 'AND '
token[0x8B] = 'OR '
token[0x8C] = 'XOR '
token[0x8D] = 'TEMP'
token[0x8E] = 'DISK '
token[0x8F] = 'TAPE '

token[0x90] = 'TRACE '
token[0x91] = 'LET '
token[0x92] = 'FIX('        # BASIC-2
token[0x93] = 'DIM '
token[0x94] = 'ON '
token[0x95] = 'STOP '
token[0x96] = 'END '
token[0x97] = 'DATA '
token[0x98] = 'READ '
token[0x99] = 'INPUT '
token[0x9A] = 'GOSUB '
token[0x9B] = 'RETURN '
token[0x9C] = 'GOTO '
token[0x9D] = 'NEXT '
token[0x9E] = 'FOR '
token[0x9F] = 'IF '

token[0xA0] = 'PRINT '
token[0xA1] = 'LOAD '
token[0xA2] = 'REM '
token[0xA3] = 'RESTORE '
token[0xA4] = 'PLOT '
token[0xA5] = 'SELECT '
token[0xA6] = 'COM '
token[0xA7] = 'PRINTUSING '
token[0xA8] = 'MAT '
token[0xA9] = 'REWIND '
token[0xAA] = 'SKIP '
token[0xAB] = 'BACKSPACE '
token[0xAC] = 'SCRATCH '
token[0xAD] = 'MOVE '
token[0xAE] = 'CONVERT '
token[0xAF] = 'PLOT '       # [SELECT] PLOT

token[0xB0] = 'STEP '
token[0xB1] = 'THEN '
token[0xB2] = 'TO '
token[0xB3] = 'BEG '
token[0xB4] = 'OPEN '
token[0xB5] = 'CI '         # [SELECT] CI
token[0xB6] = 'R '          # [SELECT] R
token[0xB7] = 'D '          # [SELECT] D
token[0xB8] = 'CO '         # [SELECT] CO
token[0xB9] = 'LGT('        # BASIC-2 only
token[0xBA] = 'OFF '
token[0xBB] = 'DBACKSPACE '
token[0xBC] = 'VERIFY '
token[0xBD] = 'DA '
token[0xBE] = 'BA '
token[0xBF] = 'DC '

token[0xC0] = 'FN'
token[0xC1] = 'ABS('
token[0xC2] = 'SQR('
token[0xC3] = 'COS('
token[0xC4] = 'EXP('
token[0xC5] = 'INT('
token[0xC6] = 'LOG('
token[0xC7] = 'SIN('
token[0xC8] = 'SGN('
token[0xC9] = 'RND('
token[0xCA] = 'TAN('
token[0xCB] = 'ARC'
token[0xCC] = '#PI'
token[0xCD] = 'TAB('
token[0xCE] = 'DEFFN'
token[0xCF] = 'TAN('      # ARCTAN( gets encoded as CBCF sequence

token[0xD0] = 'SIN('      # ARCSIN( gets encoded as CBD0 sequence
token[0xD1] = 'COS('      # ARCCOS( gets encoded as CBD1 sequence
token[0xD2] = 'HEX('
token[0xD3] = 'STR('
token[0xD4] = 'ATN('
token[0xD5] = 'LEN('
token[0xD6] = 'RE'         # used by REDIM?
token[0xD7] = '#'          # [SELECT] #
token[0xD8] = '%'          # % [image]
token[0xD9] = 'P'          # [SELECT] P
token[0xDA] = 'BT'
token[0xDB] = 'G'          # [SELECT] G
token[0xDC] = 'VAL('
token[0xDD] = 'NUM('
token[0xDE] = 'BIN('
token[0xDF] = 'POS('

token[0xE0] = 'LS='
token[0xE1] = 'ALL'
token[0xE2] = 'PACK'
token[0xE3] = 'CLOSE'
token[0xE4] = 'INIT'
token[0xE5] = 'HEX'        # eg HEXPRINT, not HEX(
token[0xE6] = 'UNPACK'
token[0xE7] = 'BOOL'
token[0xE8] = 'ADD'
token[0xE9] = 'ROTATE'
token[0xEA] = '$'          # $[statement]
token[0xEB] = 'ERROR'
token[0xEC] = 'ERR'        # BASIC-2 only
token[0xED] = 'DAC '       # BASIC-2 only
token[0xEE] = 'DSC '       # BASIC-2 only
token[0xEF] = 'SUB'        # BASIC-2 only

token[0xF0] = 'LINPUT '    # BASIC-2 only
token[0xF1] = 'VER('       # BASIC-2 only
token[0xF2] = ' ELSE '     # BASIC-2 only
token[0xF3] = 'SPACE'      # BASIC-2 only
token[0xF4] = 'ROUND('     # BASIC-2 only
token[0xF5] = 'AT('        # BASIC-2 only
token[0xF6] = 'HEXOF('     # BASIC-2 only
token[0xF7] = 'MAX('       # BASIC-2 only
token[0xF8] = 'MIN('       # BASIC-2 only
token[0xF9] = 'MOD('       # BASIC-2 only
token[0xFA] = 'DATE'       # BASIC-2 only
token[0xFB] = 'TIME'       # BASIC-2 only

########################################################################
# list the program from just one record, optionally prettyprinted

# ugly ENUM
ST_ATOMIC    = 0   # somewhere in the line where we expand atoms
ST_IN_QUOTES = 1   # inside "..."
ST_IN_REM    = 2   # after REM but before ':' or eol
ST_IN_IMAGE  = 3   # inside image (%) statement

# pylint: disable=too-many-branches
def listProgramRecord(blk, secnum):
    # type: (bytearray, int) -> List[str]

    listing = []

    # 1. if (byte[0] & 0xC0) != 0x00, header or data block
    # 2. if (byte[0] & 0x10) != 0x10, not protected at all
    # 3. if (byte[1] & 0x80) == 0x80, SAVEP style
    # 4. else                         SAVE! style
    if (blk[0] & 0xc0) == 0x00 and \
       (blk[0] & 0x10) == 0x10 and \
       (blk[1] & 0x80) == 0x00:
        blk = unscramble_one_sector(blk)

    if (blk[0] & 0xc0) == 0x40:
        # header record
        name = headerName(blk)
        if (blk[0] & 0x10) == 0x10:
            ftype = ', protected file'
        else:
            ftype = ''
        listing.append("# Sector %d, program filename = '%s'%s" % (secnum, name, ftype))
        return listing

    if (blk[0] & 0xc0) != 0x00:
        # data record
        # FIXME: maybe just do a hex dump?
        listing.append("# %s doesn't look like a program record" % secnum)
        return listing

    # program body record
    cp = 1            # skip control byte
    line = ""
    state = ST_ATOMIC
    while (cp < 255) and (blk[cp] != 0xFD) and (blk[cp] != 0xFE):
        c = blk[cp]

        if c == 0x0D:
            listing.append(line)
            line = ""
            state = ST_ATOMIC
            cp += 3  # 0x0D, then two 00 bytes after line end
            continue

        if c < 128:
            line += chr(c)
        elif (c == 0xFF) and (cp < 254):
            # line number
            linenum = 1000*(blk[cp+1] // 16) + \
                       100*(blk[cp+1]  % 16) + \
                        10*(blk[cp+2] // 16) + \
                         1*(blk[cp+2]  % 16)
            cp += 2
            line += "%d" % linenum
        elif (state != ST_ATOMIC) or token[c] == '':
            line += "\\%02X" % c
        else:
            line += token[c]

        # TODO: there are a few differences between Wang BASIC and BASIC-2.
        #     - Wang BASIC didn't do smart parsing; tokens were expanded
        #       in every state, not just "ST_ATOMIC" state
        #     - in Wang BASIC, an image statement ends at an embedded colon,
        #       but the logic below just follows BASIC-2 convention.
        #     - in Wang BASIC, strings can be inside single quotes too,
        #       but BASIC-2 doesn't allow it.
        if (c == 0xA2) and (state == ST_ATOMIC):  # REM token
            state = ST_IN_REM
        elif (c == 0xD8) and (state == ST_ATOMIC):  # image (%) token
            state = ST_IN_IMAGE
        elif (c == ord('"')) and (state == ST_ATOMIC):
            state = ST_IN_QUOTES
        elif (c == ord('"')) and (state == ST_IN_QUOTES):
            state = ST_ATOMIC
        elif (c == ord(':')) and (state == ST_IN_REM):
            state = ST_ATOMIC

        cp += 1

    return listing

########################################################################
# given a regular line listing, pretty print it into multiple lines,
# ala the VP "LIST D" command
#
# + origline is the unadorned program line, in ascii
# + width is the screen width, so we know where to wrap
# + basic2 is a boolean indicating if we follow basic2 rules or not
#
# LISTD inserts a blank line when it thinks the line can't fall through,
# eg, 20 GOTO 100, or 100 RETURN. This function doesn't mimic that.
#
# LISTD has a feature where "REM%" causes a page break and some
# blank lines to be printed.  This function doesn't mimic that.

# pylint: disable=too-many-branches, too-many-statements
def prettyprint(origline, width=80, basic2=True):
    # type: (str, int, bool) -> List[str]

    # make a copy
    line = origline
    listing = []

    # 2) ignore spaces on left, extract line number and rest of line
    tmatch = re.search(r'^( *)([0-9]{1,4})(.*)$', line)
    if not tmatch:
        return [line]
    linenumber = tmatch.group(2)
    line       = tmatch.group(3)

    # 3) the line number is always four digits and zero filled,
    #    followed by a space
    newline = '%04d ' % int(linenumber)
    # 3b) eat one space if the line started with a space anyway
    if (line != '') and (line[0] == ' '):
        line = line[1:]

    # 4) break the line into statements
    while line:

        # skip spaces before the next statement
        tmatch = re.search(r'^( *)(.*)$', line)
        if tmatch:
            newline += tmatch.group(1)
            line     = tmatch.group(2)

        stmt_end = 0  # marker to end of statement

        # we don't really loop forever; this just allows
        # "break" for each case to jump to an exit point
        while True:

            # if REM, skip to next colon.  quotes don't matter
            tmatch = re.search(r'^(REM [^:]*)', line)
            if tmatch:
                stmt_end = tmatch.end(1)  # one char past end of stmt
                break

            # if it is an image statement (%), ':' is a statement
            # separator for Wang BASIC, but not BASIC-2
            if line[0] == '%':
                if basic2:
                    stmt_end = len(line)
                else:
                    tmatch = re.search(r'^(%[^:]*)', line)
                    if tmatch is None:
                        stmt_end = len(line)
                    else:
                        stmt_end = tmatch.end(1)
                break

            # scan until end of statement, watching out for quotes
            line_len = len(line)
            while stmt_end < line_len:
                #print("looking at:%s" % line[stmt_end:])

                if basic2:
                    # basic2 has only double quotes
                    tmatch = re.search(r'([:"])', line[stmt_end:])
                else:
                    # wang basic has single and double quotes
                    tmatch = re.search(r'([:"''])', line[stmt_end:])
                if not tmatch:
                    # statement must consume rest of the line
                    stmt_end = len(line)
                    break

                if tmatch.group(1) == ':':
                    # simple case -- next colon marks stmt boundary
                    stmt_end += tmatch.start(1)
                    break

                # found a quote -- find matcher, if there is one
                stmt_end += tmatch.end(1)  # begin looking char after open quote
                pat = "(%c)" % tmatch.group(1)
                #print("find closing quote in:%s" % line[stmt_end:])
                tmatch = re.search(pat, line[stmt_end:])
                if tmatch:
                    #print("yes")
                    stmt_end += tmatch.end(1)
                    #print("remainder:%s" % line[stmt_end:])
                    continue
                # didn't find matching closing quote
                #print("no")
                stmt_end = len(line)
                break

            # end while (stmt_end < line_len)
            break

        # end while True

        # we should have found the end of the next statement at this point
        assert stmt_end > 0

        if (stmt_end < len(line)) and (line[stmt_end] != ':'):
            # confused -- the end of statement wasn't the end of the line,
            # nor was it followed by ":".  just tack on the remainder
            stmt_end = len(line)

        newline += line[0:stmt_end]  # grab statement
        # wrap listing if needed
        while newline != '':
            if len(newline) <= width:
                listing.append(newline)
                break
            listing.append(newline[0:width])
            if width > 6:
                newline = "     " + newline[width:]
            else:
                newline = newline[width:]

        line = line[stmt_end:]       # remainder of line
        if line != '':
            if line[0] == ':':
                line = line[1:]
                newline = "   : "
                if (line != '') and (line[0] == ' '):
                    # chomp one whitespace because we added it already after "    : "
                    line = line[1:]
            else:
                assert line == ''      # we must be done by now

    # end while (len(line) > 0):

    return listing

########################################################################
# class definition

class WvdHandler_basic(WvdHandler_base):

    def __init__(self):
        WvdHandler_base.__init__(self)

    @staticmethod
    def name():
        return "program"

    @staticmethod
    def nameLong():
        return "Wang BASIC and BASIC-2 program files"

    @staticmethod
    def fileType():
        return 'P '  # Program

    ########################################################################
    # inspect the sectors of a program file to make sure they are consistent
    # and look like a program.
    #
    # check that the control byte of each sector matches what is expected,
    # and that if the program is protected, that flag is consistent.
    # the first sector is 0x40, middle sectors are 0x00, final sector is 0x20
    #
    #    a) the first sector is the header.  the first byte is 0x40 if it is
    #       an ordinary file, or 0x50 if it is a protected file.  the next
    #       eight bytes are the filename, which should match what is stored
    #       in the catalog, if the catalog is in use.  the next byte must be
    #       0xFD (EOB, end of block).
    #    b) the middle sectors start with 0x00 if normal program files, or 0x10
    #       if protected.  there are some number of bytes encoding one or more
    #       program lines, but they are marked by a 0xFD (EOB).  we probably
    #       don't care to parse the program, but we can make a few checks:
    #          * the first few bytes are of the form FF ab cd, where 0xFF is the
    #            "line number" token, and abcd are BCD digits.
    #          * lines end with 0D 00 00, so check for 00 00 after any 0D
    #          * the end of block is 0xFD, and immediately follows 0D 00 00
    #       the middle sector type is optional; short programs will have only
    #       the header sector and a trailer sector.
    #    c) the trailer sector starts with 0x20 for normal programs, and 0x30
    #       if protected.  the constitution is the same as described as for
    #       intermediate sectors, except that rather than terminating with a
    #       0xFD (EOB), it is terminated with 0xFE (EOD, end of data)
    #
    # If there is only a single block, don't be strict about the file
    # structure, i.e., don't insist a header block comes first.

    # pylint: disable=too-many-locals, too-many-branches
    def checkBlocks(self, blocks, options):
        # type: (List[bytearray], Dict[str, Any]) -> Dict[str, Any]
        if 'warnlimit' not in options: options['warnlimit'] = 0

        one_block = len(blocks) == 1

        start = options['sector']  # sector offset of first sector of file

        protected = 0x00  # until proven otherwise
        trailer_record_num = -1
        self.clearErrors()

        for offset, blk in enumerate(blocks):
            sec = start + offset

            # if it looks scrambled, unscramble it
            if ((blk[0] & 0xD0) == 0x10) and ((blk[1] & 0x80) == 0x00):
                newblk = bytearray(blk)
                protected = 0x10
                blk = unscramble_one_sector(newblk)

            data_record    = (blk[0] & 0x80) == 0x80
            header_record  = (blk[0] & 0x40) == 0x40
            trailer_record = (blk[0] & 0x20) == 0x20

            if data_record:
                self.error(sec, "sector %d indicates it is a data record" % sec)
                return self.status(sec, options)

            if header_record:
                # first sector
                terminator_byte = 0xFD
                protected = (blk[0] & 0x10)  # 0x10 if "SAVE P" file
                expected = 0x40 | protected
            elif trailer_record:
                terminator_byte = 0xFE
                expected = 0x20 | protected
            else:  # middle sector
                terminator_byte = 0xFD
                expected = 0x00 | protected

            # the first byte of each sector identifies the type of data it contains
            if (blk[0] & 0xf0) != expected:
                self.warning(sec, "sector %d has control byte of 0x%02x; 0x%02x expected" \
                                  % (sec, blk[0] & 0xf0, expected) )

            if header_record:
                if offset != 0 and not one_block:
                    # TODO: if we are using this function to scan, we don't want to
                    #       return an error, just cut off the file there I guess
                    self.error(sec, "sector %d indicates it is a header record" % sec)
                else:
                    cat_fname = options.get('cat_fname', '')
                    self.checkHeaderRecord(sec, blk, cat_fname)
            else:
                if offset == 0 and not one_block:
                    self.warning(sec, "sector %d should have been a header record" % sec)
                else:
                    # middle or trailer program record; these contain the actual program
                    self.checkBodyRecord(sec, blk, terminator_byte)
                    # TODO: line numbers should strictly increase when advancing
                    #       through the file

            if trailer_record:
                trailer_record_num = sec
                break
            if self._errors or (len(self._warnings) > options['warnlimit']):
                break

        if 'used' in options and (trailer_record_num > 0):
            used = options['used']    # number of sectors used according to catalog
            used_computed = (trailer_record_num - start + 1) + 1  # +1 for the control record
            if used != used_computed:
                self.warning(sec, "catalog claims %d used sectors, but file uses %d sectors" \
                                  % (used, used_computed))

        return self.status(sec, options)

    ########################################################################
    # check the first record of a program file.
    # return True on error, False if no errors.
    def checkHeaderRecord(self, sec, blk, cat_fname):
        # type: (int, bytearray, str) -> None

        # the catalog and the header sector both contain the filename
        name = blk[1:9]
        str_name = WvdFilename(name).asStr()
        if cat_fname not in ('', cat_fname):
            self.warning(sec, "file %s's header block indicates file name '%s'" \
                              % (cat_fname, str_name))

        # the next byte after the filename should be 0xFD
        if blk[9] != 0xFD:
            self.warning(sec, "%s's header record (sector %d) must have a 0xFD terminator in the 10th byte" \
                            % (str_name, sec) )

    ########################################################################
    # check the body record of a program file.
    # return True on error, False if no errors.
    # pylint: disable=too-many-return-statements, too-many-branches
    def checkBodyRecord(self, sec, blk, terminator_byte):
        # type: (int, bytearray, int) -> None
        #cat_str_fname = cat_fname.asStr()

        # each record must have a terminator byte (0xFD or 0xFE)
        assert isinstance(terminator_byte, int)
        assert isinstance(blk, bytearray)
        assert len(blk) == 256
        if python2:
            term_byte = chr(terminator_byte) # bytearray is alias of str
        else:
            term_byte = terminator_byte
        pos = blk.find(term_byte)
        if pos < 0:
            self.error(sec, "(sector %d) does not contain a 0x%02X terminator byte" \
                            % (sec, terminator_byte))
            return

        # the terminator shouldn't be too soon -- it must follow the SOB,
        # 0xFF <2 line number bytes) 0x0D 0x00 0x00, except empty files,
        # which start 20 fe 00 00 ...
        if pos < 7 and pos != 1:
            self.error(sec, "(sector %d) contains a 0x%02X terminator byte, but it is too soon" \
                            % (sec, terminator_byte))
            return

        # now scan the active part of the record.  make sure it starts
        # with a line number, that lines are terminated with 0D 00 00,
        # that non-final line endings are followed by another line number,
        # and that the final line ending is followed by the terminator
# TODO: it can start with spaces (Wang BASIC anyway)
        (expect_line_num, seek_cr) = (0, 1)
        state = expect_line_num
        cp = 1
        while cp < pos:
            #print("Inspecting offset=%d, state=%d" % (cp, state))
            if state == expect_line_num:
                # look for line number token
                # Wang BASIC allowed leading spaces before a line number
                if blk[cp] == 0x20:
                    cp = cp+1
                    continue
                if blk[cp] != 0xFF:
                    self.error(sec, "(sector %d) didn't find a line number token, 0xFF, where expected; 0x%02X found" \
                                    % (sec, blk[cp]))
                    return
                # the next two bytes contain the line number in bcd
                if (not valid_bcd_byte(blk[cp+1])) or \
                   (not valid_bcd_byte(blk[cp+2])):
                    self.error(sec, "(sector %d) should begin with a bcd line number, but found 0x%02X%02X" \
                                    % (sec, blk[cp+1], blk[cp+2]))
                    return
                # found a valid line number sequence
                cp = cp+3
                state = seek_cr
                continue

            if state == seek_cr:
                # looking for the end of line
                if blk[cp] != 0x0D:
                    cp = cp+1
                    continue
                # found the end of line; make sure it is followed by 00 00
                if (blk[cp+1] != 0x00) or \
                   (blk[cp+2] != 0x00):
                    self.error(sec, "(sector %d) contains an EOL (0x0D) which isn't followed by 0x0000; found 0x%02X%02X" \
                                    % (sec, blk[cp+1], blk[cp+2]))
                    return
                cp = cp+3
                state = expect_line_num
                continue
            # end while cp<pos

        # check that an end of line immediately precedes the termination byte
# TODO: the first byte might be the terminator.  the while loop above won't execute,
#       but the following test will fail:
        if state == seek_cr:
            self.error(sec, "(sector %d) terminated with a partial program line at offset %d" \
                            % (sec, cp))

    ########################################################################
    def listOneBlock(self, blk, options):
        # type: (bytearray, Dict[str, Any]) -> Tuple[bool, List[str]]
        if (blk[0]) == 0xa0:  # trailer record?
            return (True, [])

        secnum = options['sector']
        listing = listProgramRecord(blk, secnum)

        if options.get('prettyprint', False):
            width = 78
            basic2 = True   # Wang BASIC and BASIC-2 have a few differences
            newlisting = []
            for line in listing:
                newlisting.extend(prettyprint(line, width, basic2))
            listing = newlisting

        return (False, listing)

########################################################################
# testing

def pp(line, width=80, basic2=True):
    # type: (str, int, bool) -> None
    listing = prettyprint(line, width, basic2)
    for txt in listing:
        print(txt)

if __name__ == "__main__":
    pp("10 REM")
    pp("20 REM this is a test:PRINT 12+34")
    pp("30 REM this is a really really really really really really really really really really really really really really really really really really really long statement", 64)
    pp("40 % this a Basic-2 #### image : statement", 80, True)
    pp("40 % this a Wang BASIC #### image : statement", 80, False)
    pp('20 PRINT "Hi there":PRINT 1+2')
    pp('960 PRINT HEX(0C):PRINT A$;S1(1),S(1);TAB(41);S(2);TAB(57);S1(2);" ";STR(A0$,1,1);:PRINT :RETURN ')

    handler = WvdHandler_basic()
