#!/usr/bin/env python3

# Program: wvdutil.py
# Purpose: read a .wvd (wang virtual disk file) and report what's in it
# Author:  Jim Battle
#
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
# Version: 1.6, 2018/09/15, JTB
#     added ability to list EDIT text files
#     fixed a number of crash scenarios, including using "| more" with py3
#     significant code restructuring
#     pylint cleanups
# Version: 1.7, 2020/01/04, JTB
#     enhance 'edit' file handler to recognize a second format for such files
# Version: 1.8, 2020/01/20, JTB
#     fix a bug that caused a crash when run under python 2.
#     handle scrambled files where the header block doesn't indicate protected.
#     when a byte needs to be escaped in a listing, wvdutil used to emit it as
#         \x<hex><hex>
#     however, the emulator script language uses this format to read escapes:
#         \<hex><hex>
#     wvdutil has been changed to do the latter.
# Version: 1.9, 2020/02/02, JTB
#     added LIST tokens for DATE (0xFA) and TIME (0xFB) (used in later BASIC-2)
# Version: 1.10, 2020/08/15, JTB
#     fix a bounds check error which caused a crash if the user typed, e.g.,
#     "dump 1280" on a 1280 sector disk, as they are numbered 0 to 1279.
#     added the "label" command to inspect or set the label
#     added support for new WVD disk types: FD5_DD (DS/DD), FD5_HD (DS/HD)
# Version: 1.11, 2020/09/07, JTB
#     fixed the 'compare' command -- it was not properly diffing the contents
#     of two files with the same name
# Version: 1.12, 2020/12/27, JTB
#     fixed a bug in 'compare' if two disks had the same file, but one of
#     them produced an empty file (eg, the number of used sectors in the
#     file control record was wrong).
#     'compare' can now take a -c/-context argument to produce a context diff.
#     filename wildcard should treat '.' as a literal period character.
# Version: 1.13, 2021/06/19, JTB
#     get rid of bilingualism (aka python2 support);
#     convert to inline type hints instead of type hint pragma comments

########################################################################
# there are any number of operations that could be provided by this
# program, but things which can easily be done on the emulator (e.g.,
# "COPY FR" to squish out scratched files) are not duplicated here.

# FIXME:
#    see the scattered FIXME comments throughout
#    overall the program structure is poor because it grew out of ad-hoc
#       improvements and was not ever really designed
#    very little testing has been done on disks with the newer indexing scheme
#    before any command that performs state change,
#       warn if the disk is write protected (and suggest 'wp' command)
#    MVP OS 2.6 and later have files with names like "@PM010V1" which are
#       printer control files.  they all start with 0x6911.
#       add logic to allow check, scan, and perhaps list them.
#       Basic2Release_2.6_SoftwareBullitin.715-0097A.5-85.pdf
#       has some description of the generalized printer driver.
#    MVP OS 3.0 and later support saving files in "wrap mode".
#       this is completely unsupported and there is no guard to detect it.
#    newsletter_no_5.pdf, page 5, describes via a program "TC" file format.
#       what is this?  should I add disassembly of such files?
#    make registerCmd() a decorator instead of an explicit call
#       however, python2 doesn't allow class decorators.
#
# Syntax for possible new commands:
#    wvd> check <filename>        currently the whole image is checked
#    wvd> disk <filename>         resets the current disk being operated on
#    wvd> extract <filename>      (as binary file)
#    wvd> extract sector(s) a(,b) (as binary file)
#
# Reminders (so I don't need to figure these out again later):
#    checking type annotations:
#        py -m mypy wvdutil.py  (or just mypy wvdutil.py)
#    pylint checker:
#        py [-2] -m pylint -f msvs wvdutil.py wvdlib.py ...etc...

from typing import List, Dict, Callable, Any, Union, Optional, Tuple  # pylint: disable=unused-import

import sys
import os.path
import re
import tempfile
import difflib

from urllib.parse import quote_plus, unquote_plus  # type: ignore

from wvdlib import (WangVirtualDisk,   # pylint: disable=unused-import
                    WvdFilename,
                    CatalogFile,
                    unscramble_one_sector, sanitize_filename)

from wvdHandler_basic import WvdHandler_basic
from wvdHandler_data  import WvdHandler_data
from wvdHandler_edit  import WvdHandler_edit

# pluggable handlers for interpreting different file formats
# note: put the subtypes of data files in the list before the generic
# data file handler to ensure they get priority.
handlers = [ WvdHandler_basic(),
             WvdHandler_edit(),
             WvdHandler_data()
           ]

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

def reportMetadata(wvd: WangVirtualDisk) -> None:
    wpList = ('no', 'yes')
    mediaList = ('5.25" floppy', '8" floppy',
                 '2260 hard disk', '2280 hard disk',
                 '5.25" DS/DD floppy', '5.25" DS/HD floppy',
                 'invalid')

    mediatype = wvd.mediaType()
    if mediatype >= len(mediaList)-1:
        mediatype = len(mediaList)-1  # map to 'invalid'

    #print("write format:    ", wvd.getWriteFormat())
    #print("read format:     ", wvd.getReadFormat())
    print("filename:       ", wvd.getFilename())
    print("write protect:  ", wpList[wvd.getWriteProtect()])
    print("platters:       ", wvd.numPlatters())
    print("sectors/platter:", wvd.numSectors())
    print("media type:     ", mediaList[mediatype])
    print("label:          ")
    for line in wvd.label().splitlines():
        print("    ", line.rstrip())

################################# catalog #################################
# report list of files on the given platter of the disk, with optional
# wildcard list.  wildcards are dos-like:
#    "*" means any number of characters
#    "?" means any one character

