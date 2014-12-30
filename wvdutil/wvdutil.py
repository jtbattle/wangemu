# Program: wvdutil.py
# Purpose: read a .wvd (wang virtual disk file) and report what's in it
# Version: 1.2, 2008/08/18
# Author:  Jim Battle

########################################################################
# there are any number of operations that could be provided by this
# program, but things which can easily be done on the emulator (e.g.,
# "COPY FR" to squish out scratched files) are not duplicated here.

# FIXME:
#    before any command that performs state change,
#        warn if the disk is write protected (and suggest 'wp' command)
#    before quitting or changing disk, complain if the disk state is dirty
#    do some testing with disks that don't have a catalog at all and at
#       least make sure there are no asserts
#    newsletter_no_5.pdf, page 5, describes via a program "TC" file format.
#       what is this?  should I add disassembly of such files?
#
# Syntax for possible new commands
#    wvd> disk <filename>         resets the current disk being operated on
#    wvd> extract <filename>      (as binary file)
#    wvd> extract sector(s) a(,b) (as binary file)

#import pychecker.checker        # static lint tool
import sys
import os.path
import re
#import pdb                      # for debugging

from wvdlib import *
from wvfilelist import *

# when working with multiplatter disks, commands may specify which platter
# to operate on.  if a platter isn't specified, the default value comes
# from this variable.  it is set by the "platter <n>" command.
# the value the user sees has an origin of 1, but the internal code uses
# an origin of 0.
default_platter = 0

############################ reportMetadata ############################
# make sure that the name passed by the user is a real file, or if
# it is a pattern, ensure that it resolves to a single file.

def getOneFilename(wvd, p, name):
    cat = wvd.catalog[p]
    if not cat.hasCatalog():
        print "This disk doesn't appear to have a catalog"
        return

    outlist = cat.expandWildcards( [name] )
    if len(outlist) == 0:
        return None
    elif len(outlist) > 1:
        print "This command accepts a single filename, and more than one matched:"
        print [x.rstrip() for x in outlist]
        return None
    else:
        return outlist[0]

############################ reportMetadata ############################
# report information about the disk

def reportMetadata(wvd):
    wpList = ( 'no', 'yes' )
    mediaList = ( '5.25" floppy', '8" floppy', '2260 hard disk', '2280 hard disk' )

    #print "write format:    ", wvd.getWriteFormat()
    #print "read format:     ", wvd.getReadFormat()
    print "filename:       ", wvd.getFilename()
    print "write protect:  ", wpList[wvd.getWriteProtect()]
    print "platters:       ", wvd.numPlatters()
    print "sectors/platter:", wvd.numSectors()
    print "media type:     ", mediaList[wvd.mediaType()]
    print "label:          "
    for line in wvd.label().splitlines():
        print "    ",line

################################# catalog #################################
# report list of files on the given platter of the disk, with optional
# wildcard list.  wildcards are dos-like:
#    "*" means any number of characters
#    "?" means any one character

def catalog(wvd, p, wcList = ['*']):

    cat = wvd.catalog[p]
    if not cat.hasCatalog():
        print "This disk doesn't appear to have a catalog"
        return

    filelist = cat.expandWildcards(wcList)

    # print header and disk information
    index_mark = " '&"[cat.getIndexType()]
    print "INDEX SECTORS = %05d%c" % (cat.numIndexSectors(), index_mark)
    print "END CAT. AREA = %05d" % cat.endCatalogArea()
    print "CURRENT END   = %05d" % cat.currentEnd()
    print
    print "NAME     TYPE  START   END   USED   FREE"

    for name in filelist:
        curFile = cat.getFile(name)
        if curFile != None:
            # check file for extra information
            status = []
            saveMode = curFile.programSaveMode()
            if saveMode != None and saveMode != "normal":
                status.append(saveMode)

            bad = checkFile(wvd, p, name, report=False)
            if bad: status.append("non-standard or bad format")

            extra = ""
            if len(status): extra = "   [%s]" % (', '.join(status))

            # report it
            fname   = curFile.getFilename()
            fstatus = curFile.getStatus()
            start   = curFile.getStart()
            end     = curFile.getEnd()
            used    = curFile.getUsed()
            free    = curFile.getFree()
            print "%8s  %s   %05d  %05d  %05d  %05d%s" % \
                (fname, fstatus, start, end, used, free, extra)

########################### convert file to text ###########################
# given a program file name, convert that to a program listing, returned
# as a list of strings.

