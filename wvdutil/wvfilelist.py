# Program: wvfilelist.py, used by wvdutil.py
# Purpose: encapsulates some useful routines for manipulating .wvd files
# Author:  Jim Battle
#
# Version: 1.1, 2008/10/18, JTB
# Version: 1.3, 2011/08/13, JTB
#     added program prettyprinting
# Version: 1.4, 2011/10/08, JTB
#     refactored a bit to support higher level changes
#     ARCSIN(, ARCCOS(, and ARCTAN( were handled wrong
#     report all characters of the the filename embedded in the program header
# Version: 1.5, 2018/01/01, JTB
#     made it python 2/3 compatible
#     using bytearray data instead of character strings
#     pylint cleanups, mypy type annotation
# Version: 1.6, 2018/08/24, JTB
#     with python3, dumping data files contained "bytearray(...)" spew
#     add command to list assembler source code files
#     smarter program listing: expand tokens only in some contexts
#     pylint cleanups

from __future__ import print_function
from typing import List  # pylint: disable=unused-import
import re

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
token[0xF1] = 'VER'        # BASIC-2 only
token[0xF2] = ' ELSE '     # BASIC-2 only
token[0xF3] = 'SPACE'      # BASIC-2 only
token[0xF4] = 'ROUND('     # BASIC-2 only
token[0xF5] = 'AT('        # BASIC-2 only
token[0xF6] = 'HEXOF('     # BASIC-2 only
token[0xF7] = 'MAX('       # BASIC-2 only
token[0xF8] = 'MIN('       # BASIC-2 only
token[0xF9] = 'MOD('       # BASIC-2 only

########################################################################
# given a header record, return the filename it contains.
# NB: disk headers pad names shorter than 8 characters with 0x20
#     tape headers pad names shorter than 8 characters with 0xFF

def headerName(sector):
    # type: (bytearray) -> str
    rawname = sector[1:9]
    while rawname:
        if (rawname[-1] != 0xff) and (rawname[-1] != ord(' ')):
            break
        rawname = rawname[:-1]
    name = [chr(byt) if (32 <= byt < 128) else ("\\x%02X" % byt) for byt in rawname]
    return ''.join(name)

########################################################################
# given a list of file blocks, return a program file listing

# pylint: disable=too-many-branches, too-many-statements, too-many-locals
def listSourceFileFromBlocks(blocks):
    # type: (List[bytearray]) -> List[str]