def catalog(wvd: WangVirtualDisk, p: int,
            flags: Dict[str,bool],
            wcList: List[str]) -> None:
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
    print("INDEX SECTORS = %05d%s" % (cat.numIndexSectors(), index_mark))
    print("END CAT. AREA = %05d" % cat.endCatalogArea())
    print("CURRENT END   = %05d" % cat.currentEnd())
    print()
    print("NAME     TYPE  START   END   USED   FREE")

    for name in filelist:
        curFile = cat.getFile(name)
        if curFile is not None:
            # check file for extra information
            status = []
            fileType = curFile.getType()
            if fileType == 'P ':
                saveMode = curFile.programSaveMode()
                if saveMode is not None and saveMode != 'normal':
                    status.append(saveMode)

            bad, fmt = checkFile(wvd, p, name, report=False)
            if bad:
                status.append("non-standard or bad format")
            elif fmt:
                status.append(fmt)

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
                print("<a href=\"%s\">%-8s</a>  %s  %05d  %05d  %05d  %05d%s" % \
                    (linkname, str_fname, fstatus, start, end, used, free, extra))
            else:
                print("%-8s  %s  %05d  %05d  %05d  %05d%s" % \
                    (str_fname, fstatus, start, end, used, free, extra))

############################ (un)protect files ############################
# given a program filename (or wildcard list), change the protection
# flags to make the program protected or unprotected

def setProtection(wvd: WangVirtualDisk, p: int,
                  protect: bool,
                  wcList:List[str]=None) -> None:
    wcList = wcList or ['*']

    if wvd.getWriteProtect():
        print("Not done: this disk image is write protected")
        print("Use the command 'wp off' to turn off write protection")
        return

    for name in wvd.catalog[p].expandWildcards(wcList):
        curFile = wvd.catalog[p].getFile(name)
        fname = name.asStr()
        if curFile is None:
            print('Unable to read file "%s"' % fname)
            continue
        if curFile.getType() != 'P ':
            print('"%s" is not a program file' % fname)
            continue
        saveMode = curFile.programSaveMode()
        if saveMode == 'unknown':
            print('"%s" is not a program file' % fname)
            continue
        if protect and (saveMode in ('protected','scrambled')):
            continue
        if not protect and (saveMode == 'normal'):
            continue

        if saveMode == 'scrambled':
            print('"%s" is a scrambled (SAVE !) file' % fname)
            inp = input("Do you really want to unprotect it (y/n)? ").strip().lower()
            if inp not in ('y','yes'):
                print('skipping "%s"' % fname)
                continue
            fileBlocks = curFile.getSectors()
            newBlocks = []
            for blk in fileBlocks:
                # make doubly sure that each sector is a scrambled body block
                if (blk[0] & 0xD0) == 0x10 and \
                   (blk[1] & 0x80) == 0x00:
                    newBlocks.append(unscramble_one_sector(blk))
                else:
                    newBlocks.append(blk)
            # write back the modified file
            curFile.setSectors(newBlocks)

        newstate = "protected" if protect else "unprotected"
        print("Changing '%s' to %s" % (fname, newstate))
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

def compareFiles(wvd: WangVirtualDisk, disk1_p: int,
                 wvd2: WangVirtualDisk, disk2_p: int,
                 verbose: str, args: List[str]) -> None:
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
    fileset1 = {f.asStr() for f in fileset1x}
    fileset2x = wvd2.catalog[disk2_p].expandWildcards(wildcardlist)
    fileset2 = {f.asStr() for f in fileset2x}

    # 3) report if a file appears on one disk or other only
    if fileset1.isdisjoint(fileset2):
        print("These disks have no files in common")
        return
    wvd1only = fileset1.difference(fileset2)
    wvd2only = fileset2.difference(fileset1)

    if wvd1only:
        if same_disk:
            print("Files on platter %d and not platter %d:" % (disk1_p+1, disk2_p+1))
        else:
            print("Files in %s and not in %s:" % (wvdname, wvd2name))
        print(sorted(wvd1only))

    if wvd2only:
        if same_disk:
            print("Files on platter %d and not platter %d:" % (disk2_p+1, disk1_p+1))
        else:
            print("Files in %s and not in %s:" % (wvd2name, wvdname))
        print(sorted(wvd2only))

    # 4) report if a files don't have the same type or status
    # 5) report if a files compare exactly (for now)
    # 6) show diff report ? (future enhancement)

    wvdMatching = []
    wvdMismatching = []
    for f in fileset1x: # pylint: disable=too-many-nested-blocks
        f1name = f.asStr()

        file1 = wvd.catalog[disk1_p].getFile(f)
        assert file1 is not None
        file1Data = file1.getSectors()

        if f1name in fileset2:
            file2list = wvd2.catalog[disk2_p].expandWildcards([f1name])
            assert file2list is not None
            assert len(file2list) == 1
            file2 = file2list[0]
            file2 = wvd2.catalog[disk2_p].getFile(file2)
            file2Data = file2.getSectors()

            matching = False
            if (file1Data is None) and (file2Data is None):
                matching = True
            elif (file1Data is None) or (file2Data is None):
                matching = False
            elif file1Data == file2Data:
                matching = True
            elif (file1.getType() == 'P ' and \
                  file2.getType() == 'P ' and \
                  file1.programSaveMode() in ['normal', 'protected'] and \
                  file1.programSaveMode() == file2.programSaveMode()):
                # there are bytes which don't matter after the EOB/EOD byte
                # of each sector which can create a false mismatch via an
                # exact binary match. here we create a listing if we can and
                # compare those.
                listing1 = listProgramFromBlocks(file1Data, False, 0)
                listing2 = listProgramFromBlocks(file2Data, False, 0)
                # the first line of the listing contains the sector number,
                # so we need to ignore that
                #listing1 = listing1[1:]
                #listing2 = listing1[2:]
                matching = listing1 == listing2
                if (not matching) and (verbose != ''):
                    if len(listing1) != len(listing2):
                        print("%s mismatch: left has %d lines, right has %d lines" \
                             % (f1name, len(listing1), len(listing2)))
                    if verbose == 'v':
                        for n in range(min(len(listing1), len(listing2))):
                            if listing1[n] != listing2[n]:
                                print("%s has first mismatch at line %d" % (f1name, n+1))
                                print(" left>", listing1[n])
                                print("right>", listing2[n])
                                break
                    if verbose == 'c':
                        diff = difflib.unified_diff(listing1, listing2, 'thiswvd', 'thatwvd', '', '', 3, '')
                        for txt in diff:
                            print(txt)

            if matching:
                wvdMatching.append(f1name)  # exact binary match
            else:
                wvdMismatching.append(f1name)

    if wvdMismatching:
        wvdMismatching.sort()
        if verbose != '': print('')  # put a blank line after details and before summary
        print("Mismatching files:")
        print(wvdMismatching)

    if wvdMatching:
        wvdMatching.sort()
        print("Matching files:")
        print(wvdMatching)

