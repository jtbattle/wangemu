# Program: wvdutil.py
# Purpose: read a .wvd (wang virtual disk file) and report what's in it
# Author:  Jim Battle
# Version: 1.2, 2008/08/18, JTB
# Version: 1.3, 2011/08/13, JTB
#     added program prettyprinting, zip file reading
# Version: 1.4, 2011/10/08, JTB
#     'cat' output sort is sorted, with optional flags
#     'list' now has 'list <start> <end>' form
#     'scan' command has been added
#     'compare' now takes a '-v' verbose flag
#     ask if user really wants to exit if there are unsaved changes.
# Version: 1.5, 2018/01/01, JTB
#     made it python 2/3 compatible
#     using bytearray data instead of character strings
#     pylint cleanups, mypy type annotation

########################################################################
# there are any number of operations that could be provided by this
# program, but things which can easily be done on the emulator (e.g.,
# "COPY FR" to squish out scratched files) are not duplicated here.

# FIXME:
#    see the scattered FIXME comments throughout
#    make registerCmd() a decorator instead of an explicit call
#       however, python2 doesn't allow class decorators.
#    before any command that performs state change,
#       warn if the disk is write protected (and suggest 'wp' command)
#    MVP OS 2.6 and later have files with names like "@PM010V1" which are
#       printer control files.  they all start with 0x6911.
#       add logic to allow check, scan, and perhaps list them.
#       Basic2Release_2.6_SoftwareBullitin.715-0097A.5-85.pdf
#       has some description of the generalized printer driver.
#    newsletter_no_5.pdf, page 5, describes via a program "TC" file format.
#       what is this?  should I add disassembly of such files?
#
# Syntax for possible new commands:
#    wvd> disk <filename>         resets the current disk being operated on
#    wvd> extract <filename>      (as binary file)
#    wvd> extract sector(s) a(,b) (as binary file)
#
# Reminders (so I don't need to figure these out again later):
#    checking type annotations:
#        py -m mypy wvdutil.py
#    pylint checker:
#        py [-2] -m pylint -f msvs wvdutil.py
#    pychecker checker:
#        uncomment the "import pychecker" comment below, then run the program

from __future__ import print_function
from builtins import input                                     # pylint: disable=redefined-builtin
from typing import List, Dict, Callable, Any, Optional, Tuple  # pylint: disable=unused-import

#import pychecker.checker  # static lint tool
import sys
import os.path
import re

try:
    # python 3
    from urllib.parse import quote_plus, unquote_plus  # type: ignore
except ImportError:
    # python 2
    from urllib import quote_plus, unquote_plus        # type: ignore

from wvdlib import (WangVirtualDisk, WvdFilename, CatalogFile,   # pylint: disable=unused-import
                    guessFiles, checkPlatter, checkFile,
                    checkProgramHeaderRecord, checkProgramBodyRecord)

from wvfilelist import (listProgramFromBlocks, listDataFromOneRecord, listDataFromBlocks)

debug = False
if debug:
    import pdb

# when working with multiplatter disks, commands may specify which platter
# to operate on.  if a platter isn't specified, the default value comes
# from this variable.  it is set by the "platter <n>" command.
# the value the user sees has an origin of 1, but the internal code uses
# an origin of 0.
# global
default_platter = 0

# global
generate_html = False

############################ reportMetadata ############################
# report information about the disk

def reportMetadata(wvd):
    # type: (WangVirtualDisk) -> None
    wpList = ('no', 'yes')
    mediaList = ('5.25" floppy', '8" floppy', '2260 hard disk', '2280 hard disk')

    #print("write format:    ", wvd.getWriteFormat())
    #print("read format:     ", wvd.getReadFormat())
    print("filename:       ", wvd.getFilename())
    print("write protect:  ", wpList[wvd.getWriteProtect()])
    print("platters:       ", wvd.numPlatters())
    print("sectors/platter:", wvd.numSectors())
    print("media type:     ", mediaList[wvd.mediaType()])
    print("label:          ")
    for line in wvd.label().splitlines():
        print("    ", line)

################################# catalog #################################
# report list of files on the given platter of the disk, with optional
# wildcard list.  wildcards are dos-like:
#    "*" means any number of characters
#    "?" means any one character

def catalog(wvd, p, flags, wcList):
    # type: (WangVirtualDisk, int, Dict[str,bool], List[str]) -> None
    # pylint: disable=too-many-branches, too-many-locals

    cat = wvd.catalog[p]
    if not cat.hasCatalog():
        print("This disk doesn't appear to have a catalog")
        return

    filelist = cat.expandWildcards(wcList)  # List[WvdFilename]

    if flags['s']:  # sort by start address
        filelist.sort(key=lambda name: wvd.catalog[p].getFile(name).getStart())
    elif flags['u']:  # sort by number of used sectors (size)
        filelist.sort(key=lambda name: wvd.catalog[p].getFile(name).getUsed())
    else: # sort by name:
        filelist.sort(key=lambda name: name.get())

    if flags['r']:  # reverse order
        filelist.reverse()

    # print header and disk information
    index_mark = " '&"[cat.getIndexType()]
    print("INDEX SECTORS = %05d%c" % (cat.numIndexSectors(), index_mark))
    print("END CAT. AREA = %05d" % cat.endCatalogArea())
    print("CURRENT END   = %05d" % cat.currentEnd())
    print()
    print("NAME     TYPE  START   END   USED   FREE")

    for name in filelist:
        curFile = cat.getFile(name)
        if curFile != None:
            # check file for extra information
            status = []
            saveMode = curFile.programSaveMode()
            if saveMode != None and saveMode != "normal":
                status.append(saveMode)

            bad = checkFile(wvd, p, name, report=False)
            if bad:
                status.append("non-standard or bad format")

            extra = ""
            if status:
                extra = "   [%s]" % (', '.join(status))

            # report it
            fname    = curFile.getFilename()
            fstatus  = curFile.getStatus()
            start    = curFile.getStart()
            end      = curFile.getEnd()
            if curFile.fileExtentIsPlausible():
                used = curFile.getUsed()
                free = curFile.getFree()
            else:
                (used, free) = (0, 0)

            str_fname = fname.asStr()
            if generate_html:
                linkname = "vmedia.html?diskname=" \
                         + quote_plus(wvd.getFilename()) \
                         + "&filename=" \
                         + quote_plus(str_fname) \
                         + "&prettyprint=1"
                print("<a href=\"%s\">%-8s</a>  %s   %05d  %05d  %05d  %05d%s" % \
                    (linkname, str_fname, fstatus, start, end, used, free, extra))
            else:
                print("%-8s  %s   %05d  %05d  %05d  %05d%s" % \
                    (str_fname, fstatus, start, end, used, free, extra))