# check that each sector contains the right header bytes
# and that each sector contains four strings of 62 chars
# and that the first block contains FILE = ........,
#
#     81 01 be <62 text bytes>
#           be <62 text bytes>
#           be <62 text bytes>
#           be <62 text bytes> fd 00
#
# what do the bytes mean?
#
#   0x81=0x80 (data file) | 0x01 (last physical record of logical record)
#   0x01=sequence number of logical record; it will always be 01 because the
#        logical record never spans sectors
#   0xbe=190; 0x80 bit means string; 190-128-62 = length of string
#   0xfd: end of block
#   0x00: unused padding byte
#
# any number of these blocks.  each line ends with a 1E.  after the last line,
# an 03 appears followed by a bunch of 20 bytes padding out the rest, though
# probably it doesn't really matter -- 03 is the end-of-file.  then there is a
# trailer block.
#     a0 01 be .. ..
# but probably the contents after A0 doesn't matter.

    filename = '        '  # pylint: disable=unused-variable
    end_byte_seen = False
    warning = False        # pylint: disable=unused-variable
    listing = []
    curline = ''

    for secnum, secData in enumerate(blocks):

        if secData[0] == 0xA0:
            # trailer record
            if secnum == 0:
                print("Warning: first sector is a trailer record - null file")
                warning = True
            elif not end_byte_seen:
                print("Warning: relative sector %d is a trailer record, but \\x03 byte not yet seen" % secnum)
                warning = True
            break

        if end_byte_seen:
            print("relative sector %d: end byte \\x03 byte seen; expected trailer record" % secnum)
            warning = True
            break

        if secData[0] != 0x81:
            print("relative sector %d: control byte 0x%02x (expected 0x81)" % (secnum, secData[0]))
            warning = True
            break

        # each logical record spans a single sector
        if secData[1] != 0x01:
            print("relative sector %d: sequence byte 0x%02x (expected 0x01)" % (secnum, secData[1]))
            warning = True
            break

        # verify that the block contains four strings of 62 bytes each
        if (secData[2+0*63] != 0xbe or secData[2+1*63] != 0xbe  or
                secData[2+2*63] != 0xbe or secData[2+3*63] != 0xbe):
            print("relative sector %d: expected to see four strings of 62 bytes each" % secnum)
            warning = True
            break

        # the last two bytes should be fd 00, but we check only 0xfd byte
        # since the final byte shouldn't matter
        if secData[0xfe] != 0xfd:
            print("relative sector %d: end of block byte was 0x%02x (expected 0xfd)" % (secnum, secData[0xfe]))
            warning = True
            break

        # grab the four strings
        line0 = secData[3+0*63:2+1*63]
        line1 = secData[3+1*63:2+2*63]
        line2 = secData[3+2*63:2+3*63]
        line3 = secData[3+3*63:2+4*63]

        if secnum == 0:
            # first sector should start with 0x02 0x01 {length},
            # followed by some text.  the {length} byte indicates an offset
            # to where the first asm source line begins.  that first
            # bit of text doesn't end in a carriage return, so we add it.
            # but if there is an expclit one there, we convert it to a space
            # because that is what the EDIT program does.
            if line0[0] != 0x02 or line0[1] != 0x01 or line0[2] not in range(0x16,0x1b):
                print("relative sector 0: ",
                      "first line of file started with 0x%02x 0x%02x 0x%02x " %
                      (line0[0], line0[1], line0[2]),
                      "(expected 0x02 0x01 (0x16..0x1a))")
                warning = True
                break
            if line0[3:10] != b'FILE = ':
                print("relative sector 0: first line didn't contain the filename")
                warning = True
                break
            filename = line0[10:18].decode('latin_1')  # 8 characters
            # OK, here is some hokey stuff that the EDIT program does for
            # inexplicable reasons.  my guess is that an older version of edit
            # used a slightly different format, and the later version would
            # fix up files in that old format.
            offset = line0[2]  # 0x16..0x1a
            if line0[2+offset] == 0x1e:
                line0[2+offset] = 0x20  # convert to space
            # chomp of that weird three byte header, and split off
            # the file description section from the first actual line
            encoding = 'latin-1'
            line0 = bytearray('* ', encoding) + line0[3:3+offset] + bytearray(chr(0x1E), encoding) + line0[3+offset:]

        lines = []
        for t in (line0, line1, line2, line3):
            if 0x03 in t:
                idx = t.index(0x03)
                t = t[0:idx]  # chomp 0x03 and everything after it
                end_byte_seen = True
            lines.append(t)
            if end_byte_seen:
                break

        # scan through all lines regardless of boundaries and assemble lines
        for line in lines:
            for byt in line:
                if byt == 0x1E:
                    # end of line
                    listing.append(curline)
                    curline = ''
                elif byt == 0x09:
                    # tab
                    curline += "\t"
                elif 32 <= byt <= 128:
                    curline += chr(byt)
                else:
                    curline += "\\x%02x" % byt

    # all blocks processed
    if curline != '':
        listing.append(curline)

    # add filename
    # listing.insert(0, "# FILENAME='%s'" % filename)
    return listing

########################################################################
# list the program from just one record, optionally prettyprinted

# ugly ENUM
ST_ATOMIC    = 0   # somewhere in the line where we expand atoms
ST_IN_QUOTES = 1   # inside "..."
ST_IN_REM    = 2   # after REM but before ':' or eol
ST_IN_IMAGE  = 3   # inside image (%) statement