###################### dump file or range of sectors ######################
# dump all sectors of a file or a specified range of sectors in hex and ascii

def dumpFile(wvd: WangVirtualDisk, p: int, args: List[str]) -> None:
    (theFile, start, end) = filenameOrSectorRange(wvd, p, args)  # pylint: disable=unused-variable
    if start is None or end is None:
        return # invalid arguments

    for s in range(start, end+1):
        print("### Disk Sector", s, "###")
        dumpSector(wvd.getRawSector(p, s))
    return

def dumpSector(data: bytearray) -> None:
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

# pylint: disable=too-many-return-statements
def listFile(wvd: WangVirtualDisk, p: int,
             args: List[str], listd: bool) -> List[str]:

    (theFile, start, end) = filenameOrSectorRange(wvd, p, args)
    if start is None or end is None:
        return []  # invalid arguments

    if theFile is None:
        # start,end range given instead of a filename
        # TODO: first check if the range is a legal file type, and if so, list it,
        #       and only if they all fail, resort to listing block by block.
        rawblocks = []
        for n in range(start, end+1):
            rawblocks.append(wvd.getRawSector(p, n))
        return listArbitraryBlocks(rawblocks, listd, start)

    if not theFile.fileExtentIsPlausible():
        print("The file extent is not sensible; no listing generated")
        return []
    if not theFile.fileControlRecordIsPlausible():
        print("The file control record is not sensible; no listing generated")
        return []

    # make sure it is a program or data file
    blocks = theFile.getSectors()
    if blocks is None:
        return []

    options = { 'sector' : start, 'prettyprint' : listd }

    fileType = theFile.getType()
    if fileType == "P ":
        return handlers[0].listBlocks(blocks, options)
    if fileType == "D ":
        # TODO: iterate over all (data) handlers, instead of this hard-coded approach
        rv = handlers[1].checkBlocks(blocks, options)
        if not rv['errors']:
            return handlers[1].listBlocks(blocks, options)
        rv = handlers[2].checkBlocks(blocks, options)
        if not rv['errors']:
            return handlers[2].listBlocks(blocks, options)

    print("unknown file type")
    return []

############################ checkPlatter ############################
# ad-hoc check of disk integrity
# if report = 1, report warnings on stdout
# return False if things are OK; return True if errors

def checkPlatter(wvd: WangVirtualDisk, p: int, report: bool) -> bool:
    x = checkCatalogIndexes(wvd, p, report)
    if x: return x
    if wvd.catalog[p].hasCatalog():
        x = checkCatalog(wvd, p, report)
        if x: return x
    return False