############################ (un)protect files ############################
# given a program filename (or wildcard list), change the protection
# flags to make the program protected or unprotected

def setProtection(wvd, p, protect, wcList=None):
    # type: (WangVirtualDisk, int, bool, List[str]) -> None
    wcList = wcList or ['*']

    # FIXME: make sure disk isn't write protected

    for name in wvd.catalog[p].expandWildcards(wcList):
        curFile = wvd.catalog[p].getFile(name)
        assert curFile != None
        if curFile.getType() != 'P':
            print('"%s" is not a program file' % name.rstrip())
        else:
            saveMode = curFile.programSaveMode()
            if saveMode == 'scrambled':
                print('"%s" is a scrambled (SAVE !) file' % str(name).rstrip())
                print("This program doesn't know how to crack such files")
            else:
                curFile.setProtection(protect)

########################### compare disks/files ###########################
# wvd>compare diskimage2.wvd [<wildcard list>]
# report if:
#   1) disk images, including metadata, match exactly (quit if so)
#   2) disk images, excluding metadata, match exactly (quit if so)
#   foreach file in (unique union of both catalogs):
#       3) report if a file appears on one disk or other only
#       4) report if a files don't have the same type or status
#       5) report if a files compare exactly (for now)
#       6) show diff report ? (future enhancement)
#           using python difflib? (I have it)

# FIXME: now we have multiplatter things to deal with
#    1) as now, operate on whole disks: "compare disk2.wvd"
#          the code below only compares platter 0 to platter 0
#    2) compare two platters of one disk: "compare:1 :2"
#    3) compare two platters of two disks: "compare:1 udisk2.wvd:2"
#    4) take foo.wvd.  set the file mode to protected on one file
#       and save it to bar.wvd.  then compare foo.wvd bar.wvd.
#       nothing is reported -- because the metadata matches, and
#       all the file names and listsings match, but the sector
#       data is different.

def compareFiles(wvd, disk1_p, wvd2, disk2_p, verbose, args):
    # type: (WangVirtualDisk, int, WangVirtualDisk, int, bool, List[str]) -> None
    # pylint: disable=too-many-arguments, too-many-locals, too-many-statements, too-many-branches
    if args:
        wildcardlist = args
    else:
        wildcardlist = ['*']

    wvdname  = '"' + wvd.getFilename() + '"'
    wvd2name = '"' + wvd2.getFilename() + '"'

    same_disk = (wvdname == wvd2name)

    # if we are operating on two disks, first check if the disks themselves
    # are an exact match, as that answers all other questions
    if not same_disk:
        sameMetadata = (wvd.getWriteFormat()  == wvd2.getWriteFormat())  and \
                       (wvd.getReadFormat()   == wvd2.getReadFormat())   and \
                       (wvd.getWriteProtect() == wvd2.getWriteProtect()) and \
                       (wvd.numPlatters()     == wvd2.numPlatters())     and \
                       (wvd.numSectors()      == wvd2.numSectors())      and \
                       (wvd.mediaType()       == wvd2.mediaType())       and \
                       (wvd.label()           == wvd2.label())

        sameSectors = (wvd.numPlatters() == wvd2.numPlatters()) and \
                      (wvd.numSectors()  == wvd2.numSectors())
        if sameSectors:
            for p in range(wvd.numPlatters()):
                for n in range(wvd.numSectors()):
                    sec1 = wvd.getRawSector(p, n)
                    sec2 = wvd2.getRawSector(p, n)
                    if sec1 != sec2:
                        sameSectors = False
                        break

        # 1) disk images, including metadata, match exactly (quit if so)
        if sameMetadata and sameSectors:
            print("The two disks appear to be identical")
            return

        # 2) disk images, excluding metadata, match exactly (quit if so)
        if sameSectors:
            print("The two disks have different metadata, but identical sectors")
            return

    # python 2.2 doesn't have sets module nor set built-in
    # so this will be a little verbose
    fileset1x = wvd.catalog[disk1_p].expandWildcards(wildcardlist)
    fileset1 = [f.asStr().rstrip() for f in fileset1x]
    fileset2x = wvd2.catalog[disk2_p].expandWildcards(wildcardlist)
    fileset2 = [f.asStr().rstrip() for f in fileset2x]
    #print("fileset1: ", ["'%s', " % x for x in fileset1])
    #print("fileset2: ", ["'%s', " % x for x in fileset2])

    # 3) report if a file appears on one disk or other only
    wvd1only = []
    for f in fileset1:
        if f not in fileset2:
            wvd1only.append(f)

    wvd2only = []
    for f in fileset2:
        if f not in fileset1:
            wvd2only.append(f)

    if (wvd1only == fileset1) and (wvd2only == fileset2):
        print("These disks have no files in common")
        return

    if wvd1only:
        wvd1only.sort()
        if same_disk:
            print("Files on platter %d and not platter %d:" % (disk1_p+1, disk2_p+1))
        else:
            print("Files in %s and not in %s:" % (wvdname, wvd2name))
        print(wvd1only)

    if wvd2only:
        wvd2only.sort()
        if same_disk:
            print("Files on platter %d and not platter %d:" % (disk2_p+1, disk1_p+1))
        else:
            print("Files in %s and not in %s:" % (wvd2name, wvdname))
        print(wvd2only)

    # 4) report if a files don't have the same type or status
    # 5) report if a files compare exactly (for now)
    # 6) show diff report ? (future enhancement)

    wvdMatching = []
    wvdMismatching = []
    for f in fileset1x: # pylint: disable=too-many-nested-blocks

        file1 = wvd.catalog[disk1_p].getFile(f)
        assert file1 != None
        file1Data = file1.getSectors()

        if f in fileset2x:

            file2 = wvd2.catalog[disk2_p].getFile(f)
            assert file2 != None
            file2Data = file2.getSectors()

            matching = False
            if file1Data == file2Data:
                matching = True
            elif (file1.getType() == 'P' and \
                  file2.getType() == 'P' and \
                  file1.programSaveMode() in ['normal', 'protected'] and \
                  file1.programSaveMode() == file2.programSaveMode()):
                # there are bytes which don't matter after the EOB/EOD byte
                # of each sector which can create a false mismatch via an
                # exact binary match. here we crete a listing if we can and
                # compare those.
                listing1 = listProgramFromBlocks(file1Data, False, 0)
                listing2 = listProgramFromBlocks(file2Data, False, 0)
                # the first line of the listing contains the secotr number,
                # so we need to ignore that
                #listing1 = listing1[1:]
                #listing2 = listing1[2:]
                matching = listing1 == listing2
                if not matching and verbose:
                    if len(listing1) != len(listing2):
                        print("%s mismatch: left has %d lines, right has %d lines" \
                             % (f.asStr(), len(listing1), len(listing2)))
                    for n in range(min(len(listing1), len(listing2))):
                        if listing1[n] != listing2[n]:
                            print("%s has first mismatch at line %d" % (f.asStr(), n+1))
                            print(" left>", listing1[n])
                            print("right>", listing2[n])
                            break

            if matching:
                wvdMatching.append(f.asStr())  # exact binary match
            else:
                wvdMismatching.append(f.asStr())

    if wvdMismatching:
        wvdMismatching.sort()
        print("Mismatching files:")
        print([fname for fname in wvdMismatching])

    if wvdMatching:
        wvdMatching.sort()
        print("Matching files:")
        print([fname for fname in wvdMatching])