def listProgramRecord(secData, secnum, listd):
    # type: (bytearray, str, bool) -> List[str]

    listing = []

    if (secData[0] & 0xc0) == 0x40:
        # header record
        name = headerName(secData)
        if (secData[0] & 0x10) == 0x10:
            ftype = ', protected file'
        else:
            ftype = ''
        listing.append("# %s, program filename = '%s'%s" % (secnum, name, ftype))
        return listing

    if (secData[0] & 0xc0) != 0x00:
        # data record
        # FIXME: maybe just do a hex dump?
        listing.append("# %s doesn't look like a program record" % secnum)
        return listing

    # program body record
    cp = 1            # skip control byte
    line = ""
    state = ST_ATOMIC
    while (cp < 255) and (secData[cp] != 0xFD) and (secData[cp] != 0xFE):
        c = secData[cp]

        if c == 0x0D:
            listing.append(line)
            line = ""
            state = ST_ATOMIC
            cp += 3  # 0x0D, then two 00 bytes after line end
            continue

        if c < 128:
            line += chr(c)
        elif c == 0xFF:
            # line number
            linenum = 1000*(secData[cp+1] // 16) + \
                       100*(secData[cp+1]  % 16) + \
                        10*(secData[cp+2] // 16) + \
                         1*(secData[cp+2]  % 16)
            cp += 2
            line += "%d" % linenum
        elif (state != ST_ATOMIC) or token[c] == '':
            line += "\\x%02x" % c
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

    if listd:
        width = 78
        basic2 = True   # Wang BASIC and BASIC-2 have a few differences
        newlisting = []
        for line in listing:
            newlisting.extend(prettyprint(line, width, basic2))
        return newlisting

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
                else:
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
            else:
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
# given a list of file blocks for a structured data file (that is,
# produced by DATASAVE), produce a human readable listing

def listDataFromBlocks(blocks):
    # type List[bytearray] -> List[str]

    secOffset = -1
    logicalRecNum = 0
    listing = []

    for secData in blocks[0:-1]: # skip the last sector
        secOffset += 1
        c0 = secData[0]
        c1 = secData[1]
        # print(">>> sector control bytes 0x%02X 0x%02X" % (c0, c1))
        if (c0 & 0xA0) == 0xA0:
            listing.append('### trailer record; done')
            return listing
        if (c0 & 0x80) == 0:
            listing.append('Sector %d of data file has bad header byte 0x%02X' % (secOffset, c0))
            return listing
        if c1 == 0x01:
            listing.append('### Logical Record %d' % logicalRecNum)
            logicalRecNum += 1

        listing.extend(listDataFromOneRecord(secData))

    return listing

########################################################################
# given a one sector from a structured data file (that is, produced by
# DATASAVE), produce a human readable listing
# returns

def listDataFromOneRecord(secData):
    # type: (bytearray) -> List[str]

    listing = []

    cp = 2       # skip control bytes
    while (cp < 255) and (secData[cp] < 0xFC):
        c = secData[cp]

        if c == 0x08:  # numeric
            if cp+1+8 > 254:  # 254 because we still need a terminator
                listing.append("# SOV indicates a number, but there aren't enough bytes left in the sector")
                return listing
            fp = secData[cp+1 : cp+9]
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
            return listing

        else:
            strlen = c-128
            if cp+1+strlen > 254:  # 254 because we still need a terminator
                listing.append("# SOV indicates string length that doesn't fit in sector")
                return listing
            dataobj = secData[cp+1 : cp+1+strlen]
            # expand non-ascii characters
            tmp = [chr(byt) if (32 <= byt < 128) else ("\\x%02X" % byt) for byt in dataobj]
            strng = ''.join(tmp)
            listing.append('"' + strng + '"')
            cp += 1 + strlen

    return listing

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
    pp("40 % this an #### image : statement", 80, True)
    pp("40 % this an #### image : statement", 80, False)
    pp('20 PRINT "Hi there":PRINT 1+2')
    pp('960 PRINT HEX(0C):PRINT A$;S1(1),S(1);TAB(41);S(2);TAB(57);S1(2);" ";STR(A0$,1,1);:PRINT :RETURN ')