# check the catalog index for internal consistency.
# the actual file contents are not checked.
#   (1) check that there is an index at all
#   (2) check that the disk parameter block is reasonable
#   (3) check that the index entries are valid
#       a. file status (unused, valid, scratched, or invalid),
#       b. file type (program, data)
#       c. check that the filename consists of reasonable characters
#       d. check that there are no duplicate file names
#       e. the file name is hashed into the right location
#       f. check that there are no gaps in the sequence of indexes
#       g. location of file extent makes sense
#       h. the file extent doesn't overlap that of any other file
#
# check that the catalog parameter block is sane
# pylint: disable=too-many-statements, too-many-branches, too-many-locals
def checkCatalogIndexes(wvd: WangVirtualDisk, p: int, report: bool) -> bool:
    if wvd.numPlatters() > 1:
        desc = 'Platter %d' % (p+1)
    else:
        desc = 'The disk'

    # -- (1) check that there is an index at all
    platter = wvd.catalog[p]
    if platter.numIndexSectors() == 0:
        if report: print("%s has no catalog" % desc)
        return False

    # -- (2) check that the disk parameter block is reasonable
    if platter.numIndexSectors() >= wvd.numSectors():
        if report: print("%s reports an index that larger than the disk" % desc)
        return True
    if platter.endCatalogArea() >= wvd.numSectors():
        if report: print("%s reports a last catalog sector that is larger than the disk size" % desc)
        return True
    if platter.endCatalogArea() <= platter.numIndexSectors():
        if report: print("%s reports a last catalog sector that is inside the catalog index area" % desc)
        return True
    if platter.currentEnd() > platter.endCatalogArea():
        if report: print("%s supposedly has more cataloged sectors in use than available" % desc)
        return True

    # record the extent of each file so we can check for overlap
    # we could simply use an array with one entry for each sector,
    # but that is wasteful for large disks.
    # each entry is a hash of ('name', 'start', 'end'), containing the
    # file name and its first and last sector extent.
    # Any because 'name' is str, others int
    extent_list: List[Dict[str, Any]] = []

    # keep track of the valid and scratched file names
    live_names: Dict[str, bool] = {}

    # note if any problems have been noted on this platter
    any_problems = False

    for secnum in range(platter.numIndexSectors()):

        found_problems = False  # any problems on this sector
        prev_slot_empty = False

        for slot in range(16):
            if secnum == 0 and slot == 0: continue  # first sector has 15, not 16 entries
            index_slot = secnum*16 + slot - 1
            indexEntry = platter.getIndexEntryByNumber(index_slot)
            indexState = indexEntry.getIndexState()
            fname = indexEntry.getFilename()
            (file_start, file_end) = indexEntry.getFileExtent()
            str_fname = fname.asStr()

            # -- (3a) check file status (unused, valid, scratched, or invalid),
            if indexState == 'unknown':
                if report: print("Sector %d, entry %d has unknown file state %s, filename '%s'" \
                                    % (secnum, slot, indexEntry.getIndexStateNumber(), str_fname))
                found_problems = True
                continue
            if indexState == 'empty':
                prev_slot_empty = True
                continue

            # -- (3b) check file type (program, data)
            fileType = indexEntry.getFileType()
            if fileType == '?':
                if report: print("Sector %d, entry %d has unknown file type %s (should be 0x00 or 0x80)" \
                                    % (secnum, slot, indexEntry.getFileTypeNumber()))
                found_problems = True
                continue

            # -- (3c) check that the filename consists of reasonable characters
            suspect_name = False
            for ch in fname.get():
                suspect_name = suspect_name or ch < 0x20 or ch >= 0x80
            if suspect_name:
                if report: print("(warning) suspect filename '%s'" % str_fname)

            # -- (3d) check for duplicate file names in the list of
            #         valid and scratched files.
            if indexState in ['valid', 'scratched']:
                if str_fname in live_names:
                    found_problems = True
                    if report: print("Sector %d, entry %d has unknown file state %s, filename '%s'" \
                                        % (secnum, slot, indexEntry.getIndexStateNumber(), str_fname))
                    continue
                live_names[str_fname] = True

            # -- (3e) check the file name is hashed into the right location
            # ignore it if the original bucket is full, because the file
            # spills to the next sector.  a more complete check would be to
            # test that not only is the original bucket full, but all
            # intermediate buckets in case it spilled more than once.
            hsh = platter.getFilenameHash(fname) % platter.numIndexSectors()
            if hsh != secnum:
                if not platter.isIndexBucketFull(hsh):
                    if report: print("'%s' filed in index sector %d, but it should be in sector %d" \
                                              % (str_fname, secnum, hsh))
                    found_problems = True
                    continue

            # -- (3f) check that there are no gaps in the sequence of indexes.
            #         entries are filled from the first slot to the last,
            #         so once there is an empty slot, all remaining slots in
            #         the current sector should be empty as well.
            if prev_slot_empty:
                if report: print("File '%s' has index at sector %d, entry %d, but it follows an empty index slot" % \
                            (str_fname, secnum, slot))
                found_problems = True
                continue

            # no further checks if this entry has been invalidated
            if indexState == 'invalid':
                continue

            # -- (3g) check location of file extent makes sense
            #print("Current: %s (%d,%d)" % (str_fname, file_start, file_end))
            if file_start < platter.numIndexSectors():
                if report: print("%s starts at sector %d, which is inside the catalog index" \
                            % (str_fname, file_start))
                found_problems = True
                continue
            if file_end < file_start:
                if report: print("%s ends (%d) before it starts (%d)" % (str_fname, file_end, file_start))
                found_problems = True
                continue
            if file_end >= wvd.numSectors():
                if report: print("%s is off the end of the disk (%d > %d)" % (str_fname, file_end, wvd.numSectors()))
                found_problems = True
                continue

            # -- (3h) check the file extent doesn't overlap that of any other file
            current = { 'name' : str_fname, 'start' : file_start, 'end' : file_end }
            #print("current: %s (%d,%d)" % (current['name'], current['start'], current['end']))
            overlap = False
            for old in extent_list:
                #print("old:     %s (%d,%d)" % (old['name'], old['start'], old['end']))
                if (old['start'] <= current['start'] <= old['end']) or \
                   (old['start'] <= current['end']   <= old['end']):
                    # TODO: type annotation is failing me here because current & old
                    #       are Dict[str, Union[str,int]] because the dict has both int and str,
                    #       but the really 'name' is always str and the others are always int.
                    if report: print("file extent of %s (%d,%d) overlaps that of %s (%d,%d)" \
                            % (current['name'], current['start'], current['end'], old['name'], old['start'], old['end']))
                    overlap = True
            extent_list.append(current)
            if overlap:
                found_problems = True
                continue

            # -- done with step 3

        # end checking this sector of this platter
        any_problems = any_problems or found_problems

    # all catalog index sectors have been checked
    return any_problems

# step through and verify each entry of a catalog on the indicated platter.
# returns True if there are errors.
# this might return garbage if the catalog index isn't valid, so this
# should be called only if checkCatalogIndexes() shows no problems.
def checkCatalog(wvd: WangVirtualDisk, p: int, report: bool) -> bool:
    platter = wvd.catalog[p]
    failures = False

    # step through each catalog entry
    for name in platter.allFilenames():
        indexEntry = platter.getIndexEntry(name)
        if indexEntry:
            bad, _ = checkFile(wvd, p, name, report)
            if bad:
                failures = True

    return failures