def listProgram(wvd, p, filename):
    theFile = wvd.catalog[p].getFile(filename)
    assert theFile != None
    return listProgramFromBlocks(theFile.getSectors())

# list structured (that is, produced by DATASAVE) data file
def listData(wvd, p, filename):
    theFile = wvd.catalog[p].getFile(filename)
    assert theFile != None
    return listDataFromBlocks(theFile.getSectors())

############################ (un)protect files ############################
# given a program filename (or wildcard list), change the protection
# flags to make the program protected or unprotected

def setProtection(wvd, p, protect, wcList = [ '*' ]):

    # FIXME: make sure disk isn't write protected

    for name in wvd.catalog[p].expandWildcards(wcList):
        curFile = wvd.catalog[p].getFile(name)
        assert curFile != None
        if curFile.getType() != 'P':
            print '"%s" is not a program file' % name.rstrip()
        else:
            saveMode = curFile.programSaveMode()
            if saveMode == 'scrambled':
                print '"%s" is a scrambled (SAVE !) file' % name.rstrip()
                print "This program doesn't know how to crack such files"
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

def compareFiles(wvd, disk1_p, wvd2, disk2_p, args):
    if len(args) > 0:
        wildcardlist = args
    else:
        wildcardlist = ['*']

    wvdname  = '"' + wvd.getFilename() + '"'
    wvd2name = '"' + wvd2.getFilename() + '"'

    same_disk = (wvdname == wvd2name)

    # if we are operating on two disks, first check if the disks themselves
    # are an exact match, as that answers all other questions
    if (not same_disk):
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
            for p in xrange(wvd.numPlatters()):
                for n in xrange(wvd.numSectors()):
                    sec1 = wvd.getRawSector(p, n)
                    sec2 = wvd2.getRawSector(p, n)
                    if sec1 != sec2:
                        sameSectors = 0
                        break

        # 1) disk images, including metadata, match exactly (quit if so)
        if sameMetadata and sameSectors:
            print "The two disks appear to be identical"
            return

        # 2) disk images, excluding metadata, match exactly (quit if so)
        if sameSectors:
            print "The two disks have different metadata, but identical sectors"
            return

    # python 2.2 doesn't have sets module nor set built-in
    # so this will be a little verbose
    fileset1x = wvd.catalog[disk1_p].expandWildcards(wildcardlist)
    fileset1 = [ f.rstrip() for f in fileset1x ]
    fileset2x = wvd2.catalog[disk2_p].expandWildcards(wildcardlist)
    fileset2 = [ f.rstrip() for f in fileset2x ]

    # 3) report if a file appears on one disk or other only
    wvd1only = []
    for f in (fileset1):
        if f not in fileset2:
            wvd1only.append(f)

    wvd2only = []
    for f in (fileset2):
        if f not in fileset1:
            wvd2only.append(f)

    if (wvd1only == fileset1) and (wvd2only == fileset2):
        print "These disks have no files in common"
        return

    if len(wvd1only) > 0:
        wvd1only.sort()
        if same_disk:
            print "Files on platter %d and not platter %d:" % (disk1_p+1, disk2_p+1)
        else:
            print "Files in %s and not in %s:" % (wvdname, wvd2name)
        print wvd1only

    if len(wvd2only) > 0:
        wvd2only.sort()
        if same_disk:
            print "Files on platter %d and not platter %d:" % (disk2_p+1, disk1_p+1)
        else:
            print "Files in %s and not in %s:" % (wvd2name, wvdname)
        print wvd2only

    # 4) report if a files don't have the same type or status
    # 5) report if a files compare exactly (for now)
    # 6) show diff report ? (future enhancement)

    wvdMatching = []
    wvdMismatching = []
    for f in (fileset1):

        file1 = wvd.catalog[disk1_p].getFile(f)
        file1Data = file1.getSectors()
        assert file1 != None

        if f in fileset2:

            file2 = wvd2.catalog[disk2_p].getFile(f)
            assert file2 != None
            file2Data = file2.getSectors()

            if file1Data == file2Data:
                wvdMatching.append(f)
            else:
                wvdMismatching.append(f)

    if len(wvdMismatching) > 0:
        wvdMismatching.sort()
        print "Mismatching files:"
        print wvdMismatching
    if len(wvdMatching) > 0:
        wvdMatching.sort()
        print "Matching files:"
        print wvdMatching

###################### dump file or range of sectors ######################
# dump all sectors of a file or a specified range of sectors in hex and ascii

