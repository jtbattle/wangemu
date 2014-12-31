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

import re

########################################################################
# table of BASIC atoms

def initBasicTokens():
    global token
    token = [None] * 256
    token[0x80] = "LIST ";
    token[0x81] = "CLEAR ";
    token[0x82] = "RUN ";
    token[0x83] = "RENUMBER ";
    token[0x84] = "CONTINUE ";
    token[0x85] = "SAVE ";
    token[0x86] = "LIMITS ";
    token[0x87] = "COPY ";
    token[0x88] = "KEYIN ";
    token[0x89] = "DSKIP ";
    token[0x8A] = "AND ";
    token[0x8B] = "OR ";
    token[0x8C] = "XOR ";
    token[0x8D] = "TEMP";
    token[0x8E] = "DISK ";
    token[0x8F] = "TAPE ";

    token[0x90] = "TRACE ";
    token[0x91] = "LET ";
    token[0x92] = "FIX(";        # BASIC-2
    token[0x93] = "DIM ";
    token[0x94] = "ON ";
    token[0x95] = "STOP ";
    token[0x96] = "END ";
    token[0x97] = "DATA ";
    token[0x98] = "READ ";
    token[0x99] = "INPUT ";
    token[0x9A] = "GOSUB ";
    token[0x9B] = "RETURN ";
    token[0x9C] = "GOTO ";
    token[0x9D] = "NEXT ";
    token[0x9E] = "FOR ";
    token[0x9F] = "IF ";

    token[0xA0] = "PRINT ";
    token[0xA1] = "LOAD ";
    token[0xA2] = "REM ";
    token[0xA3] = "RESTORE ";
    token[0xA4] = "PLOT ";
    token[0xA5] = "SELECT ";
    token[0xA6] = "COM ";
    token[0xA7] = "PRINTUSING ";
    token[0xA8] = "MAT ";
    token[0xA9] = "REWIND ";
    token[0xAA] = "SKIP ";
    token[0xAB] = "BACKSPACE ";
    token[0xAC] = "SCRATCH ";
    token[0xAD] = "MOVE ";
    token[0xAE] = "CONVERT ";
    token[0xAF] = "PLOT ";       # [SELECT] PLOT

    token[0xB0] = "STEP ";
    token[0xB1] = "THEN ";
    token[0xB2] = "TO ";
    token[0xB3] = "BEG ";
    token[0xB4] = "OPEN ";
    token[0xB5] = "CI ";         # [SELECT] CI
    token[0xB6] = "R ";          # [SELECT] R
    token[0xB7] = "D ";          # [SELECT] D
    token[0xB8] = "CO ";         # [SELECT] CO
    token[0xB9] = "LGT(";        # BASIC-2 only
    token[0xBA] = "OFF ";
    token[0xBB] = "DBACKSPACE ";
    token[0xBC] = "VERIFY ";
    token[0xBD] = "DA ";
    token[0xBE] = "BA ";
    token[0xBF] = "DC ";

    token[0xC0] = "FN";
    token[0xC1] = "ABS(";
    token[0xC2] = "SQR(";
    token[0xC3] = "COS(";
    token[0xC4] = "EXP(";
    token[0xC5] = "INT(";
    token[0xC6] = "LOG(";
    token[0xC7] = "SIN(";
    token[0xC8] = "SGN(";
    token[0xC9] = "RND(";
    token[0xCA] = "TAN(";
    token[0xCB] = "ARC";
    token[0xCC] = "#PI";
    token[0xCD] = "TAB(";
    token[0xCE] = "DEFFN";
    token[0xCF] = "TAN(";      # ARCTAN( gets encoded as CBCF sequence

    token[0xD0] = "SIN(";      # ARCSIN( gets encoded as CBD0 sequence
    token[0xD1] = "COS(";      # ARCCOS( gets encoded as CBD1 sequence
    token[0xD2] = "HEX(";
    token[0xD3] = "STR(";
    token[0xD4] = "ATN(";
    token[0xD5] = "LEN(";
    token[0xD6] = "RE";         # used by REDIM?
    token[0xD7] = "#";          # [SELECT] #
    token[0xD8] = "%";          # % [image]
    token[0xD9] = "P";          # [SELECT] P
    token[0xDA] = "BT";
    token[0xDB] = "G";          # [SELECT] G
    token[0xDC] = "VAL(";
    token[0xDD] = "NUM(";
    token[0xDE] = "BIN(";
    token[0xDF] = "POS(";

    token[0xE0] = "LS=";
    token[0xE1] = "ALL";
    token[0xE2] = "PACK";
    token[0xE3] = "CLOSE";
    token[0xE4] = "INIT";
    token[0xE5] = "HEX";        # eg HEXPRINT, not HEX(
    token[0xE6] = "UNPACK";
    token[0xE7] = "BOOL";
    token[0xE8] = "ADD";
    token[0xE9] = "ROTATE";
    token[0xEA] = "$";          # $[statement]
    token[0xEB] = "ERROR";
    token[0xEC] = "ERR";        # BASIC-2 only
    token[0xED] = "DAC ";       # BASIC-2 only
    token[0xEE] = "DSC ";       # BASIC-2 only
    token[0xEF] = "SUB";        # BASIC-2 only

    token[0xF0] = "LINPUT ";    # BASIC-2 only
    token[0xF1] = "VER(";       # BASIC-2 only
    token[0xF2] = " ELSE ";     # BASIC-2 only
    token[0xF3] = "SPACE";      # BASIC-2 only
    token[0xF4] = "ROUND(";     # BASIC-2 only
    token[0xF5] = "AT(";        # BASIC-2 only
    token[0xF6] = "HEXOF(";     # BASIC-2 only
    token[0xF7] = "MAX(";       # BASIC-2 only
    token[0xF8] = "MIN(";       # BASIC-2 only
    token[0xF9] = "MOD(";       # BASIC-2 only

    # all others are undefined
    return