# check the structure of an individual file.
# returns True if there are errors.
# This is used both by the "check" command, and it gets called by the "catalog"
# command to indicate after each entry whether it appears to be valid.
def checkFile(wvd: WangVirtualDisk, p: int,
              cat_fname: WvdFilename,
              report: bool) -> Tuple[bool, str]:
    indexEntry = wvd.catalog[p].getIndexEntry(cat_fname)
    if indexEntry is None:
        print("Platter %d: %s: not found" % (p, cat_fname))
        return (True, '')

    str_fname = cat_fname.asStr()

    # FIXME: if the file control record is bad (eg, the last sector is all zeros)
    # LIST DC will have goofy numbers, but the program can still be loaded OK.
    # maybe this should be a warning, not a fatal error.
    if not indexEntry.fileControlRecordIsPlausible():
        if report: print("%s doesn't have a plausible file control record" % str_fname)
        return (True, '')

    # used must be fetched only after fileControlRecordIsPlausible() check
    # as it is located in the last sector of the sectors allocated for the file
    start = indexEntry.getFileStart()
    end   = indexEntry.getFileEnd()
    used  = indexEntry.getFileUsedSectors()

    # both data and program files use the last sector to indicate
    # how many sectors are used
    if start+used-1 > end:
        if report: print("%s's end block claims %d sectors used, but only %d allocated" % \
            (str_fname, used, end-start+1))
        return (True, '')

    # do program or data file specific checks
    blocks = []
    start = indexEntry.getFileStart()
    used  = indexEntry.getFileUsedSectors()
    for sec in range(start, start+used-1):
        secData = wvd.getRawSector(p, sec)
        blocks.append(secData)

    options = { 'sector': start, 'used': used }

    fmt = ''
    fileType = indexEntry.getFileType()
    if fileType not in ["P ","D ", "P'"]:
        if report: print("%s has unknown file type" % str_fname)
        return (True, '')
    for h in handlers:
        if fileType == h.fileType():
            file_status = h.checkBlocks(blocks, options)
            if not file_status['errors']:
                if h.name() == 'edit':
                    fmt = 'edit'
                break
    else:
        # no handler recognized it as valid
        return (True, 'unknown')

    # report errors and warnings if requested
    if report and file_status['failed']:
        if file_status['errors']:
            print("%s errors: " % str_fname)
            for err in file_status['errors']:
                print('    ', err)
        if file_status['warnings']:
            print("%s warnings: " % str_fname)
            for warn in file_status['warnings']:
                print('    ', warn)

    return (file_status['failed'], fmt)

########################################################################
# given a stream of sectors, try to identify coherent files, or even
# fragments of files.   return a list of hashes containing these entries:
#    { "first_record" : int,
#       "last_record" : int,
#       "file_type" : str,
#       "filename" : str  }
# where file_type is one of
#   "program"          -- a whole program
#   "program_header"   -- just the header of a program
#   "program_tailless" -- header plus some part of program, but no trailer
#   "program_headless" -- some part of program plus trailer, but no header
#   "program_fragment" -- program body with no header and no trailer
#   "data"             -- an apparently complete data file
#                         (it is hard to know if the start got truncated
#                          because there is no header record)
#   "data_fragment"    -- a part of a data file