def dumpFile(wvd, p, args):
    if (len(args) == 1) and wvd.catalog[p].hasCatalog() and \
            (padName(args[0]) in wvd.catalog[p].allFilenames()):
        filename = args[0]
        theFile = wvd.catalog[p].getFile(filename)
        if theFile == None:
            print "File not found"
            return
        start = theFile.getStart()
        end   = theFile.getEnd()
    elif (len(args) == 1) and (args[0].isdigit()):
        start = int(args[0], 0)
        end = start
    elif (len(args) == 2) and (args[0].isdigit()) and (args[1].isdigit()):
        start = int(args[0], 0)
        end   = int(args[1], 0)
    else:
        print "Expecting filename, sector number, or pair of sector numbers"
        return
    if (start < 0) or (end < 0) or \
        (start > wvd.numSectors()) or (end > wvd.numSectors()):
        print "sector start or end is not a valid sector number for this disk"
        print "Valid range: sector 0 to %d" % (wvd.numSectors()-1)
        return
    if start > end:
        (start, end) = (end, start)  # swap
    for s in xrange(start, end+1):
        print "### Disk Sector", s, "###"
        dumpSector(wvd.getRawSector(p, s))
    return

def dumpSector(data):
    for line in xrange(16):
        print "%02x:" % (line*16),
        for off in xrange(16):
            x = 16*line + off
            print "%02x" % ord(data[x]),
        asc = ''
        for off in xrange(16):
            x = 16*line + off
            if (data[x] >= ' ') and (data[x] < chr(128)):
                asc += data[x]
            else:
                asc += "."
        print " ",asc

######################## list program or data file ########################
# given a file name, return a list containing one text line per item

def listFile(wvd, p, filename):

    fname = getOneFilename(wvd, p, filename)
    if fname == None:
        return []

    # make sure the file is real
    theFile = wvd.catalog[p].getFile(fname)
    if theFile == None:
        print 'No file named "' + fname + '" on disk'
        return []

    # make sure it is a program
    fileType = theFile.getType()
    if fileType == "D":
        return listData(wvd, p, fname)

    if fileType != "P":
        print "Not a program file"
        return []

    # make sure program isn't scrambled
    mode = theFile.programSaveMode()
    if mode == 'scrambled':
        print "Program is scrambled (SAVE !) and can't be listed"
        return []

    # list it
    return listProgram(wvd, p, fname)

############################# command parser #############################

# cmdSet is a hash keyed on the command name, storing a command handler instance
cmdSet = {}
cmdSetAliases = {}
def registerCmd( cmdInst ):
    global cmdSet
    name = cmdInst.name()
    # names can contain aliases as a "|"-separated list
    names = name.split("|")
    primary = 1
    for n in names:
        if cmdSet.has_key(n) or cmdSetAliases.has_key(n):
            print "Command",n,"defined twice!"
            sys.exit()
        if primary:
            cmdSet[n] = cmdInst
        else:
            cmdSetAliases[n] = cmdInst
        primary = 0

def cmdUsage(cmdName=None):
    if cmdName == None:
        print
        print 'Usage:'
        print '   ', basename, '<filename> [<command>[/<platter>] [<args>]] [<redirection>]'
        print
        print 'With no command, enter into command line mode, where each subsequent line'
        print 'contains one of the commands, as described below.  The output can be optionally'
        print 'redirected to the named logfile, either overwriting, ">" or appending, ">>".'
        print 'The output can also be piped through a pager by ending with "| more".'
        print 'Arguments containing spaces can be surrounded by double quote characters.'
        print
        print 'For multiplatter disks, the platter can be specified by following the command'
        print 'name with a slash and the decimal platter number.  For commands where it makes'
        print 'sense, "/all" can be specified to operate on all platters.'
        print
        print '<redirection> is optional, and takes one of three forms:'
        print '   ... >   <logfile>  -- write command output to a logfile'
        print '   ... >>  <logfile>  -- append command output to a logfile'
        print '   ... | more         -- send command output through a pager'
        print
        print 'Type "help <command>" to get more detailed help on a particular command.'
        print
        keys = cmdSet.keys()
        keys.sort()
        for k in keys:
            print '   %-9s - %s' % ( k, cmdSet[k].shortHelp() )
        print
        return

    if cmdSet.has_key(cmdName):
        print cmdSet[cmdName].longHelp()
        names = cmdSet[cmdName].name().split("|")
        if len(names) > 1:
            print "Command aliases:", ", ".join(names)
        return

    if cmdSetAliases.has_key(cmdName):
        print cmdSetAliases[cmdName].longHelp()
        names = cmdSetAliases[cmdName].name().split("|")
        if len(names) > 1:
            print "Command aliases:", ", ".join(names)
        return

    else:
        print 'No command "' + cmdName + '"'
        return