###################### dump file or range of sectors ######################
# dump all sectors of a file or a specified range of sectors in hex and ascii

def dumpFile(wvd, p, args):
    # type: (WangVirtualDisk, int, List[str]) -> None
    (theFile, start, end) = filenameOrSectorRange(wvd, p, args)  # pylint: disable=unused-variable
    if start is None:
        return # invalid arguments

    for s in range(start, end+1):
        print("### Disk Sector", s, "###")
        dumpSector(wvd.getRawSector(p, s))
    return

def dumpSector(data):
    # type: (bytearray) -> None
    for line in range(16):
        print("%02x:" % (line*16), end=' ')
        for off in range(16):
            x = 16*line + off
            print("%02x" % data[x], end=' ')
        asc = ''
        for off in range(16):
            x = 16*line + off
            if (data[x] >= ord(' ')) and (data[x] < 128):
                asc += chr(data[x])
            else:
                asc += "."
        print(" ", asc)

######################## list program or data file ########################
# given a file name, return a list containing one text line per item
# LIST <FILENAME>
# LIST <WILDCARD>  (but it must match just one file)
# LIST 100         (list sector 100)
# LIST 100 200     (list sectors 100 to 200, inclusively)

def listFile(wvd, p, args, listd):
    # type: (WangVirtualDisk, int, List[str], bool) -> List[str]
    # pylint: disable=too-many-return-statements

    (theFile, start, end) = filenameOrSectorRange(wvd, p, args)
    if start is None:
        return []  # invalid arguments

    if theFile is not None:
        # make sure it is a program or data file
        fileType = theFile.getType()
        if fileType == "D":
            return listDataFromBlocks(theFile.getSectors())
        if fileType != "P":
            print("Not a program file")
            return []
        if not theFile.fileExtentIsPlausible():
            print("The file extent is not sensible; no listing generated")
            return []
        if not theFile.fileControlRecordIsPlausible():
            print("The file control record is not sensible; no listing generated")
            return []
        # make sure program isn't scrambled
        mode = theFile.programSaveMode()
        if mode == 'scrambled':
            print("Program is scrambled (SAVE !) and can't be listed")
            return []
        blocks = theFile.getSectors()
        listing = listProgramFromBlocks(blocks, listd, start)
    else:
        # start,end range given
        blocks = []
        for n in range(start, end+1):
            blocks.append(wvd.getRawSector(p, n))
        #listing = listProgramFromBlocks(blocks, listd, start)
        listing = listArbitraryBlocks(blocks, listd, start)

    return listing

############################# command parser #############################

# cmdSet is a hash keyed on the command name, storing a command handler instance
cmdSet = {}          # type: Dict[str,cmdTemplate]
cmdSetAliases = {}   # type: Dict[str,cmdTemplate]

def registerCmd(cmdInst):
    # type: (cmdTemplate) -> None
    global cmdSet  # pylint: disable=global-statement
    name = cmdInst.name()
    # names can contain aliases as a "|"-separated list
    names = name.split("|")
    primary = 1
    for n in names:
        if n in cmdSet or n in cmdSetAliases:
            print("Command", n, "defined twice!")
            sys.exit()
        if primary:
            cmdSet[n] = cmdInst
        else:
            cmdSetAliases[n] = cmdInst
        primary = 0

def cmdUsage(cmdName=None):
    # type: (Optional[str]) -> None
    if cmdName is None:
        print()
        print('Usage:')
        print('   ', basename, '<filename> [<command>[/<platter>] [<args>]] [<redirection>]')
        print()
        print('With no command, enter into command line mode, where each subsequent line')
        print('contains one of the commands, as described below.  The output can be optionally')
        print('redirected to the named logfile, either overwriting, ">" or appending, ">>".')
        print('The output can also be piped through a pager by ending with "| more".')
        print('Arguments containing spaces can be surrounded by double quote characters.')
        print()
        print('For multiplatter disks, the platter can be specified by following the command')
        print('name with a slash and the decimal platter number.  For commands where it makes')
        print('sense, "/all" can be specified to operate on all platters.')
        print()
        print('<redirection> is optional, and takes one of three forms:')
        print('   ... >   <logfile>  -- write command output to a logfile')
        print('   ... >>  <logfile>  -- append command output to a logfile')
        print('   ... | more         -- send command output through a pager')
        print()
        print('Type "help <command>" to get more detailed help on a particular command.')
        print()
        keys = list(cmdSet.keys())
        keys.sort()
        for k in keys:
            print('   %-9s - %s' % (k, cmdSet[k].shortHelp()))
        print()
        return

    if cmdName in cmdSet:
        print(cmdSet[cmdName].longHelp())
        names = cmdSet[cmdName].name().split("|")
        if len(names) > 1:
            print("Command aliases:", ", ".join(names))
        return

    if cmdName in cmdSetAliases:
        print(cmdSetAliases[cmdName].longHelp())
        names = cmdSetAliases[cmdName].name().split("|")
        if len(names) > 1:
            print("Command aliases:", ", ".join(names))
        return

    else:
        print('No command "' + cmdName + '"')
        return