initBasicTokens()

########################################################################
# given a header record, return the filename it contains.
# NB: disk headers pad names shorter than 8 characters with 0x20
#     tape headers pad names shorter than 8 characters with 0xFF

def headerName(sector):
    name = sector[1:9]
    while len(name) > 0:
        if (name[-1] != chr(0xff)) and (name[-1] != ' '):
            break
        name = name[:-1]
    return name

########################################################################
# given a list of file blocks, return a program file listing

def listProgramFromBlocks(blocks, listd, abs_sector=None):

    listing = []
    relsector = -1  # sector number, relative to first block

    for secData in (blocks):

        relsector = relsector + 1
        if isinstance(abs_sector, int):
            secnum = "sector %d" % (relsector + abs_sector)
        else:
            secnum = "relative sector %d" % relsector
        #listing.append( "============== %s ==============" % secnum )

        if (ord(secData[0]) & 0xc0) == 0x40:
            # header record
            name = headerName(secData)
            listing.append("# Sector %s, program filename = '%s'" % (secnum, name))
            continue

        if (ord(secData[0]) & 0xc0) != 0x00:
            # data record
            listing.append("# %s doesn't look like a program record" % secnum)
            continue

        listing.extend( listProgramBodyRecord(secData, listd) )

    return listing

########################################################################
# list the program from just one record, optionally prettyprinted

def listProgramBodyRecord(secData, listd):

    listing = []

    if (ord(secData[0]) & 0xc0) == 0x40:
        # header record
        name = headerName(secData)
        listing.append("# Program filename = '%s'" % name)
        return listing

    if (ord(secData[0]) & 0xc0) != 0x00:
        # data record
        listing.append("# this doesn't look like a program record")
        return listing

    # program body record
    cp = 1            # skip control byte
    line = ""
    while (cp < 255) and (secData[cp] != chr(0xFD)) and (secData[cp] != chr(0xFE)):
        c = secData[cp]
        oc = ord(c)
        if oc == 0x0D:
            cp += 2  # there are two 00 bytes after line end
            listing.append(line)
            line = ""
        elif (oc < 128):
            line += c
        elif oc == 0xFF:
            # line number
            linenum = 1000*(ord(secData[cp+1]) / 16) + \
                       100*(ord(secData[cp+1]) % 16) + \
                        10*(ord(secData[cp+2]) / 16) + \
                         1*(ord(secData[cp+2]) % 16)
            cp += 2
            line += "%d" % linenum
        elif token[oc] == None:
            line += "\\x%02x" % oc
        else:
            line += token[oc]
        cp += 1

    if (listd):
        width = 78
        basic2 = True   # Wang BASIC and BASIC-2 have a few differences
        newlisting = []
        for line in listing:
            newlisting.extend(prettyprint(line, width, basic2))
        return newlisting
    else:
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