# report all commands and optionally quit
def usage(exit=1):
    cmdUsage()
    if exit: sys.exit()


# check if one or more of the specified platters has a catalog
# pass in a wvd and a list of platters that we want to inspect
# returns None if no platters exist, and a list containing a subset
# of the original platters which do have catalogs.
def pickUsefulCatalog(wvd, platters):
    # if the command requires a platter to have a catalog, check that
    # our list of target platters obeys
    if len(platters) == 1:
        p = platters[0]
        if not wvd.catalog[p].hasCatalog():
            if wvd.numPlatters() == 1:
                print "This disk doesn't appear to have a catalog"
            else:
                print "Platter %d doesn't appear to have a catalog" % (p+1)
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
        if len(new_platters) == 0:
            print "At least one platter must have a catalog for this command"
            return None
        if len(bad_platters) > 0:
            print "These platters have no catalog and will be skipped:", \
                ", ".join(bad_platters)
        return new_platters


# this wrapper makes it easier to clean up redirection after all commands
def cmdDispatch(cmd, args):
    # check if a platter has been specified, eg: "cat/2" or "cat/all"
    mo = re.match(r'([a-z]+)/(.+)$', cmd)
    platters = [default_platter]
    if mo is not None:
        cmd = mo.group(1)
        if mo.group(2).lower() == "all":
            # user is requesting operation on all platters
            platters = range(wvd.numPlatters())
        elif mo.group(2).isdigit():
            # user is requesting operation on a specific platter
            p = int(mo.group(2))
            if (p < 1) or (p > wvd.numPlatters()):
                print "Platter %d specified; disk has %d platters" % (p, wvd.numPlatters())
                return
            platters = [ p-1 ]  # user sees 1 as first platter, code uses 0
        else:
            print "Bad platter specification '%s'" % mo.group(2)
            return

    # try and find a command handler
    if cmdSet.has_key(cmd):
        command = cmdSet[cmd]
    elif cmdSetAliases.has_key(cmd):
        command = cmdSetAliases[cmd]
    else:
        print 'Unknown command:', cmd
        print 'Type "help" for the list of commands'
        return

    if command.needsCatalog():
        platters = pickUsefulCatalog(wvd, platters)
        if not platters:
            return 

    # perform the command
    command.doCommand(wvd, platters, args)

# ----------------------------------------------------------------------

# given a disk image and a command, perform the command (or bitch)
def command(wvd, cmdString):

    # because filenames may contain a space, we need to handle quoting
    # only doublequote is supported since wang filenames may contain a single quote
    quotedWordRe = r'"[^"]*"'        # double quotes surrounding word (possibly 0 length)
    wordRe = r'[^ "]+'               # string of non-space letters
    allRe = quotedWordRe + '|' + wordRe
    words = re.findall( allRe, cmdString.strip() )

    # the redirection operators, ">", ">>", and "|", might be attached to
    # neighboring words.  split them off of not quoted words.
    words, old_words = [], words
    redir_op = re.compile( r'(>>|>|[|])' )
    for w in old_words:
        if w[0]=='"': # quoted
            words.append(w)
        else:
            # pad spaces around redirection operators and then split
            words.extend( redir_op.sub(r' \1 ', w).split() )

    # quoted strings still have their quotes around them
    words = [ re.sub(r'^"(.*)"$', r'\1', w) for w in words]

    if len(words) == 0:
        return
    cmd = words[0].lower()  # the command words is case insensitive

    # check for redirection
    origStdout = sys.stdout
    pager_file = None;
    if ('|' in words):
        # "| more" must be the final two words
        idx = words.index("|")
        if (idx != len(words)-2) or (words[idx+1] != "more"):
            print "Bad pipe command"
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
            print 'Error: Redirection ">" or ">>" expects to be followed by a single filename'
            return
        redirFile = words[idx+1]
        words = words[ :idx]     # chop off "> filename" part
        try:
            sys.stdout = file(redirFile, mode, 1)
        except:
            sys.stdout = origStdout
            print "Couldn't open" + '"' + redirFile + '" for redirection'
            return

    try:
        cmdDispatch(cmd, words[1:])
    except KeyboardInterrupt:
        sys.stdout = origStdout
        if pager_file:
            os.unlink(pager_file)
        print "<<command interrupted>>"
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
        return ["Overload doCommand() in command object subclass"]