# report all commands and optionally quit
def usage(terminate=True):
    # type: (bool) -> None
    cmdUsage()
    if terminate:
        sys.exit()

# check if one or more of the specified platters has a catalog
# pass in a wvd and a list of platters that we want to inspect
# returns None if no platters exist, and a list containing a subset
# of the original platters which do have catalogs.
def pickUsefulCatalog(wvd, platters):
    # type: (WangVirtualDisk, List[int]) -> Optional[List[int]]
    # if the command requires a platter to have a catalog, check that
    # our list of target platters obeys
    if len(platters) == 1:
        p = platters[0]
        if not wvd.catalog[p].hasCatalog():
            if wvd.numPlatters() == 1:
                print("This disk doesn't appear to have a catalog")
            else:
                print("Platter %d doesn't appear to have a catalog" % (p+1))
            return None
        return platters
    else:  # multiplatter
        new_platters = []
        bad_platters = []  # ones without a catalog
        for p in platters:
            if wvd.catalog[platters[p]].hasCatalog():
                new_platters.append(p)
            else:
                bad_platters.append(str(p+1))
        if not new_platters:
            print("At least one platter must have a catalog for this command")
            return None
        if bad_platters:
            print("These platters have no catalog and will be skipped:", \
                ", ".join(bad_platters))
        return new_platters


# this wrapper makes it easier to clean up redirection after all commands
def cmdDispatch(wvd, cmd, args):
    # type: (WangVirtualDisk, str, List[str]) -> None
    # check if a platter has been specified, eg: "cat/2" or "cat/all"
    mo = re.match(r'([a-z]+)/(.+)$', cmd)
    platters = [default_platter]
    if mo is not None:
        cmd = mo.group(1)
        if mo.group(2).lower() == "all":
            # user is requesting operation on all platters
            platters = list(range(wvd.numPlatters()))
        elif mo.group(2).isdigit():
            # user is requesting operation on a specific platter
            p = int(mo.group(2))
            if (p < 1) or (p > wvd.numPlatters()):
                print("Platter %d specified; disk has %d platters" % (p, wvd.numPlatters()))
                return
            platters = [p-1]  # user sees 1 as first platter, code uses 0
        else:
            print("Bad platter specification '%s'" % mo.group(2))
            return

    # try and find a command handler
    if cmd in cmdSet:
        action = cmdSet[cmd]
    elif cmd in cmdSetAliases:
        action = cmdSetAliases[cmd]
    else:
        print('Unknown command:', cmd)
        print('Type "help" for the list of commands')
        return

    if action.needsCatalog():
        platters = pickUsefulCatalog(wvd, platters)
        if not platters:
            return

    # perform the command
    action.doCommand(wvd, platters, args)

# ----------------------------------------------------------------------

# given a disk image and a command, perform the command (or bitch)
def command(wvd, cmdString):
    # type: (WangVirtualDisk, str) -> None
    # pylint: disable=too-many-branches, too-many-statements, too-many-locals

    # because filenames may contain a space, we need to handle quoting
    # only doublequote is supported since wang filenames may contain a single quote
    quotedWordRe = r'"[^"]*"'        # double quotes surrounding word (possibly 0 length)
    wordRe = r'[^ "]+'               # string of non-space letters
    allRe = quotedWordRe + '|' + wordRe
    words = re.findall(allRe, cmdString.strip())

    # the redirection operators, ">", ">>", and "|", might be attached to
    # neighboring words.  split them off of not quoted words.
    words, old_words = [], words
    redir_op = re.compile(r'(>>|>|[|])')
    for word in old_words:
        if word[0] == '"': # quoted
            words.append(word)
        else:
            # pad spaces around redirection operators and then split
            words.extend(redir_op.sub(r' \1 ', word).split())

    # quoted strings still have their quotes around them
    words = [re.sub(r'^"(.*)"$', r'\1', word) for word in words]

    if not words:
        return
    cmd = words[0].lower()  # the command words is case insensitive

    # check for redirection
    origStdout = sys.stdout
    pager_file = None
    if '|' in words:
        # "| more" must be the final two words
        idx = words.index("|")
        if (idx != len(words)-2) or (words[idx+1] != "more"):
            print("Bad pipe command")
            return
        del words[-2:]  # strip off "| more"
        # using os.popen or subprocess.Popen didn't want to work, so just
        # dump the output to a temp file and page it later
        import tempfile
        tmp_fd, pager_file = tempfile.mkstemp()
        sys.stdout = os.fdopen(tmp_fd, 'w+b')
    elif ('>' in words) or ('>>' in words):
        if '>' in words:
            idx = words.index(">")
            mode = 'w'   # write
        else:
            idx = words.index(">>")
            mode = 'a'   # append
        if idx+2 != len(words):
            print('Error: Redirection ">" or ">>" expects to be followed by a single filename')
            return
        redirFile = words[idx+1]
        words = words[ :idx]     # chop off "> filename" part
        try:
            sys.stdout = open(redirFile, mode, 1)  # 1=buffered
        except IOError:
            sys.stdout = origStdout
            print("Couldn't open" + '"' + redirFile + '" for redirection')
            return

    try:
        cmdDispatch(wvd, cmd, words[1:])
    except KeyboardInterrupt:
        sys.stdout = origStdout
        if pager_file:
            os.unlink(pager_file)
        print("<<command interrupted>>")
        return

    sys.stdout = origStdout

    # page the output
    if pager_file:
        try:
            os.system("more " + pager_file)
        finally:
            os.unlink(pager_file)

    return

