# Program: wvdlib.py, used by wvdutil.py
# Purpose: encapsulates some useful routines for manipulating .wvd files
# Author:  Jim Battle
#
# Version: 1.2, 2008/08/18, JTB
# Version: 1.3, 2011/08/13, JTB
#     added support to read .zip images
# Version: 1.4, 2011/10/08, JTB
#     made lib more robust in the face of corrupt disk images
#     "check" command much more thorough
# Version: 1.5, 2018/01/01, JTB
#     made it python 2/3 compatible
#     using bytearray data instead of character strings
#     pylint cleanups, mypy type annotation
# Version: 1.6, 2018/09/15, JTB
#     added function to handle unscrambling a scrambled sector
#     pylint cleanups

########################################################################
# technical notes
#
# in the virtual disk, a 256 byte block preceeds the raw sector data.
# sectors always have 256 bytes in the Wang world.
#
# on a wang disk, the first 16 bytes of the first sector describe the
# disk configuration:
#     if sector 0, byte 0 == HEX(00)
#         it is old index style (no marker)
#         byte  1   = number of sectors for index
#         bytes 2,3 less 1 = current end
#         bytes 4,5 less 1 = end catalog area
#     if sector 0, byte 0 == HEX(01)
#         it is a new index style ("'" marker)
#         bytes 1-5 are same as old index style
#     if sector 0, byte 0 == HEX(02)
#         it is a tri-byte index style ("&" marker)
#         bytes 1,2   = # of sectors for index
#         bytes 3,4,5 less 1 = current end
#         bytes 6,7,8 less 1 = end catalog area
#
# In the catalog structure, there is room for eight characters per file.
# unused characters are padded with spaces at the end.  In this utility,
# all filenames that get compared are padded to eight places with spaces
# before comparison.

from __future__ import print_function
from typing import List, Dict, Tuple, Any, Union, Optional  # pylint: disable=unused-import
import sys
import re

########################################################################
# simple exception class

class ProgramError(Exception):
    def __init__(self, value):
        Exception.__init__(self, value)
        self.value = value
    def __str__(self):
        return repr(self.value)

########################################################################
# this represents top-level information about a wang virtual disk

# pylint: disable=useless-object-inheritance
class WangVirtualDisk(object):

    # creation always produces an invalid disk.
    # currently the only way to get a valid disk is from reading one;
    # one can't be constructed from scratch.
    def __init__(self):
        self._valid = False   # the rest of self.members are meaningless
        self._dirty = False   # contents modified from on-disk version
        self._filename = None # type: str  # host filename of wvd file
        self.sector = []      # type: List[bytearray]
        self.head_dat = bytearray()
        self.catalog = []     # TODO: ttype: List[Catalog] -- enabling this causes tons of warnings
                              # one entry per platter

    # returns True if the in-memory disk image has been modified
    def isDirty(self):
        # type: () -> bool
        return self._dirty

    # read a virtual disk disk from a file into memory
    def read(self, filename):
        # type: (str) -> None
        if filename[-4:] == '.zip':
            import traceback
            try:
                import zipfile
                zipf = zipfile.ZipFile(filename, 'r')
                firstname = zipf.namelist()[0]
                # python 2.5 doesn't allow opening as file object,
                # and fatcow is using python 2.5.  deal with it.
                # fh = zipf.open(firstname, 'r')
                fdata = bytearray(zipf.read(firstname))  # in python2 read returns a string
            except Exception as e:  # pylint: disable=unused-variable, broad-except
                traceback.print_exc()
                print('Something went wrong opening zip file')
        else:
            fh = open(filename, 'rb')
            fdata = bytearray(fh.read())   # in python2 read() returns a string
            fh.close()
        self._filename = filename
        self.head_dat = fdata[0:256]  # get wvd header
        if self.head_dat[0:5] != b'WANG\x00':
            print('File is not a .wvd type')
            raise IOError
        self.sector = []
        for i in range(self.numPlatters() * self.numSectors()):
            self.sector.append(fdata[256*(i+1) : 256*(i+2)])
            i = i+1
        for p in range(self.numPlatters()):
            self.catalog.append(Catalog(self, p))
        self._valid = True
        self._dirty = False

    # dump the current disk image to the specified file
    def write(self, filename):
        # type: (str) -> None
        fh = open(filename, 'wb')
        fh.write(self.head_dat)
        for i in range(self.numSectors()):
            fh.write(self.sector[i])
        fh.close()
        self._dirty = False

    # get metadata about the virtual disk
    def getFilename(self): # type: () -> str
        return self._filename
    def getWriteFormat(self): # type: () -> int
        return self.head_dat[5]
    def getReadFormat(self): # type: () -> int
        return self.head_dat[6]
    def getWriteProtect(self): # type: () -> int
        return self.head_dat[7]
    def numSectors(self): # type: () -> int
        return self.head_dat[8] + 256*self.head_dat[9]
    def mediaType(self): # type: () -> int
        return self.head_dat[10]
    def numPlatters(self): # type: () -> int
        return self.head_dat[11] + 1
    def label(self): # type: () -> str
        return (self.head_dat[16:256].decode('latin-1').split('\0'))[0]

    # the first generation disk controller developed by Wang took a 16b
    # sector address, but it ignored the MSB, since at the time no media
    # had > 32K sectors.  for unknown reasons, when a disk was inserted in
    # the R drive of such a controller, the msb of all sector addresses
    # would be set, and those written in the F drive would not be set.
    #
    # later drives had >32K sectors/platter, and could have multiple
    # addressible platters as well.  these "intelligent" disk controllers
    # used all 16 bits of the sector address.
    def getSectorMask(self):
        # type: () -> int
        if (self.numPlatters() > 1) or (self.numSectors() > 32768):
            return 0xFFFF
        return 0x7FFF

    # set metadata
    # unused
    def setWriteProtect(self, prot):
        # type: (bool) -> None
        # self.head_dat[7] = (prot != 0)
        if prot:
            protbyte = 0x01
        else:
            protbyte = 0x00
        self.head_dat = self.head_dat[0:7] + bytearray([protbyte]) + self.head_dat[8:256]
        self._dirty = True

    # unused
    def setMediaType(self, kind):
        # type: (int) -> None
        assert kind in (0, 1, 2)
        self.head_dat[10] = kind
        self._dirty = True

    # unused