# ----------------------------------------------------------------------

class cmdCatalog(cmdTemplate):

    def name(self):
        return "cat|catalog|dir|ls"
    def shortHelp(self):
        return "display all or selected files on the disk"
    def longHelp(self):
        return \
"""cat [<wildcard list>]
    List files on disk, or those matching any supplied wildcard.
    Wildcards use "?" to match one char, "*" to match no or many"""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        for p in platters:
            if len(platters) > 1:
                if p != platters[0]: print
                print '-'*20, "Platter #%d" % (p+1), '-'*20
            if len(args) == 0:
                catalog( wvd, p )         # list all
            else:
                catalog( wvd, p, args )   # wildcard version
        return

registerCmd( cmdCatalog() )

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
        if len(args) > 0:
            cmdUsage('check')
            return
        fail = False
        for p in platters:
            fail = checkPlatter(wvd, p, report=True)
            if fail: break
        if fail:
            print "Failed sanity check"
        else:
            print "Looks OK"
        return

registerCmd( cmdCheck() )

# ----------------------------------------------------------------------

class cmdCompare(cmdTemplate):

    def name(self):
        return "compare"
    def shortHelp(self):
        return "Compare two disks or selected files on two disks"
    def longHelp(self):
        return \
"""compare[/<platter>] /<platter> [<wildcard list>]
compare[/<platter>] disk2.wvd[/<platter>] [<wildcard list>]

    In the first form, the files of two different platters of the same disk
    are compared.  For this to make any sense, the disk must be multiplatter.
    Files that appear on one platter and not the other are reported, files
    with the same name but mismatch are reported, and files with the same
    name and match exactly are also reported.  If a <wildcard list> is given,
    only files matching the list will be considered for comparison.

    In the second form, the files from two platters on two different disks
    are compared, again with an optional file wildcard list.  The second
    /<platter> specifier is optional, and defaults to platter 1.  For either
    disk, the /<platter> specifier is not meaningful for single platter disks."""

    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        # FIXME: this needs an overhaul
        if (len(args) < 1) or (len(platters) > 1):
            cmdUsage('compare')
            return

        disk1_p = platters[0]  # disk1 partition
        if not wvd.catalog[disk1_p].hasCatalog():
            print "Disk1 doesn't appear to have a catalog"
            return

        # foo.wvd/3:  group1=foo.wvd, group2=/3, group3=3
        # /3:         group1=,        group2=/3, group3=3
        # foo.wvd:    group1=foo.wvd, group2=None, group3=None
        mo = re.match('([^/]*)(/(\d+))?$', args[0])
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
            except:
                print "Couldn't open " + '"' + disk2_name + '" as a Wang virtual disk image'
                return
        else:
            # comparing two platters of the same multiplatter disk
            wvd2 = wvd

        if disk2_p >= wvd2.numPlatters():
            print "The second platter specifier is greater than %d" % wvd2.numPlatters()
            return
        if (not disk2_name) and (disk1_p == disk2_p):
            print "Please specify two different platters of this disk"
            return

        compareFiles(wvd, disk1_p, wvd2, disk2_p, args[1:])
        return

registerCmd( cmdCompare() )

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
        if len(platters) > 1:
            print "This command can only operate on a single platter"
            return
        dumpFile(wvd, platters[0], args)
        return

registerCmd( cmdDump() )

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
        if len(args) > 0:
            print "This command takes no arguments"
            return
        sys.exit()

registerCmd( cmdExit() )

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
        if len(args) >= 2:
            print "This command takes takes at most one argument"
            return
        if len(args) == 1:
            cmdUsage(args[0])
        else:
            cmdUsage()
        return

registerCmd( cmdHelp() )

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
        if len(args) > 1:
            cmdUsage( 'index' )
            return
        for p in platters:
            ttype = wvd.catalog[p].getIndexType()
            if len(args) == 0:
                if ttype == 0: print "Platter %d: the catalog index is the old style" % (p+1)
                if ttype == 1: print "Platter %d: the catalog index is the new style" % (p+1)
                if ttype == 2: print "Platter %d: the catalog index is tri-byte (and this util doesn't support it well" % (p+1)
            else:
                if args[0] == "old":
                    newtype = 0
                elif args[0] == "new":
                    newtype = 1
                else:
                    cmdUsage( 'index' )
                    return
                wvd.catalog[p].convertIndex(newtype)
        return

registerCmd( cmdIndex() )