# ----------------------------------------------------------------------

# this is an abstract class that all command objects should override
class cmdTemplate(object):
    # pylint: disable=no-self-use
    def __init__(self):
        return
    def name(self):
        return "Overload name() in command object subclass"
    def shortHelp(self):
        return ["Overload shortHelp() in command object subclass"]
    def longHelp(self):
        return ["Overload longHelp() in command object subclass"]
    def needsCatalog(self):
        return False  # class returns true if it requires a catalog structure
    def doCommand(self, wvd, platters, args):
        # pylint: disable=unused-argument
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        return # Overload doCommand() in command object subclass

# ----------------------------------------------------------------------

class cmdCatalog(cmdTemplate):

    def name(self):
        return "cat|catalog|dir|ls"
    def shortHelp(self):
        return "display all or selected files on the disk"
    def longHelp(self):
        return \
"""cat [-s|-u] [-r] [<wildcard list>]
    List files on disk, or those matching any supplied wildcard.
    Wildcards use "?" to match one char, "*" to match no or many.
    Use flag "-s" to sort by starting address instead of name.
    Use flag "-u" to sort by used sector count intead of name.
    Use flag "-r" to reverse the sorting order."""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        flags = {'s': False, 'u' : False, 'r' : False}
        while args:
            if args[0] == '-s':
                flags['s'] = True
                flags['u'] = False
                args = args[1:]
            elif args[0] == '-u':
                flags['s'] = False
                flags['u'] = True
                args = args[1:]
            elif args[0] == '-r':
                flags['r'] = True
                args = args[1:]
            else:
                break
        for p in platters:
            if len(platters) > 1:
                if p != platters[0]:
                    print()
                print('-'*20, "Platter #%d" % (p+1), '-'*20)
            if not args:
                catalog(wvd, p, flags, ['*'])  # list all
            else:
                catalog(wvd, p, flags, args)   # wildcard version
        return

registerCmd(cmdCatalog())

# ----------------------------------------------------------------------

class cmdCheck(cmdTemplate):

    def name(self):
        return "check"
    def shortHelp(self):
        return "check the disk data structures for consistency"
    def longHelp(self):
        return \
"""check
    Inspect the disk and file data structures and report any integrity issues.
    However, if problems are found, nothing is fixed."""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if args:
            cmdUsage('check')
            return
        fail = False
        for p in platters:
            fail = checkPlatter(wvd, p, report=True)
            if fail:
                break
        if fail:
            print("Failed sanity check")
        else:
            print("Looks OK")
        return

registerCmd(cmdCheck())

# ----------------------------------------------------------------------

class cmdScan(cmdTemplate):

    def name(self):
        return "scan"
    def shortHelp(self):
        return "scan the disk and identify whole or partial program files"
    def longHelp(self):
        return \
"""scan
scan -all
scan -bad
    This scans the disk, ignoring the catalog, and attempts to identify program
    files based on sector contents, not the catalog.  If -all is specified,
    it will also report program fragments -- things which look like parts of
    programs, but which aren't fully valid.  Once identified, such fragments
    can be displayed using the 'list <start> <end>' command.  Finally, -bad
    specifies that only incomplete program fragments should be listed."""
    def needsCatalog(self):
        return False

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        # pylint: disable=too-many-branches
        flag_good = True
        flag_bad = False
        if args and (args[0] == '-all'):
            flag_good = True
            flag_bad = True
            args = args[1:]
        if args and (args[0] == '-bad'):
            flag_good = False
            flag_bad = True
            args = args[1:]
        if args:
            cmdUsage('scan')
            return
        for p in platters:
            if len(platters) > 1:
                print("======== platter %d ========" % p)
            blocks = []
            for n in range(wvd.numSectors()):
                blocks.append(wvd.getRawSector(p, n))
            files = guessFiles(blocks, 0)
            if not files:
                print("Didn't identify any program files")
            else:
                for fileinfo in files:
                    if flag_good and (fileinfo['file_type'] == 'program'):
                        print("Sector %5d to %5d: name='%s' [ program ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record'],
                               fileinfo['filename']))
                    if flag_bad and (fileinfo['file_type'] == 'program_headless'):
                        print("Sector %5d to %5d: [ no header record ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record']))
                    if flag_bad and (fileinfo['file_type'] == 'program_tailless'):
                        print("Sector %5d to %5d: [ no trailer record ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record']))
                    if flag_bad and (fileinfo['file_type'] == 'program_fragment'):
                        print("Sector %5d to %5d: [ no header nor trailer records ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record']))
                    if flag_good and (fileinfo['file_type'] == 'data'):
                        print("Sector %5d to %5d: name='%s' [ data ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record'],
                               fileinfo['filename']))
                    if flag_bad and (fileinfo['file_type'] == 'data_fragment'):
                        print("Sector %5d to %5d: name='%s' [ data fragment ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record'],
                               fileinfo['filename']))
        return

registerCmd(cmdScan())

# ----------------------------------------------------------------------

class cmdCompare(cmdTemplate):

    def name(self):
        return "compare"
    def shortHelp(self):
        return "Compare two disks or selected files on two disks"
    def longHelp(self):
        return \
"""compare[/<platter>] /<platter> [-v|-verbose] [<wildcard list>]
compare[/<platter>] disk2.wvd[/<platter>] [-v|-verbose] [<wildcard list>]

    In the first form, the files of two different platters of the same disk
    are compared.  For this to make any sense, the disk must be multiplatter.
    Files that appear on one platter and not the other are reported, files
    with the same name but mismatch are reported, and files with the same
    name and match exactly are also reported.  If a <wildcard list> is given,
    only files matching the list will be considered for comparison.

    In the second form, the files from two platters on two different disks
    are compared, again with an optional file wildcard list.  The second
    /<platter> specifier is optional, and defaults to platter 1.  For either
    disk, the /<platter> specifier is not meaningful for single platter disks.

    The -v or -verbose flag will indicate the first line where files
    mismatch, if they are program files and are not scrambled."""

    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        # pylint: disable=too-many-return-statements, too-many-branches
        # FIXME: this needs an overhaul
        if (len(args) < 1) or (len(platters) > 1):
            cmdUsage('compare')
            return

        # look for flags
        verbose = False
        argsout = []
        for arg in args:
            if arg in ('-v', '-verbose'):
                verbose = True
            else:
                argsout.append(arg)
        args = argsout

        disk1_p = platters[0]  # disk1 partition
        if not wvd.catalog[disk1_p].hasCatalog():
            print("Disk1 doesn't appear to have a catalog")
            return

        # foo.wvd/3:  group1=foo.wvd, group2=/3, group3=3
        # /3:         group1=,        group2=/3, group3=3
        # foo.wvd:    group1=foo.wvd, group2=None, group3=None
        mo = re.match(r'([^/]*)(/(\d+))?$', args[0])
        if mo is None:
            cmdUsage('compare')
            return
        disk2_name = mo.group(1)  # disk2 name, if any
        if mo.group(3) is None:
            disk2_p = 0
        else:
            disk2_p = int(mo.group(3))-1  # interally the first platter is 0

        # if a second disk file was specified, read it in
        if disk2_name:
            wvd2 = WangVirtualDisk()
            try:
                wvd2.read(disk2_name)
            except:  # pylint: disable=bare-except
                print("Couldn't open " + '"' + disk2_name + '" as a Wang virtual disk image')
                return
        else:
            # comparing two platters of the same multiplatter disk
            wvd2 = wvd

        if disk2_p >= wvd2.numPlatters():
            print("The second platter specifier is greater than %d" % wvd2.numPlatters())
            return
        if (not disk2_name) and (disk1_p == disk2_p):
            print("Please specify two different platters of this disk")
            return

        compareFiles(wvd, disk1_p, wvd2, disk2_p, verbose, args[1:])
        return

registerCmd(cmdCompare())

# ----------------------------------------------------------------------

# display disk files or sectors in hex and ascii
class cmdDump(cmdTemplate):

    def name(self):
        return "dump"
    def shortHelp(self):
        return "show contents of file or sectors in hex and ascii"
    def longHelp(self):
        return \
"""dump {<filename> | <secnumber> | <secnumber> <secnumber> }
    Prints a sector-by-sector dump in hex and ascii of the specified file.
    If the specified filename doesn't exist but looks like a number, the
    that sector will be dumped.  If there is a pair of numbers, then
    all sectors in that range, inclusive, will be dumped."""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(platters) > 1:
            print("This command can only operate on a single platter")
            return
        dumpFile(wvd, platters[0], args)
        return

registerCmd(cmdDump())

# ----------------------------------------------------------------------

class cmdExit(cmdTemplate):

    def name(self):
        return "exit|quit"
    def shortHelp(self):
        return "quit program"
    def longHelp(self):
        return \
"""exit
    Quit the program"""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if args:
            print("This command takes no arguments")
            return
        sys.exit()

registerCmd(cmdExit())

# ----------------------------------------------------------------------

class cmdHelp(cmdTemplate):

    def name(self):
        return "help|?"
    def shortHelp(self):
        return "print list of commands more details of one command"
    def longHelp(self):
        return \
"""help [<command>]
    Without any argument, prints a short summary of commands.
    With a command name, gives details about the command."""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) >= 2:
            print("This command takes takes at most one argument")
            return
        if len(args) == 1:
            cmdUsage(args[0])
        else:
            cmdUsage()
        return