#   def setLabel(self, label):
#       # type: (bytearray) -> None
#       label = label[0:256-16]              # chop off excess
#       label += bytearray([ord(' ')]) * (256-16 - len(label)) # pad if required
#       self.head_dat[16:256] = bytearray(label)
#       self._dirty = True

    def checkSectorAddress(self, p, n):
        # type: (int, int) -> None
        if p >= self.numPlatters():
            print('Error: attempted to access invalid platter #', p)
            print(self.numPlatters())
            raise ProgramError
        if n >= self.numSectors():
            print('Error: attempted to access invalid sector #', n)
            print(self.numSectors())
            raise ProgramError

    def getRawSector(self, p, n):
        # type: (int, int) -> bytearray
        self.checkSectorAddress(p, n)
        nn = p*self.numSectors() + n
        return self.sector[nn]

    # replace a sector
    def setRawSector(self, p, n, data):
        # type: (int, int, bytearray) -> None
        assert len(data) == 256
        self.checkSectorAddress(p, n)
        nn = p*self.numSectors() + n
        self.sector[nn] = data
        self._dirty = True

########################################################################
# this is a very simple class which is exists mostly to give the mypy type
# checker something to work with.  that is, the wvd filenames are passed
# around as bytearray()s of length 8. other bytearrays are being passed
# around, so this allows us to tag which type we are dealing with.

# pylint: disable=useless-object-inheritance
class WvdFilename(object):
    def __init__(self, filename):
        # type: (bytearray) -> None
        assert len(filename) == 8
        self._filename = filename
    def get(self): # type: () -> bytearray
        return self._filename
    def asStr(self): # type: () -> str
        '''Return the on-disk filename as a trimmed string.'''
        trimmed = self._filename.decode('latin-1').rstrip()
        return trimmed

########################################################################
# this object represents one slot in the index table of a particular platter.
# the various index attributes can be read out and modified;
# calling the update() method pushes all changes back to the wvd image.

# byte 0 describes the index:
#    bit 0: 1 means valid entry, but file was scratched
#    bit 4: 1 means valid entry
#    bit 5: 1 means invalid; eg, a file was there, but space was recycled,eg
#              SAVE DCF "FOO"         (type = 0x10)
#              SCRATCH F "FOO"        (type = 0x11)
#              SAVE DCF("FOO") "BAR"  ("FOO" type = 0x21, "BAR" type=0x10)
#    the save P and save ! options are not stored in index
#    the number of used sectors is not stored in the header
#
# byte 1 describes the file type
#    0x80 = program
#    0x00 = data
#
# bytes 2:3 point to the first sector of the file
# bytes 4:5 point to the final sector of the file
# bytes 6:7 are unused
#
# I would strongly suspect that the first entry with idxState = 0x00 would
# terminate the search for more files in the sector.
#
# these are private vars of the class:
#    _wvd      = the disk where the image came from
#    _platter  = which platter the catalog is on
#    _indexNum = the disk where the image came from
#    _record   = the 16 byte index entry
#    _dirty    = true if any fields have changed