# ----------------------------------------------------------------------

# display the specified program file in ascii
class cmdList(cmdTemplate):

    def name(self):
        return "list"
    def shortHelp(self):
        return "show program or data file as text"
    def longHelp(self):
        return \
"""list <filename>
    Produce listing of the named file; it works only for programs
    and for data files written via DATASAVE commands."""
    def needsCatalog(self):
        return True

    def doCommand(self, wvd, platters, args):
        if len(args) != 1:
            print "This command expects a single filename as an argument"
            return
        if len(platters) > 1:
            print "This command can only operate on a single platter"
            return
        listing = listFile(wvd, platters[0], args[0])
        for txt in listing:
            print txt
        return

registerCmd( cmdList() )

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
        if len(args) > 1:
            print "This command takes no arguments"
            return
        reportMetadata(wvd)
        return

registerCmd( cmdMeta() )

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
        if len(args) < 1:
            print "This command expects a list of filenames and/or wildcards"
            return
        for p in platters:
            setProtection(wvd, p, 1, args)
        # FIXME: this changes disk state; make it savable
        return

registerCmd( cmdProtect() )

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
        if len(args) >= 2:
            print "This command takes takes at most one argument"
            cmdUsage()
            return
        if len(args) == 1:
            wvd.write(args[0])
        else:
            wvd.write(wvd.getFilename())
        return

registerCmd( cmdSave() )

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
        if len(args) < 1:
            print "This command expects a list of filenames and/or wildcards"
            return
        for p in platters:
            setProtection(wvd, p, 0, args)
        # FIXME: this changes disk state; make it savable
        return

registerCmd( cmdUnprotect() )

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
        if len(args) > 1:
            cmdUsage( 'wp' )
            return
        if len(args) == 0:
            wp = wvd.getWriteProtect()
            if     wp: print "write protect is on"
            if not wp: print "write protect is off"
        else:
            if args[0] == "on":
                wp = 1
            elif args[0] == "off":
                wp = 0
            else:
                cmdUsage( 'wp' )
                return
            if wvd.getWriteProtect() == wp:
                return  # nothing changed
            wvd.setWriteProtect(wp)
            # FIXME: this changes disk state; make it savable
        return

registerCmd( cmdWriteProtect() )

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
        global default_platter
        if len(args) > 1:
            cmdUsage( 'platter' )
            return
        elif len(args) == 0:
            print "default platter value is %d" % (default_platter+1)
        elif args[0].isdigit():
            val = int(args[0])
            if (val < 1) or (val > wvd.numPlatters()):
                print "Ignored: disk has %d platters" % wvd.numPlatters()
            else:
                default_platter = val-1
        else:
            cmdUsage( 'platter' )
        return

registerCmd( cmdPlatter() )

############################ main program ############################

if __name__ == "__main__":
    #pdb.set_trace()  # initiate debugging

    basename = os.path.basename(sys.argv[0])

    # if no arguments, print help message and quit
    if len(sys.argv) < 2: usage(1)

    # first argument is always a .wvd filename
    filename = sys.argv[1]
    wvd = WangVirtualDisk()
    try:
        wvd.read(filename)
    except:
        print "Couldn't open",filename,"as a Wang virtual disk image"
        sys.exit()

    # report if some platters don't have a catalog
    no_catalog = []
    for p in xrange(wvd.numPlatters()):
        if not wvd.catalog[p].hasCatalog():
            no_catalog.append(str(p+1))
    if len(no_catalog) == wvd.numPlatters():
        print "This disk doesn't have any catalog structure."
        print "Some commands will not be useful for this disk."
    elif len(no_catalog) > 0:
        print "These platters have no catalog:", ", ".join(no_catalog)
        print "Some commands will not be useful for those platters."

    if len(sys.argv) > 2:
        # just perform command on command line and quit
        cmdStr = " ".join(sys.argv[2:])
        command(wvd, cmdStr)
        sys.exit()

    print basename + ', version 1.2, 2008/10/18'
    print 'Type "help" to see all commands'

    # accept command lines from user interactively
    while 1:
        # prompt and get command
        if (wvd.numPlatters() > 1):
            sys.stdout.write("wvd/%d>" % (default_platter+1))
        else:
            sys.stdout.write("wvd>")

        try:
            cmdStr = sys.stdin.readline()
        except KeyboardInterrupt:
            sys.exit(0)

        # perform command
        try:
            command(wvd, cmdStr)
        except KeyboardInterrupt:
            print "<<<command interrupted>>>"

    sys.exit()