registerCmd(cmdHelp())

# ----------------------------------------------------------------------

class cmdIndex(cmdTemplate):

    def name(self):
        return "index"
    def shortHelp(self):
        return "display or change the catalog index type"
    def longHelp(self):
        return \
"""index [old | new]
    Without any argument, display the current catalog index type, or
    specify "old" or "new" index type to change the catalog file map.
    The 2200 family can only read disk with the old style index, while
    VP and MVP OS 2.5 and higher can read and write both."""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) > 1:
            cmdUsage('index')
            return
        for p in platters:
            ttype = wvd.catalog[p].getIndexType()
            if not args:
                if ttype == 0:
                    print("Platter %d: the catalog index is the old style" % (p+1))
                if ttype == 1:
                    print("Platter %d: the catalog index is the new style" % (p+1))
                if ttype == 2:
                    print("Platter %d: the catalog index is tri-byte (and this util doesn't support it well" % (p+1))
            else:
                if args[0] == "old":
                    newtype = 0
                elif args[0] == "new":
                    newtype = 1
                else:
                    cmdUsage('index')
                    return
                wvd.catalog[p].convertIndex(newtype)
        return

registerCmd(cmdIndex())

# ----------------------------------------------------------------------

# display the specified program file in ascii
class cmdList(cmdTemplate):

    def name(self):
        return "list"
    def shortHelp(self):
        return "show program or data file as text"
    def longHelp(self):
        return \
"""list {<filename> | <secnumber> | <secnumber> <secnumber>}
    Produce listing of the named file or for a range of sectors.
    It can list the contents of data files only if they were created
    conventionally, via DATASAVE commands."""
    def needsCatalog(self):
        return False   # "LIST 100 200" style doesn't require a catalog
    def prettyprinted(self):  # pylint: disable=no-self-use
        return False

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(platters) > 1:
            print("This command can only operate on a single platter")
            return
        if len(args) < 1 or len(args) > 2:
            print("This command expects a single filename as an argument, or two sector addresses")
            cmdUsage('list')
            return
        listing = listFile(wvd, platters[0], args, self.prettyprinted())
        for txt in listing:
            if generate_html:
                # must escape special characters
                txt = re.sub('&', '&amp;', txt)
                txt = re.sub('<', '&lt;', txt)
                txt = re.sub('>', '&gt;', txt)
            print(txt)
        return

registerCmd(cmdList())

# display the specified program file in ascii
# note: inherits from cmdList
class cmdListD(cmdList):

    def name(self):
        return "listd"
    def shortHelp(self):
        return "show pretty-printed program or data file as text"
    def longHelp(self):
        return \
"""listd {<filename> | <secnumber> | <secnumber> <secnumber>}
    Produce listing of the named file or for a range of sectors.
    The output is "decompressed" (prettyprinted), ala the BASIC-2
    LISTD (list decompressed) command."""
    def prettyprinted(self):
        return True

registerCmd(cmdListD())