# FIXME: when I rearranged the code to have a separate class for each file type,
# the intent was for this block to present all of the sectors to each handler,
# and each handler would return a count of the number of legal blocks in a row
# it could find.  if none succeeded, then one block would be stripped, and we'd
# try again. if one succeeded, then those N blocks would be classified as a file,
# the blocks would be stripped, and the remaining blocks resubmitted, etc.
# but for now, I just hacked up the old FSM logic as it was the quickest way to
# get something working again.  this approach recognizes only program and data
# files, which in theory is enough, but by switching to the specialized decoders,
# it would be possible to recover more information, eg, EDIT can figure out the
# name of the file (although currently there is no way for the checker to
# return that information).
#
# FIXME: this fails to detect SAVEP and SAVE! files
def guessFiles(blocks: List[bytearray], abs_sector: int
              ) -> List[Dict[str, Any]]:
    func_dbg = False
    files = [] # holds the list of file bits

    # encode what kind of fragment we are seeing
    (  st_unknown,        # we're lost
       st_pgm_hdr,        # we just saw a program header
       st_pgm_hdr_body,   # we have seen a program header and some program body
       st_pgm_body,       # we have seen program body, but no header
       st_data_ok_whole,  # we're seen some number of complete logical data records
       st_data_ok_part,   # we're seen some number of complete logical data records
                          # and we have seen the start of another logical record
    ) = list(range(6))
    state = st_unknown
    fileinfo: Dict[str, Any] = {}

    # logical records can span multiple physical records.  we track the sequence
    # number across records to make sure things are ocnsistent
    logical_data_record = 0
    prev_data_phys_seq = -1

    for offset, secData in enumerate(blocks):
        secnum = abs_sector + offset
        options = { 'sector' : secnum }

        if func_dbg: print("# ============== sector %d ==============" % secnum)

        # break out the SOB flag bits
        data_file                = secData[0] & 0x80
        header_record            = secData[0] & 0x40
        trailer_record           = secData[0] & 0x20
        protected_file           = secData[0] & 0x10
        intermediate_phys_record = secData[0] & 0x02
        last_phys_record         = secData[0] & 0x01

        if func_dbg: print("# data=%d, header=%d, trailer=%d, prot=%d, last_phys=%d, middle_phys=%d" \
                            % (data_file != 0,
                               header_record != 0, trailer_record != 0,
                               protected_file != 0,
                               last_phys_record != 0, intermediate_phys_record != 0))

        # these indicate if the sector contents, irrespective of the SOB byte,
        # look like a given type of sector
        dummyname = WvdFilename(bytearray(8))

        rv = handlers[0].checkBlocks([secData], options)
        pgm_ok = not rv['errors'] and not rv['warnings']
        if False and secnum == 11:
            for m in rv['errors']:
                print("err:  ", m)
            for m in rv['warnings']:
                print("warn: ", m)
        valid_pgm_hdr = pgm_ok and not data_file and header_record
        valid_pgm_mid = pgm_ok and not data_file and not header_record and not trailer_record and 0xFD in secData
        valid_pgm_end = pgm_ok and not data_file and not header_record and     trailer_record and 0xFE in secData
        if func_dbg: print("# pgm_ok=%d, pgm_hdr=%d, pgm_mid=%d, pgm_end=%d" \
                           % (pgm_ok, valid_pgm_hdr != 0, valid_pgm_mid != 0, valid_pgm_end != 0) )

        rv = handlers[2].checkBlocks([secData], options)
        data_blk_ok = not rv['errors'] and not rv['warnings']
        valid_data_start = data_file and data_blk_ok and \
                           (intermediate_phys_record or last_phys_record) and \
                           (secData[1] == 0x01)
        valid_data_mid = data_file and data_blk_ok and \
                         (intermediate_phys_record or last_phys_record) and \
                         (secData[1] == (prev_data_phys_seq+1))
        valid_data_trailer = data_file and trailer_record

        if state == st_pgm_hdr:
            # we've seen a program header so far
            if valid_pgm_mid:
                state = st_pgm_hdr_body
                if func_dbg: print("pgm_hdr: got mid -> pgm_hdr_body")
                continue  # on to next sector
            if valid_pgm_end:
                fileinfo['last_record'] = secnum
                fileinfo['file_type'] = 'program'
                files.append(fileinfo)
                state = st_unknown
                if func_dbg: print("pgm_hdr: got end -> unknown")
                fileinfo = {}
                continue  # on to next sector
            # didn't get a logical continuation -- end with just the header
            fileinfo['last_record'] = secnum-1
            fileinfo['file_type'] = 'program_tailless'
            files.append(fileinfo)
            state = st_unknown
            if func_dbg: print("pgm_hdr: got unexpected -> unknown")
            fileinfo = {}

        if state == st_pgm_hdr_body:
            # we've seen a head and a partial body so far
            if valid_pgm_mid:
                state = st_pgm_hdr_body
                if func_dbg: print("pgm_hdr_body: -> pgm_hdr_body")
                continue  # on to next sector
            if valid_pgm_end:
                fileinfo['last_record'] = secnum
                fileinfo['file_type'] = 'program'
                files.append(fileinfo)
                state = st_unknown
                if func_dbg: print("pgm_hdr_body: got end -> unknown")
                fileinfo = {}
                continue  # on to next sector
            # didn't get a logical continuation -- end with just the header
            fileinfo['last_record'] = secnum-1
            fileinfo['file_type'] = 'program_tailless'
            files.append(fileinfo)
            state = st_unknown
            if func_dbg: print("pgm_hdr_body: got unexpected -> unknown")
            fileinfo = {}

        if state == st_pgm_body:
            # we've seen a headless program body
            if valid_pgm_mid:
                if func_dbg: print("pgm_body: -> pgm_body")
                continue  # on to next sector
            if valid_pgm_end:
                fileinfo['last_record'] = secnum
                fileinfo['file_type'] = 'program_headless'
                files.append(fileinfo)
                state = st_unknown
                fileinfo = {}
                if func_dbg: print("pgm_body: got pgm end -> unknown")
                continue  # on to next sector
            # didn't get a logical continuation -- end with just the header
            fileinfo['last_record'] = secnum-1
            fileinfo['file_type'] = 'program_fragment'
            files.append(fileinfo)
            state = st_unknown
            if func_dbg: print("pgm_body: got unexpected -> unknown")
            fileinfo = {}

        if state == st_data_ok_part:
            if func_dbg and data_file and \
                         (intermediate_phys_record or last_phys_record):
                print("data_ok_part: physical record %d, expecting %d" \
                            % (secData[1], prev_data_phys_seq+1))
            if valid_data_mid and not last_phys_record and \
                (secData[1] == prev_data_phys_seq+1):
                state = st_data_ok_part
                if func_dbg: print("data_ok_part: got more data -> data_ok_part")
                prev_data_phys_seq = prev_data_phys_seq+1
                continue  # on to next sector
            if valid_data_mid and last_phys_record and \
                (secData[1] == prev_data_phys_seq+1):
                state = st_data_ok_whole
                logical_data_record = logical_data_record+1
                if func_dbg: print("data_ok_part: got data -> st_data_ok_whole (log record %d)" % logical_data_record)
                continue  # on to next sector
            # didn't get logical continuation -- end it here
            fileinfo['last_record'] = secnum-1
            fileinfo['file_type'] = 'data_fragment'
            fileinfo['filename'] = 'DF_%05d' % fileinfo['first_record']
            files.append(fileinfo)
            state = st_unknown
            if func_dbg: print("data_ok_part: got unexpected -> unknown")
            fileinfo = {}

        if state == st_data_ok_whole:
            if valid_data_start and not last_phys_record:
                state = st_data_ok_part
                if func_dbg: print("data_ok_whole: got more data -> data_ok_part")
                prev_data_phys_seq = 1
                continue  # on to next sector
            if valid_data_start and last_phys_record:
                state = st_data_ok_whole
                logical_data_record = logical_data_record+1
                if func_dbg: print("data_ok_whole: got data -> st_data_ok_whole (log record %d)" % logical_data_record)
                continue  # on to next sector
            if valid_data_trailer:
                fileinfo['last_record'] = secnum
                fileinfo['file_type'] = 'data'
                fileinfo['filename'] = 'DAT%05d' % fileinfo['first_record']
                files.append(fileinfo)
                state = st_unknown
                if func_dbg: print("data_ok_whole: got trailer -> unknown")
                fileinfo = {}
                continue
            # didn't get logical continuation -- end it here
            fileinfo['last_record'] = secnum-1
            fileinfo['file_type'] = 'data'
            fileinfo['filename'] = 'DAT%05d' % fileinfo['first_record']
            files.append(fileinfo)
            state = st_unknown
            if func_dbg: print("data_ok_whole: got unexpected -> unknown")
            fileinfo = {}

        if state == st_unknown:
            if valid_pgm_hdr:
                fileinfo['first_record'] = secnum
                fileinfo['filename'] = secData[1:9]   # bytearray
                state = st_pgm_hdr
                if func_dbg: print("unknown: -> pgm_hdr, filename=%s" % sanitize_filename(secData[1:9]))
                continue  # on to next sector
            if valid_pgm_mid:
                fileinfo['first_record'] = secnum
                fileinfo['filename'] = 'PF_%05d' % secnum  # Program Fragment
                state = st_pgm_body
                if func_dbg: print("unknown: -> pgm_body")
                continue  # on to next sector
            if valid_pgm_end:
                fileinfo['first_record'] = secnum
                fileinfo['last_record'] = secnum
                fileinfo['file_type'] = 'program_headless'
                fileinfo['filename'] = 'PF_%05d' % secnum  # Program Fragment
                files.append(fileinfo)
                state = st_unknown
                if func_dbg: print("unknown: got prm end -> unknown")
                fileinfo = {}
                continue  # on to next sector
            if valid_data_start and not last_phys_record:
                fileinfo['first_record'] = secnum
                state = st_data_ok_part
                if func_dbg: print("unknown: got data -> data_ok_part")
                logical_data_record = 0
                prev_data_phys_seq = 1
                continue  # on to next sector
            if valid_data_start and last_phys_record:
                fileinfo['first_record'] = secnum
                state = st_data_ok_whole
                logical_data_record = 1
                if func_dbg: print("unknown: got data -> st_data_ok_whole (log record %d)" % logical_data_record)
                continue
            # don't know what it is
            if func_dbg: print("unknown: at a loss; ignoring this sector")

    return files