# pylint: disable=useless-object-inheritance
class WvdIndex(object):
    def __init__(self, wvd, p, n, record):
        # type: (WangVirtualDisk, int, int, bytearray) -> None
        assert len(record) == 16
        assert p < wvd.numPlatters()
        self._wvd      = wvd     # disk the index belongs to
        self._platter  = p       # platter of disk
        self._indexNum = n       # location in index it is from
        self._record   = record  # the index itself
        self._dirty    = False   # not modified

    # return the record as a 16 byte binary string
    def asBytes(self):
        # type: () -> bytearray
        return self._record

    def getFilename(self):
        # type: () -> WvdFilename
        return WvdFilename(self._record[8:16])

    def getIndexState(self):
        # type: () -> str
        val = self._record[0]
        if val == 0x00:
            return 'empty'
        if val == 0x10:
            return 'valid'
        if val == 0x11:
            return 'scratched'
        # TODO: comletter october 1979 No. 8 has a small article called
        # "2200 Disk Catalog Index Structure". it says 0x21 indicates a
        # formerly scratched file has had its reserved sectors reclaimed.
        # study this more and see how it affects this program.
        if val == 0x21:
            return 'invalid'
        return 'unknown'

    def getIndexStateNumber(self):
        # type: () -> str
        return '0x%02X' % self._record[0]

    def setIndexState(self, kind):
        # type: (str) -> None
        val = { 'empty':     0x00,
                'valid':     0x10,
                'invalid':   0x21,
                'scratched': 0x11 }[kind]
        self._record = bytearray([val]) + self._record[1:16]
        self._dirty = True

    def getFileType(self):
        # type: () -> str
        if self._record[1] == 0x80:
            return 'P'
        if self._record[1] == 0x00:
            return 'D'
        return '?'

    def getFileTypeNumber(self):
        # type: () -> str
        return '0x%02X' % self._record[1]

    # unused
#   def setFileType(self, kind):
#       # type: (str) -> None
#       assert kind in ('P', 'D'), "Bad type in setFileType"
#       val = { 'P': 0x80, 'D': 0x00 }[kind]
#       self._record = bytearray([self._record[0], val]) + self._record[2:16]
#       self._dirty = True

    # inspect a program file and determine if it is protected (and what kind)
    # or not returns "normal", "protected", or "scrambled".  if it isn't a
    # program file or the control bytes aren't consistent, "unknown" is
    # returned.
    # pylint: disable=too-many-return-statements
    def programSaveMode(self):
        # type: () -> str
        if self.getFileType() != 'P':
            return 'unknown'
        if not self.fileExtentIsPlausible():
            return 'unknown'

        mode = 'normal'  # until proven otherwise

        start = self.getFileStart()
        used  = self.getFileUsedSectors()
        for sec in range(used):
            secData = self._wvd.getRawSector(self._platter, start + sec)

            # FIXME: in the wild there are files where the header block does
            # not indicate protected, yet the sectors are scrambled. also, it
            # is not required that all sectors be scrambled: MVP 3.5 checks
            # each block separately and descrambles as necessary.
            if sec == 0:  # header block
                if (secData[0] & 0xF0) == 0x50:
                    mode = 'protected'
                elif (secData[0] & 0xF0) == 0x40:
                    mode = 'normal'
                else:
                    return 'unknown'
            else:
                # test if scrambled (see MVP OS 3.5 file BPMVP42D)
                if ((secData[0] & 0xD0) == 0x10) and \
                   ((secData[1] & 0x80) == 0x00):
                    return 'scrambled'

        return mode

    # this combines the index state with the file type
    def getFileStatus(self):
        # type: () -> str
        idxState = self.getIndexState()
        if idxState == 'invalid':
            return  'error' # this index is unused
        if idxState == 'invalid':
            return  'invalid' # this index was used, but is now invalid
        if idxState == 'valid':
            return ' ' + self.getFileType()  # append P or D
        if idxState == 'scratched':
            return 'S' + self.getFileType()  # append P or D
        return idxState  # error message

    def getFileStart(self):
        # type: () -> int
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            return bytes2toInt(self._record[2:4]) & self._wvd.getSectorMask()
        return bytes3toInt(self._record[2:5])

    def getFileEnd(self):
        # type: () -> int
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            return bytes2toInt(self._record[4:6]) & self._wvd.getSectorMask()
        return bytes3toInt(self._record[5:8])

    def getFileExtent(self):
        # type: () -> Tuple[int,int]
        return (self.getFileStart(), self.getFileEnd())

    # returns True if the file extent passes basic sanity checks
    def fileExtentIsPlausible(self):
        # type: () -> bool
        start = self.getFileStart()
        end   = self.getFileEnd()
        # pylint: disable=chained-comparison
        return (start < end < self._wvd.numSectors()) and \
               (start >= self._wvd.catalog[self._platter].numIndexSectors())

    # unused