# ----------------------------------------------------------------------

class cmdMeta(cmdTemplate):

    def name(self):
        return "meta"
    def shortHelp(self):
        return "report virtual disk metadata"
    def longHelp(self):
        return \
"""meta
    Prints .wvd metadata"""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) > 1:
            print("This command takes no arguments")
            return
        reportMetadata(wvd)
        return

registerCmd(cmdMeta())

# ----------------------------------------------------------------------

class cmdProtect(cmdTemplate):

    def name(self):
        return "protect|prot"
    def shortHelp(self):
        return "set program file to be saved in protected mode"
    def longHelp(self):
        return \
"""protect [<wildcard list>]
    Sets "P" protected mode on program files matching wildcard(s)"""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) < 1:
            print("This command expects a list of filenames and/or wildcards")
            return
        for p in platters:
            setProtection(wvd, p, True, args)
        # FIXME: this changes disk state; make it savable
        return

registerCmd(cmdProtect())

# ----------------------------------------------------------------------

class cmdSave(cmdTemplate):

    def name(self):
        return "save"
    def shortHelp(self):
        return "write the in-memory disk image back to disk"
    def longHelp(self):
        return \
"""save [<filename>]
    Commands which change the wvd disk state only change the in-memory
    representation of the disk.  To make the changes permanent, the save
    command is used to write the virtual disk image back out the real disk.
    Without any argument, "save" writes the updated image back to the same
    file that was the source of the disk image.  Optionally, a filename can
    be specified, and the disk image will be written there instead."""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) >= 2:
            print("This command takes takes at most one argument")
            cmdUsage()
            return
        if len(args) == 1:
            wvd.write(args[0])
        else:
            wvd.write(wvd.getFilename())
        return

registerCmd(cmdSave())

# ----------------------------------------------------------------------

class cmdUnprotect(cmdTemplate):

    def name(self):
        return "unprotect|unprot"
    def shortHelp(self):
        return "set program file to be saved in normal mode"
    def longHelp(self):
        return \
"""unprotect [<wildcard list>]
    Clears "P" protected mode on program files matching wildcard(s)"""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if not args:
            print("This command expects a list of filenames and/or wildcards")
            return
        for p in platters:
            setProtection(wvd, p, False, args)
        # FIXME: this changes disk state; make it savable
        return

registerCmd(cmdUnprotect())

# ----------------------------------------------------------------------
# turn on or off virtual disk write protect tab

class cmdWriteProtect(cmdTemplate):

    def name(self):
        return "wp"
    def shortHelp(self):
        return "set or clear the write protect status of the virtual disk"
    def longHelp(self):
        return \
"""wp [on | off]
    "on" clears write protect status for the virtual disk
    "off" sets write protect status
    if no argument is supplied, the current status is returned"""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        if len(args) > 1:
            cmdUsage('wp')
            return
        if not args:
            wp = wvd.getWriteProtect()
            if wp:
                print("write protect is on")
            else:
                print("write protect is off")
        else:
            if args[0] == "on":
                wp = True
            elif args[0] == "off":
                wp = False
            else:
                cmdUsage('wp')
                return
            if wvd.getWriteProtect() == wp:
                return  # nothing changed
            wvd.setWriteProtect(wp)
            # FIXME: this changes disk state; make it savable
        return

registerCmd(cmdWriteProtect())

# ----------------------------------------------------------------------
# specify the default platter to operate on if one isn't given for a command

class cmdPlatter(cmdTemplate):

    def name(self):
        return "platter"
    def shortHelp(self):
        return "supply the default if a platter number isn't specified"
    def longHelp(self):
        return \
"""platter [<integer>]
    For multiplatter disks, most commands need to specify which platter
    to operate on.  The platter can be specified for each command, but
    if one isn't given, the value set by this command will be used.
    If no argument is supplied, the current status is returned.
    The initial default platter value is 1."""

    def doCommand(self, wvd, platters, args):
        # type: (WangVirtualDisk, List[int], List[str]) -> None
        global default_platter  # pylint: disable=global-statement
        if len(args) > 1:
            cmdUsage('platter')
            return
        elif not args:
            print("default platter value is %d" % (default_platter+1))
        elif args[0].isdigit():
            val = int(args[0])
            if (val < 1) or (val > wvd.numPlatters()):
                print("Ignored: disk has %d platters" % wvd.numPlatters())
            else:
                default_platter = val-1
        else:
            cmdUsage('platter')
        return

registerCmd(cmdPlatter())

########################################################################
# given a set of arbitrary sectors, which may or may not contain
# coherent program and data listings, attempt to list its contents
# sensibly.

def listArbitraryBlocks(blocks, listd, abs_sector):
    # type: (List[bytearray], bool, int) -> List[str]

    listing = []
    relsector = -1  # sector number, relative to first block

    for secData in blocks:

        relsector = relsector + 1
        secnum = relsector + abs_sector

        listing.append("============== sector %d ==============" % secnum)

        # break out the SOB flag bits
        data_file      = secData[0] & 0x80
        header_record  = secData[0] & 0x40
        trailer_record = secData[0] & 0x20
        #protected_file = secData[0] & 0x10
        #last_phys_record = secData[0] & 0x02
        #intermediate_phys_record = secData[0] & 0x01

        dummyname = WvdFilename(bytearray(8))
        if not data_file and header_record and \
            not checkProgramHeaderRecord(dummyname, secnum, secData, False):
            listing.extend(listProgramFromBlocks([secData], listd, secnum))
            continue
        if not data_file and not trailer_record and \
           not checkProgramBodyRecord(dummyname, secnum, secData, 0xFD, False):
            listing.extend(listProgramFromBlocks([secData], listd, secnum))
            continue
        if not data_file and trailer_record and \
           not checkProgramBodyRecord(dummyname, secnum, secData, 0xFE, False):
            listing.extend(listProgramFromBlocks([secData], listd, secnum))
            continue
        if data_file and not trailer_record:
            listing.extend(listDataFromOneRecord(secData))
            continue
        if data_file and trailer_record:
            listing.append("Data file trailer record")
            continue

        # don't know what it is -- just dump it
        # FIXME: this is lifted from wvdutil.ph's dumpSector() routine
        #        these should be factored into a common routine
        for line in range(16):
            txt = "%02x:" % (line*16)
            for off in range(16):
                x = 16*line + off
                txt += " %02x" % secData[x]
            txt += "  "
            for off in range(16):
                x = 16*line + off
                if (secData[x] >= ord(' ')) and (secData[x] < 128):
                    txt += chr(secData[x])
                else:
                    txt += "."
            listing.append(txt)

    return listing