############################# command parser #############################

# this is an abstract class that all command objects should override
# pylint: disable=no-self-use, useless-object-inheritance
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
    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        # pylint: disable=unused-argument
        return # Overload doCommand() in command object subclass

# ----------------------------------------------------------------------

# cmdSet is a hash keyed on the command name, storing a command handler instance
cmdSet:        Dict[str, cmdTemplate] = {}
cmdSetAliases: Dict[str, cmdTemplate] = {}

def registerCmd(cmdInst: cmdTemplate) -> None:
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

def cmdUsage(cmdName: Optional[str]=None) -> None:
    if cmdName is None:
        print()
        print('Usage:')
        print('   ', basename, '<filename> [<command>[/<platter>] [<args>]] [<redirection>]')
        print()
        print('With no command, enter into command line mode, where each subsequent line')
        print('contains one of the commands, as described below. The output can be optionally')
        print('redirected to the named logfile, either overwriting, ">" or appending, ">>".')
        print('The output can also be piped through a pager by ending with "| more".')
        print('Arguments containing spaces can be surrounded by double quote characters.')
        print()
        print('For multiplatter disks, the platter can be specified by following the command')
        print('name with a slash and the decimal platter number. For commands where it makes')
        print('sense, "/all" can be specified to operate on all platters.')
        print()
        print('<redirection> is optional, and takes one of three forms:')
        print('   ... >   <logfile>  -- write command output to a logfile')
        print('   ... >>  <logfile>  -- append command output to a logfile')
        print('   ... | more         -- send command output through a pager')
        print()
        print('If there are arguments after the filename, it is interpreted as a single')
        print('command, which is executed, and then the program immediately exits. E.g.')
        print('    wvdutil.py listd BOWLING > bowling.bas')
        print('If the command changes the disk state, it will not be saved unless the "-save"')
        print('flag is passed after the filename.')
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

    print('No command "' + cmdName + '"')
    return

# report all commands and optionally quit
def usage(terminate: bool=True) -> None:
    cmdUsage()
    if terminate:
        sys.exit()

# check if one or more of the specified platters has a catalog
# pass in a wvd and a list of platters that we want to inspect
# returns None if no platters exist, and a list containing a subset
# of the original platters which do have catalogs.
def pickUsefulCatalog(wvd: WangVirtualDisk, platters: List[int]
                     ) -> Optional[List[int]]:
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

    # multiplatter
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
def cmdDispatch(wvd: WangVirtualDisk, cmd: str, args: List[str]) -> None:
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

    # try to find a command handler
    if cmd in cmdSet:
        action = cmdSet[cmd]
    elif cmd in cmdSetAliases:
        action = cmdSetAliases[cmd]
    else:
        print('Unknown command:', cmd)
        print('Type "help" for the list of commands')
        return

    if action.needsCatalog():
        useable_platters = pickUsefulCatalog(wvd, platters)
        if useable_platters is None or not useable_platters:
            return

    # perform the command
    action.doCommand(wvd, platters, args)

# ----------------------------------------------------------------------