def prettyprint(origline, width=80, basic2=True):
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
    while (len(line) > 0):

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
                    stmt_end = tmatch.end(1)
                break

            # scan until end of statement, watching out for quotes
            line_len = len(line)
            while (stmt_end < line_len):
                #print "looking at:%s" % line[stmt_end:]

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
                #print "find closing quote in:%s" % line[stmt_end:]
                tmatch = re.search(pat, line[stmt_end:])
                if tmatch:
                    #print "yes"
                    stmt_end += tmatch.end(1)
                    #print "remainder:%s" % line[stmt_end:]
                    continue
                else:
                    # didn't find matching closing quote
                    #print "no"
                    stmt_end = len(line)
                    break

            # end while (stmt_end < line_len)
            break

        # end while True

        # we should have found the end of the next statement at this point
        assert (stmt_end > 0)

        if (stmt_end < len(line)) and (line[stmt_end] != ':'):
            # confused -- the end of statement wasn't the end of the line,
            # nor was it followed by ":".  just tack on the remainder
            stmt_end = len(line)

        newline += line[0:stmt_end]  # grab statement
        # wrap listing if needed
        while (newline != ''):
            if (len(newline) <= width):
                listing.append(newline)
                break
            else:
                listing.append(newline[0:width])
                if (width > 6):
                    newline = "     " + newline[width:]
                else:
                    newline = newline[width:]

        line = line[stmt_end:]       # remainder of line
        if (line != ''):
            if (line[0] == ':'):
                line = line[1:]
                newline = "   : "
                if (line != '' and line[0] == ' '):
                    # chomp one whitespace because we added it already after "    : "
                    line = line[1:]
            else:
                assert (line == '')      # we must be done by now

    # end while (len(line) > 0):

    return listing

########################################################################
# given a list of file blocks for a structured data file (that is,
# produced by DATASAVE), produce a human readable listing

def listDataFromBlocks(blocks):

    secOffset = -1
    logicalRecNum = 0
    listing = []

    for secData in (blocks[0:-1]): # skip the last sector
        secOffset += 1
        c0 = secData[0] ; oc0 = ord(c0)
        c1 = secData[1] ; oc1 = ord(c1)
        # print ">>> sector control bytes 0x%02X 0x%02X" % (oc0, oc1)
        if (oc0 & 0xA0) == 0xA0:
            listing.append( '### trailer record; done' )
            return listing
        if (oc0 & 0x80) == 0:
            listing.append( 'Sector %d of data file has bad header byte 0x%02X' % (secOffset, oc0) )
            return listing
        if oc1 == 0x01:
            listing.append( '### Logical Record %d' % logicalRecNum )
            logicalRecNum += 1

        listing.extend( listDataFromOneRecord(secData) )

    return listing

########################################################################
# given a one sector from a structured data file (that is, produced by
# DATASAVE), produce a human readable listing
# returns 

def listDataFromOneRecord(secData):

    listing = []

    cp = 2       # skip control bytes
    while (cp < 255) and (ord(secData[cp]) < 0xFC):
        c = secData[cp] ; oc = ord(c)

        if oc == 0x08:  # numeric
            if cp+1+8 > 254:  # 254 because we still need a terminator
                listing.append( "# SOV indicates a number, but there aren't enough bytes left in the sector" )
                return listing
            fp = secData[cp+1 : cp+9]
            expSign  = (ord(fp[0]) & 0x80) != 0
            mantSign = (ord(fp[0]) & 0x10) != 0
            exp =  1 * ((ord(fp[0]) >> 0) & 0xF) \
                + 10 * ((ord(fp[1]) >> 4) & 0xF)
            mant = ("%d" % ((ord(fp[1]) >> 0) & 0xF)) + "."
            for d in fp[2:8]:
                mant += ("%02X" % ord(d))
            fpascii = (" ", "-")[mantSign] + mant + \
                      ("+", "-")[ expSign] + ("%02X" % exp)
            listing.append( fpascii )
            cp += 9

        elif (oc < 128):
            listing.append( "Unexpected SOV value of 0x%02X" % oc )
            return listing

        else:
            strlen = oc-128
            if cp+1+strlen > 254:  # 254 because we still need a terminator
                listing.append( "# SOV indicates string length that doesn't fit in sector" )
                return listing
            strng = secData[cp+1 : cp+1+strlen]
            # expand non-ascii characters
            foo = [None] * len(strng)
            for n in xrange(0,len(strng)):
                if (ord(strng[n]) < 32) or (ord(strng[n]) >= 128):
                    foo[n] = "\\x%02X" % ord(strng[n])
                else:
                    foo[n] = strng[n]
            strng = ''.join(foo)
            listing.append( '"' + strng + '"' )
            cp += 1 + strlen

    return listing

########################################################################
# testing

def pp(line, width=80, basic2=True):
    listing = prettyprint(line, width, basic2)
    for txt in listing:
        print txt

if __name__ == "__main__":
    pp("10 REM")
    pp("20 REM this is a test:PRINT 12+34")
    pp("30 REM this is a really really really really really really really really really really really really really really really really really really really long statement", 64)
    pp("40 % this an #### image : statement", 80, True)
    pp("40 % this an #### image : statement", 80, False)
    pp('20 PRINT "Hi there":PRINT 1+2')
    pp('960 PRINT HEX(0C):PRINT A$;S1(1),S(1);TAB(41);S(2);TAB(57);S1(2);" ";STR(A0$,1,1);:PRINT :RETURN ')