############################ main program ############################
# dumpFile() and listFile() have common parameter options:
#    <filename>
#    <wildcard resolving to one file>
#    <sector number>
#    <first sector> <last sector>
# listFile returns a list of strings, or [] if there was an error

# if the argument is a number and there happens to be a file name which
# matches the number, we assume the user intends the file and not the
# sector number.
#
# returns (fileobject, starting_sector_number, ending_sector_number)
# each position may be None if it isn't valid.
def filenameOrSectorRange(wvd, p, args):
    # type: (WangVirtualDisk, int, List[str]) -> Tuple[Optional[CatalogFile], Optional[int], Optional[int]]

    badReturn = (None, None, None)

    (start, end) = (-1, -1)

    if (len(args) == 1) and wvd.catalog[p].hasCatalog():
        # it might be a name or a wildcard
        filename = getOneFilename(wvd, p, args[0])
        if filename != None:
            theFile = wvd.catalog[p].getFile(filename)
            assert theFile != None
            if not theFile.fileExtentIsPlausible():
                print("The file extent doesn't make sense")
                return badReturn
            start = theFile.getStart()  # all allocated sectors
            used  = theFile.getUsed()
            end   = start+used-1        # all meaningful sectors
            return (theFile, start, end)

    if (len(args) == 1) and (args[0].isdigit()):
        start = int(args[0])
        end = start
    elif (len(args) == 2) and (args[0].isdigit()) and (args[1].isdigit()):
        start = int(args[0])
        end   = int(args[1])
    else:
        print("Expecting a filename, a sector number, or pair of sector numbers")
        return badReturn

    if (start < 0) or (end < 0) or \
        (start > wvd.numSectors()) or (end > wvd.numSectors()):
        print("sector start or end is not a valid sector number for this disk")
        print("Valid range: sector 0 to %d" % (wvd.numSectors()-1))
        return badReturn

    if start > end:
        (start, end) = (end, start)  # swap

    return (None, start, end)

############################ getOneFilename ############################
# make sure that the name passed by the user is a real file, or if
# it is a pattern, ensure that it resolves to a single file.

def getOneFilename(wvd, p, name):
    cat = wvd.catalog[p]
    if not cat.hasCatalog():
        print("This disk doesn't appear to have a catalog")
        return None

    outlist = cat.expandWildcards([name])
    if not outlist:
        return None
    if len(outlist) > 1:
        print("This command accepts a single filename, and more than one matched:")
        print([str(x).rstrip() for x in outlist])
        return None
    return outlist[0]

############################ main program ############################

def mainloop():
    # type: () -> None
    # pylint: disable=too-many-branches, too-many-statements

    if debug:
        pdb.set_trace()  # initiate debugging

    # if no arguments, print help message and quit
    if len(sys.argv) < 2:
        usage(True)

    # check for optional "-html" flag
    global generate_html  # pylint: disable=global-statement
    generate_html = '-html' in sys.argv
    if generate_html:
        sys.argv.remove('-html')

    # first argument is always a .wvd filename
    wvd_filename = sys.argv[1]
    if generate_html:
        wvd_filename = unquote_plus(wvd_filename)
    if not os.path.exists(wvd_filename):
        print("File not found:", wvd_filename)
        sys.exit()
    wvd = WangVirtualDisk()
    try:
        wvd.read(sys.argv[1])
    except:  # pylint: disable=bare-except
        print("Couldn't open", wvd_filename, "as a Wang virtual disk image")
        sys.exit()

    # report if some platters don't have a catalog
    no_catalog = []
    for platter in range(wvd.numPlatters()):
        if not wvd.catalog[platter].hasCatalog():
            no_catalog.append(str(platter+1))
    if len(no_catalog) == wvd.numPlatters():
        print("This disk doesn't have any catalog structure.")
        print("Some commands will not be useful for this disk.")
    elif no_catalog:
        print("These platters have no catalog:", ", ".join(no_catalog))
        print("Some commands will not be useful for those platters.")

    if len(sys.argv) > 2:
        # just perform command on command line and quit
        # strings containing spaces were indistinguishible from two words,
        # eg: list "TBO DIAG" was showing up to command as "list TBO DIAG"
        # commandStr = " ".join(sys.argv[2:])
        # just quote everything
        commandStr = ""
        for next_arg in sys.argv[2:]:
            commandStr = commandStr + '"' + next_arg + '"' + ' '
        command(wvd, commandStr)
        return

    print(basename + ', version 1.5, 2018/01/01')
    print('Type "help" to see all commands')

    # accept command lines from user interactively
    while 1:
        # prompt and get command
        if wvd.numPlatters() > 1:
            sys.stdout.write("wvd/%d>" % (default_platter+1))
        else:
            sys.stdout.write("wvd>")
        sys.stdout.flush()

        try:
            cmdStr = sys.stdin.readline()
        except KeyboardInterrupt:
            sys.exit(0)

        # perform command
        try:
            command(wvd, cmdStr)
        except SystemExit:   # cmdExit() raises this
            if wvd.isDirty():
                print("Warning: the disk image has been modified without being saved.")
                try:
                    inp = ''
                    while inp not in ('y', 'yes', 'n', 'no'):
                        inp = input("Do you really want to exit (y/n)? ").strip().lower()
                    if inp in ('y','yes'):
                        break
                    print("Use the 'save' command to save changes")
                    continue  # back to while 1
                except KeyboardInterrupt:
                    print()
                    print("<<<command interrupted>>>")
                    continue  # back to while 1
            break
        except KeyboardInterrupt:
            print("<<<command interrupted>>>")
#       except:
#           print("<<<internal error>>> sorry about that")


if __name__ == "__main__":
    basename = os.path.basename(sys.argv[0])
    mainloop()
    sys.exit()