# given a disk image and a command, perform the command (or bitch)
def command(wvd: WangVirtualDisk, cmdString: str) -> None:
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
    redir_idx = [i for i, x in enumerate(words) if x in ['|', '>', '>>']]
    redir = None
    if redir_idx:
        if len(redir_idx) > 1:
            print("Too many redirection specifiers")
            return
        idx = redir_idx[0]
        redir = words[idx]
        if idx != len(words)-2:
            print("Illegal redirection syntax")
            return
        redirFile = words[-1]
        del words[idx:]  # strip off redir symbol and everything after

    origStdout = sys.stdout
    pager_file = None
    if redir == '|':
        # "| more" must be the final two words
        if redirFile != "more":
            print("Bad pipe command")
            return
        # os.popen and subprocess.Popen didn't want to work,
        # so dump the output to a temp file and page it later
        tmp_fd, pager_file = tempfile.mkstemp()
        sys.stdout = os.fdopen(tmp_fd, 'w+')
    elif redir in ['>', '>>']:
        try:
            buffered = 1
            if redir == '>':
                sys.stdout = open(redirFile, 'w', buffered)
            else:
                sys.stdout = open(redirFile, 'a', buffered)
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        if args:
            # TODO: allow checking a specified file or wildcard
            #       e.g., "check FOOTBALL" or "CHECK DIAG*"
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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
                fragmentary_files = 0
                for fileinfo in files:
                    if fileinfo['file_type'] in ('program_headless', 'program_tailless', 'program_fragment', 'data_fragment'):
                        fragmentary_files += 1
                    if flag_good and (fileinfo['file_type'] == 'program'):
                        print("Sector %5d to %5d: name='%s' [ program ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record'],
                               sanitize_filename(fileinfo['filename'])))
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
                               sanitize_filename(fileinfo['filename'])))
                    if flag_bad and (fileinfo['file_type'] == 'data_fragment'):
                        print("Sector %5d to %5d: name='%s' [ data fragment ]" \
                            % (fileinfo['first_record'],
                               fileinfo['last_record'],
                               sanitize_filename(fileinfo['filename'])))
                if not flag_bad and fragmentary_files:
                    print("(%d fragmentary files not shown. use 'scan -all' to see everything." % fragmentary_files)
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
"""compare[/<platter>] /<platter> [-v|-verbose|-c|-context] [<wildcard list>]
compare[/<platter>] disk2.wvd[/<platter>] [-v|-verbose|-c|-context] [<wildcard list>]

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
    mismatch, if they are program files.

    The -c or -context flag will produce a context diff of the files,
    if they are program files."""

    def needsCatalog(self):
        return True

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        # FIXME: this needs an overhaul
        if (not args) or (len(platters) > 1):
            cmdUsage('compare')
            return

        # look for flags
        verbose = ''
        argsout = []
        for arg in args:
            if arg in ('-v', '-verbose'):
                verbose = 'v'
            elif arg in ('-c', '-context'):
                verbose = 'c'
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        if len(platters) > 1:
            print("This command can only operate on a single platter")
            return
        if (not args) or (len(args) > 2):
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        if len(args) > 1:
            print("This command takes no arguments")
            return
        reportMetadata(wvd)
        return

registerCmd(cmdMeta())

# ----------------------------------------------------------------------

class cmdLabel(cmdTemplate):

    def name(self):
        return "label"
    def shortHelp(self):
        return "report or set the disk label"
    def longHelp(self):
        return \
"""label "string of words|second line"
    Sets the .wvd label metadata, using "|" as a line separator.
    Issuing "label" with no arguments reports the existing label."""

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        if not args:
            for line in wvd.label().splitlines():
                print("    ", line.rstrip())
        elif len(args) == 1:
            wvd.setLabel(args[0])
        else:
            print("This command takes zero or one argument (typically a quoted string)")

registerCmd(cmdLabel())

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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        if not args:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
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

    def doCommand(self, wvd: WangVirtualDisk, platters: List[int], args: List[str]) -> None:
        global default_platter  # pylint: disable=global-statement
        if len(args) > 1:
            cmdUsage('platter')
            return
        if not args:
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
# sensibly. each block is listed independently.

def listArbitraryBlocks(blocks: List[bytearray], listd: bool, abs_sector: int) -> List[str]:
    listing = []

    for offset, secData in enumerate(blocks):
        secnum = abs_sector + offset

        listing.append("============== sector %d ==============" % secnum)

        # find if any handlers think this is legal.
        # we stop at the first match to ensure subtypes of data file match
        # before the generic data file handler does.
        options = { 'sector' : secnum, 'prettyprint': listd }
        for h in handlers:
            rv = h.checkBlocks([secData], options)
            if not rv['errors'] and not rv['warnings']:
                _, text = h.listOneBlock(secData, options)
                # listing.append("# type: " + h.name())
                listing.extend(text)
                break
        else:
            # don't know what it is -- just dump it
            # FIXME: this is lifted from wvdutil.py's dumpSector() routine
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

########################################################################
# given a list of file blocks, return a program file listing

def listProgramFromBlocks(blocks: List[bytearray], listd: bool, abs_sector: int
                         ) -> List[str]:
    options = { 'sector' : abs_sector, 'prettyprint' : listd }
    return handlers[0].listBlocks(blocks, options)

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
# the range is exclusive. each position may be None if it isn't valid.
def filenameOrSectorRange(wvd: WangVirtualDisk, p: int, args: List[str]
                         ) -> Tuple[Optional[CatalogFile], Optional[int], Optional[int]]:
    badReturn = (None, None, None)

    (start, end) = (-1, -1)

    if (len(args) == 1) and wvd.catalog[p].hasCatalog():
        # it might be a name or a wildcard
        filename = getOneFilename(wvd, p, args[0])
        if filename is not None:
            theFile = wvd.catalog[p].getFile(filename)
            assert theFile is not None
            if not theFile.fileExtentIsPlausible():
                print("The file extent doesn't make sense")
                return badReturn
            start = theFile.getStart()  # all allocated sectors
            used  = theFile.getUsed()
            end   = start+used-1-1      # all meaningful sectors; extra -1 for control record
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
        (start >= wvd.numSectors()) or (end >= wvd.numSectors()):
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
        print([cat.getFile(x).getFilename().asStr() for x in outlist])
        return None
    return outlist[0]

############################ main program ############################

def mainloop() -> None:
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

    # check for optional "-save" flag
    # this is useful when running a command on the command line that also
    # changes state, eg, wvdutil.py -save label "this is the new label"
    save_changes = '-save' in sys.argv
    if save_changes:
        sys.argv.remove('-save')

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
        # strings containing spaces were indistinguishable from two words,
        # eg: list "TBO DIAG" was showing up to command as "list TBO DIAG"
        # commandStr = " ".join(sys.argv[2:])
        # just quote everything
        commandStr = ""
        for next_arg in sys.argv[2:]:
            commandStr = commandStr + '"' + next_arg + '"' + ' '
        command(wvd, commandStr)
        if wvd.isDirty():
            if save_changes:
                wvd.write(wvd.getFilename())
            else:
                print("Warning: changes were not saved")
        return

    print(basename + ', version 1.12, 2020/12/27')
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
        except:
            print("<<<internal error>>> sorry about that")


if __name__ == "__main__":
    basename = os.path.basename(sys.argv[0])
    mainloop()
    sys.exit()