#   def setFileExtent(self, start, end):
#       # type: (int, int) -> None
#       assert start >= 0
#       assert start < end
#       if self._wvd.catalog[self._platter].getIndexType() < 2:
#           assert end < (1 << 16)
#           self._record = self._record[0:2] + intToBytes2(start) \
#                        + intToBytes2(end) + self._record[6:16]
#       else:
#           assert end < (1 << 24)
#           self._record = self._record[0:2] + intToBytes3(start) \
#                        + intToBytes3(end) + self._record[8:16]
#       self._dirty = True

    # the last record of a file indicates how many sectors are in use
    def getFileUsedSectors(self):
        # type: () -> int
        assert self.fileExtentIsPlausible()
        end = self.getFileEnd()
        secData = self._wvd.getRawSector(self._platter, end)
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            used = bytes2toInt(secData[1:3])
        else:
            used = bytes3toInt(secData[1:4])
        return used

    # returns True if the file control record passes basic sanity checks
    def fileControlRecordIsPlausible(self):
        # type: () -> bool
        if not self.fileExtentIsPlausible():
            return False
        (start, end) = self.getFileExtent()
        used = self.getFileUsedSectors()
        filetype = self.getFileType()
        if filetype == 'P':
            valid_prefix = (0x20,)  # type: Tuple[int,...]
        elif filetype == 'D':
            valid_prefix = (0xa0,)
        else:
            valid_prefix = (0x20, 0xa0)  # be generous and allow either
        secData = self._wvd.getRawSector(self._platter, end)
        return (used <= (end-start+1)) and \
               ((secData[0] & 0xF0) in valid_prefix)

    # return how many sectors the file uses, including header, trailer,
    # and control records
    def getFileFreeSectors(self):
        # type: () -> int
        assert self.fileExtentIsPlausible()
        (start, end) = self.getFileExtent()
        used = self.getFileUsedSectors()
        free = ((end - start+1) - used) % 65536
        return free

    def isDirty(self):
        # type: () -> bool
        return self._dirty

    # write the index back to the disk image it came from
    # unused
    def update(self):
        # type: () -> None
        if self.isDirty():
            self._wvd.catalog[self._platter].setIndexEntry(self._platter, self._indexNum, self._record)
            self._dirty = False

############################## disk catalog ##############################
# this returns information about the information in the catalog of a
# particular platter of a disk.  it is best for a client to access it
# through the wvd.catalog member, so that the wvd can make sure 'catalog'
# is always up to date, even when the disk data is being modified.

# pylint: disable=useless-object-inheritance
class Catalog(object):
    # associate the disk and catalog
    def __init__(self, wvd, p):
        # type: (WangVirtualDisk, int) -> None
        assert p < wvd.numPlatters()
        self._wvd = wvd
        self._platter = p

    # return True if the given platter of the disk appears to have a catalog
    def hasCatalog(self):
        # type: () -> bool
        idxType = self.getIndexType()
        # TODO: should we perform some kind of catalog sanity check?
        #       that would be heavyweight and this function might be
        #       called a lot, so we'd want to cache that state.
        return idxType <= 2

    # return a list of all the catalog indices
    def catalogIndices(self):
        # type: () -> List[int]
        # -1 because the first catalog sector contains only 15 entries
        # as the first slot contains the disk paramter block
        return list(range(16*self.numIndexSectors()-1))

    # given a list of (str) filenames or wildcards and a disk, return
    # a list of all filenames on disk matching the wildcards
    def expandWildcards(self, wcList):
        # type: (List[str]) -> List[WvdFilename]
        assert self.hasCatalog()

        # get a list of all files on the disk
        #
        # note that if an entry is somehow hashed into the wrong bucket,
        # this routine will pick up the file, but the file may not be
        # "visible" when accessed normally.  That is, if a file entry
        # is in the wrong catalog sector, when this program, or a real 2200,
        # tries to access this misplaced file, it won't find it.
        allFiles = []
        for n in self.catalogIndices():
            indexEntry = self.getIndexEntryByNumber(n)
            if indexEntry:
                if indexEntry.getIndexState() in ['valid', 'scratched']:
                    allFiles.append(indexEntry.getFilename().get())

        # check each entry in the wildcard list for an exact match,
        # then if that fails, see if it is a wildcard and then
        # perform a pattern match
        nameList = []   # type: List[bytearray]
        for pat in wcList:
            if len(pat) > 8:
                print('Filename "%s" is more than 8 characters; ignoring' % pat)
                continue
            fullname = padName(bytearray(pat, 'ascii'))
            # try exact match first
            if (fullname in allFiles) and (fullname not in nameList):
                nameList.append(fullname)
                continue
            # if it is a wildcard, expand it to re form
            if ('*' in pat) or ('?' in pat):
                rePat = re.sub(r'\*', r'.*', pat)
                rePat = re.sub(r'\?', r'.', rePat)
                rePat += r' *$' # allow trailing spaces
                rePat_bytes = bytes(bytearray(rePat, 'ascii'))   # bytearray() is not hashable, and python2 barfs on 'ascii': apparently bytes() is an alias for str()
                # loop over all filenames and find any that match
                #matched = False
                for f in allFiles:
                    if re.match(rePat_bytes, f):
                        #matched = True
                        if f not in nameList:
                            nameList.append(f)
                #if not matched:
                #    print('''Pattern "%s" doesn't match any filenames on disk; ignoring''' % pat)
            #else:
            #    print('''Filename "%s" doesn't match any filenames on disk; ignoring''' % pat)

        return [WvdFilename(f) for f in nameList]

    # return a list containing all filenames on disk
    def allFilenames(self):
        # type: () -> List[WvdFilename]
        assert self.hasCatalog()
        return self.expandWildcards(['*'])

    # there are three types of indices
    def getIndexType(self):
        # type: () -> int
        secData = self._wvd.getRawSector(self._platter, 0)
        idxType = secData[0] & 0x7F  # sometimes MSB is set -- what is this about?
        #if idxType > 2:
        #    print("Unknown disk index type type 0x%02X; perhaps it is raw sectors?" % idxType)
        return idxType

    def setIndexType(self, newType):
        # type: (int) -> None
        if newType < 0 or newType > 2:
            print("Attempting to set unknown disk index type", newType)
            raise ProgramError
        secData = self._wvd.getRawSector(self._platter, 0)
        secData = bytearray([newType]) + secData[1:256]
        self._wvd.setRawSector(self._platter, 0, secData)

    # the first N sectors hold index information.  return N.
    def numIndexSectors(self):
        # type: () -> int
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return bytes2toInt(secData[1:3])
        return secData[1]

    # return which sector is the last in use by the cataloged files
    def currentEnd(self):
        # type: () -> int
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return bytes3toInt(secData[3:6]) - 1
        # msb is set sometimes -- what does it mean?
        return (bytes2toInt(secData[2:4]) & self._wvd.getSectorMask()) - 1

    # return which sector is the last for holding cataloged files
    def endCatalogArea(self):
        # type: () -> int
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return bytes3toInt(secData[6:9]) - 1
        # msb is set sometimes -- what does it mean?
        return (bytes2toInt(secData[4:6]) & self._wvd.getSectorMask()) - 1

    # return an index object for the n-th catalog slot,
    # or the index object of the names file.
    # return None if the index is too large.
    def getIndexEntry(self, n):
        # type: (Union[int,WvdFilename]) -> WvdIndex
        assert self.hasCatalog()
        if isinstance(n, WvdFilename):
            rv = self.getIndexEntryByName(n)
            if rv is not None:
                return rv
        if isinstance(n, int):
            rv = self.getIndexEntryByNumber(n)
            if rv is not None:
                return rv
        raise ProgramError

    # return an index object for the n-th catalog slot.
    # numbering starts at 0
    # return None if the index is too large.
    # the first slot of sector 0 is disk metadata, which is why (n+1) is used.
    def getIndexEntryByNumber(self, n):
        # type: (int) -> Optional[WvdIndex]
        sec  = (n+1) // 16  # which index sector
        slot = (n+1) %  16  # which slot of that sector
        if sec > self.numIndexSectors():
            return None
        secData = self._wvd.getRawSector(self._platter, sec)
        record = secData[16*slot : 16*(slot+1)]
        return WvdIndex(self._wvd, self._platter, n, record)

    # given a filename, return the corresponding slot struct,
    # or return None, if not found
    def getIndexEntryByName(self, name):
        # type: (WvdFilename) -> Optional[WvdIndex]
        if False:  # pylint: disable=using-constant-test
            # brute force: don't bother with hash approach
            for n in self.catalogIndices():
                idx = self.getIndexEntryByNumber(n)
                idxState = idx.getIndexState()
                if (idx.getFilename().get() == name.get()) and \
                    (idxState in ['valid', 'scratched']):
                    return idx
            return None # not found
        # use hash to speed up search; important for huge catalogs
        secs = self.numIndexSectors()            # sectors for catalog
        hsh = self.getFilenameHash(name)         # where to start search
        sdir = [-1, 1, 1][self.getIndexType()]   # search direction search
        for delta in range(secs):
            secNum = (hsh + delta*sdir) % secs
            # scan all entries of this sector; sector 0 has only 15 entries
            for slot in range(16 - (secNum == 0)):
                absSlot = 16*secNum + slot - (secNum != 0)
                thisSlot = self.getIndexEntry(absSlot)
                idxState = thisSlot.getIndexState()
                if idxState == 'empty':
                    return None  # not found
                if (thisSlot.getFilename().get() == name.get()) and \
                   (idxState in ['valid', 'scratched']):
                    return thisSlot
        return None  # searched all entries, and not found

    # raise an error if the index is too large
    def setIndexEntry(self, n, record):
        # type: (int, bytearray) -> None
        assert len(record) == 16
        sec  = (n+1) // 16  # which index sector
        slot = (n+1) % 16   # which slot of that sector
        if sec > self.numIndexSectors():
            raise ProgramError
        secData = self._wvd.getRawSector(self._platter, sec)
        secData = secData[0:16*slot] + record + secData[16*(slot+1):256]
        self._wvd.setRawSector(self._platter, sec, secData)

    def getFile(self, name):
        # type: (WvdFilename) -> Optional[CatalogFile]
        assert self.hasCatalog()
        if self.getIndexEntry(name) is None:
            return None
        return CatalogFile(self, name)

    # this converts the catalog index between new and old formats.
    # the data files aren't touched; the entries are simply moved around
    # to their new hash locations.
    def convertIndex(self, newStyle):
        # type: (bool) -> None
        assert self.hasCatalog()

        oldIndexType = self.getIndexType()
        if oldIndexType == 2:
            print("Can't deal with tri-byte catalogs yet")
            return
        if newStyle and (oldIndexType == 1):
            return  # not changing anything
        if not newStyle and (oldIndexType == 0):
            return  # not changing anything

        # build a list of all files on the disk, along with their attributes
        fileList = []
        for name in self.allFilenames():
            indexEntry = self.getIndexEntry(name)
            assert indexEntry is not None
            fileList.append(indexEntry)

        # wipe out the index
        for s in range(self.numIndexSectors()):
            if s == 0:
                self.setIndexType(newStyle)
                secData = self._wvd.getRawSector(self._platter, s)
                secData = secData[0:16] + bytearray(240)
            else:
                secData = bytearray(256)
            self._wvd.setRawSector(self._platter, s, secData)

        # step through all files and add them back in the right place
        for entry in fileList:
            self.addIndexEntry(entry)

    # given an indexInfo struct (name, start, end, etc), add it to the catalog.
    # only the index is added, not the file it is pointing to.
    def addIndexEntry(self, entry):
        # type: (WvdIndex) -> None
        assert self.hasCatalog()

        if self.getIndexType() == 0:
            sdir = -1  # old index overflows to previous bucket
        else:
            sdir =  1  # new index overflows to next bucket

        # get which sector to try first
        hsh = self.getFilenameHash(entry.getFilename())

        # try to insert it into all sectors in the index until it fits in one
        for delta in range(self.numIndexSectors()):
            secNum = (hsh + delta*sdir) % self.numIndexSectors()
            # print("Trying to put %s into sector %d" % (entry.getFilename(), secNum))
            # scan all entries of this sector; sector has only 15 entries
            for slot in range(16 - (secNum == 0)):
                absSlot = 16*secNum + slot - (secNum != 0)
                thisSlot = self.getIndexEntry(absSlot)
                if thisSlot.getIndexState() == 'empty':
                    # print("    fit into slot %d" % slot)
                    self.setIndexEntry(absSlot, entry.asBytes())
                    return

        print("Error: couldn't find a slot to fit file %s" % entry.getFilename())
        sys.exit()

    # return which sector the filename hashes to for the given disk
    # this is where to start looking; if the bucket is full, the
    # adjacent sectors may need to be searched too
    def getFilenameHash(self, filename):
        # type: (WvdFilename) -> int
        if self.getIndexType() == 0:
            return self.oldIndexHash(filename)
        return self.newIndexHash(filename)

    # this code is based on code from ISS release 5.1, program "ISS.229S"
    @staticmethod
    def oldIndexHash(filename):
        # type: (WvdFilename) -> int
        hash_val = 0x00
        for c in filename.get():
            hash_val ^= c
        hash_val *= 3
        hash_val = (hash_val//256) + (hash_val % 256)   # fold over carries
        return hash_val

    # this code is based on information from the OS 2.5 release notes
    @staticmethod
    def newIndexHash(filename):
        # type: (WvdFilename) -> int
        rawbytes = filename.get()
        hash_val = 0x00
        for i in range(8):
            c = rawbytes[i]
            if i % 2 == 0:
                hash_val += 16*(c & 0xf) + (c >> 4)  # swap nibbles
            else:
                hash_val += c
        return hash_val % 256

    # return True if the given catalog index bucket is full for sector 'secnum'
    # it is a bit fiddly because sector 0 has indexes 0..14, sec 1 has 15..30
    def isIndexBucketFull(self, secnum):
        # type: (int) -> bool
        if secnum == 0:
            first, last = 0, 14
        else:
            first, last = (16*secnum-1), (16*secnum + 14)
        for n in range(first, last+1):
            indexEntry = self.getIndexEntryByNumber(n)
            if (indexEntry is not None) and \
               (indexEntry.getIndexState() == 'empty'):
                return False
        return True

############################ CatalogFile Object ############################
# these objects return status about a file in a catalog.
# if the disk state is changed, the object may become stale.

# pylint: disable=useless-object-inheritance
class CatalogFile(object):
    # given a filename, return a CatalogFile object
    def __init__(self, cat, filename):
        # type: (Catalog, WvdFilename) -> None
        self._name    = filename
        self._cat     = cat
        self._wvd     = cat._wvd      # pylint: disable=protected-access; # this grubbing is pretty ugly
        self._platter = cat._platter  # pylint: disable=protected-access; # this grubbing is pretty ugly
        self._index   = cat.getIndexEntry(filename)
        self._valid   = (self._index is not None)

    def getFilename(self):
        # type: () -> WvdFilename
        return self._name
    def getType(self):
        # type: () -> str
        return self._index.getFileType()
    def programSaveMode(self):
        # type: () -> str
        return self._index.programSaveMode()
    def getStatus(self):
        # type: () -> str
        return self._index.getFileStatus()
    def getStart(self):
        # type: () -> int
        return self._index.getFileStart()
    def getEnd(self):
        # type: () -> int
        return self._index.getFileEnd()
    def getExtent(self):
        # type: () -> Tuple[int,int]
        return self._index.getFileExtent()
    def getUsed(self):
        # type: () -> int
        return self._index.getFileUsedSectors()
    def getFree(self):
        # type: () -> int
        return self._index.getFileFreeSectors()
    def fileExtentIsPlausible(self):
        # type: () -> bool
        return self._index.fileExtentIsPlausible()
    def fileControlRecordIsPlausible(self):
        # type: () -> bool
        return self._index.fileControlRecordIsPlausible()

    # set or clear the protection mode on the file.
    # the file must be a program file.
    def setProtection(self, protect):
        # type: (bool) -> None
        (start, end) = self.getExtent()
        # we don't touch the final allocated block
        # we only have to touch the used blocks, not all the allocated blocks
        for s in range(start, end):
            secData = self._wvd.getRawSector(self._platter, s)
            if protect:
                # set protection bit
                secData = bytearray([secData[0] | 0x10]) + secData[1:256]
            else:
                # clear protection bit
                secData = bytearray([secData[0] & 0xEF]) + secData[1:256]
            self._wvd.setRawSector(self._platter, s, secData)

    # extract sectors of named file
    # given a file name (program or data), return a list containing the sector
    # data, but only of the used sectors, not all the allocated sectors.
    # return None if the filename isn't found, or if the file extent
    # information is bogus (in which case we report it)
    def getSectors(self):
        # type: () -> Optional[List[bytearray]]
        if self._index.getIndexState() != 'valid':
            return None
        typ = self.getType()
        if typ == '?':
            return None
        start = self.getStart()
        end   = self.getEnd()
        if end < start:
            print("%s: bad file extent information" % self._name)
            return None
        if end >= self._wvd.numSectors():
            print("%s: bad file extent information" % self._name)
            return None
        used = self.getUsed()
        sectors = []
        # TODO: really, assert? rather than crashing, report an error
        assert self.fileControlRecordIsPlausible()
        # used-1 because one sector is used for holding the used sector count
        for sec in range(start, start+used-1):
            secData = self._wvd.getRawSector(self._platter, sec)
            sectors.append(secData)
        return sectors

    # rewrite sectors of a file
    def setSectors(self, blocks):
        # type: (List[bytearray]) -> None
        assert self._index.getIndexState() == 'valid'
        assert self.getType() == 'P'
        start = self.getStart()
        end   = self.getEnd()
        if end < start:
            print("%s: bad file extent information" % self._name)
            return
        if end >= self._wvd.numSectors():
            print("%s: bad file extent information" % self._name)
            return
        # TODO: really, assert? rather than crashing, report an error
        assert self.fileControlRecordIsPlausible()
        used = self.getUsed()
        # used-1 because one sector is used for holding the used sector count
        for sec in range(start, start+used-1):
            if sec-start > len(blocks): break
            self._wvd.setRawSector(self._platter, sec, blocks[sec-start])
        return

############################## helpers ##############################

def bytes3toInt(data):
    # type: (bytearray) -> int
    assert len(data) == 3
    return 65536*data[0] + 256*data[1] + data[2]

def bytes2toInt(data):
    # type: (bytearray) -> int
    assert len(data) == 2
    return 256*data[0] + data[1]

def intToBytes2(val):
    # type: (int) -> bytearray
    return bytearray([val // 256, val % 256])

def intToBytes3(val):
    # type: (int) -> bytearray
    return bytearray([val // 65536, (val // 256) % 256, val % 256])

# return True if the two nibbles are bcd
def valid_bcd_byte(ch):
    # type: (int) -> bool
    low  = ch & 0x0F
    high = ch & 0xF0
    return (low <= 0x09) and (high <= 0x90)

def padName(name):
    # type: (bytearray) -> bytearray
    '''Takes a bytearray filename string and pads it to 8 spaces.'''
    assert len(name) < 9
    name += bytearray([ord(' ')]) * (8 - len(name))
    return name

########################################################################
# given a header record, return the filename it contains.
# NB: disk headers pad names shorter than 8 characters with 0x20
#     tape headers pad names shorter than 8 characters with 0xFF
def headerName(sector):
    # type: (bytearray) -> str
    rawname = sector[1:9]
    return sanitize_filename(rawname)

# NB: disk headers pad names shorter than 8 characters with 0x20
#     tape headers pad names shorter than 8 characters with 0xFF
def sanitize_filename(rawname):
    # type: (Any) -> str
    if isinstance(rawname, str):
        byts = bytearray(rawname.encode('latin-1'))
    else:
        byts = bytearray(rawname)
    while byts and (byts[-1] in (0xff, ord(' '))):
        byts = byts[:-1]
    name = [chr(byt) if (32 <= byt < 128) else ("\\%02X" % byt) for byt in byts]
    return ''.join(name)

########################################################################
# unscramble a single sector of data. if the incoming data isn't scrambled,
# this routine will mess it up. the incoming data is unaffected and the
# function returns a new block.
#
# the algorithm is extracted from the DCRYPT routine of Wang MVP OS 3.5.
# this function doesn't implement the tweak for "wrap mode" files.

def unscramble_one_sector(block):
    # type: (bytearray) -> bytearray

    assert len(block) == 256
    sector = bytearray(block)  # make a copy

    byte0 = sector[0]
    byte1 = sector[1]

    # The pass 1 key is in byte 5, 25, 45, or 65
    # depending on the two middle coded bits of the 2nd byte
    offset = (byte1 & 0x60) + 5
    key = sector[offset]

    sector[0] = (byte0 & 0xF0) | ((byte1 & 0xF0) >> 4)       # new control byte
    sector[1] = key                                          # save key
    sector[offset] = ((byte0 & 0x0F) << 4) | (byte1 & 0x0F)  # replace key byte

    ### DECRYPT PASS 2

    offset = 0x100-0x22     # point to pass 2 seed
    endidx = offset         # terminate the loop at the seed location
    pair = (sector[offset] << 8) | sector[offset + 1]  # pass 2 seed

    # the starting location for loop
    offset = (offset + 26)   # 248 = 0xF8

    while offset != endidx:
        # rotate previous pair left one bit, then swap the middle nibbles
        seed = ((pair << 1) | (pair >> 15)) & 0xFFFF
        nibs = (seed & 0xF00F) | ((seed & 0x00F0) << 4) | ((seed & 0x0F00) >> 4)

        # fetch next pair of bytes
        pair = (sector[offset] << 8) | sector[offset + 1]

        # transform bytes and write them out
        tmp = (pair + (0xffff ^ nibs))   # or tmp = (pair - nibs - 1)
        sector[offset]     = (tmp >> 8) & 0xff
        sector[offset + 1] = (tmp     ) & 0xff

        # increment and wrap, skipping bytes 0 & 1
        offset += 26
        if offset >= 256:
            offset = (offset + 2) & 0xff

    ### DECRYPT PASS 1

    offset = 0x22         # starting offset for loop
    endidx = 0            # stop when we get here
    pair   = sector[1]    # pass 1 seed

    while offset != endidx:
        # rotate previous pair left one bit, then swap the middle nibbles
        seed = ((pair << 1) | (pair >> 15)) & 0xFFFF
        nibs = (seed & 0xF00F) | ((seed & 0x00F0) << 4) | ((seed & 0x0F00) >> 4)

        # fetch next pair of bytes
        pair = (sector[offset] << 8) | sector[offset + 1]

        # transform bytes and write them out
        tmp = (pair + (0xffff ^ nibs))   # or tmp = (pair - nibs - 1)
        sector[offset]     = (tmp >> 8) & 0xff
        sector[offset + 1] = (tmp     ) & 0xff

        # increment and wrap
        offset = (offset + 34) & 0xFF

    # reconstruct first two bytes
    byte0 = sector[0]
    sector[0] = byte0 & 0xF0            # high half only
    sector[1] = 0xFE | (byte0 & 0x01)   # FE or FF

    return sector
