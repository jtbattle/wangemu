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
# Version: 1.6, 2018/08/24, JTB
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
        self.head_dat = []    # type: bytearray
        self.catalog = []     # one entry per platter

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
        self.sector = []  # type: List[bytearray]
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
# this is a very simple class which is exists mostly to give the mypy
# type checker something to do.  that is, the wvd filenames are passed
# around as bytearray()s of length 8. but other bytearrays are being
# passed around, so this allows us to tag which type we are dealing with.

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
    def programSaveMode(self):
        # type: () -> str
        if self.getFileType() != 'P':
            return 'unknown'
        if not self.fileExtentIsPlausible():
            return 'unknown'

        start = self.getFileStart()
        secData = self._wvd.getRawSector(self._platter, start + 0)
        if (secData[0] & 0xF0) == 0x40:
            return 'normal'
        if (secData[0] & 0xF0) != 0x50:
            return 'unknown'

        # either it is protected or scrambled.  for non-scrambled programs, the
        # first byte must be either 0x20 (space) or 0xFF (line number token).
        # scrambled programs appear to always use control bytes of the form
        # 3x 1y.  however, I'll use the logic which MVP OS 3.5 uses
        # (see BPMVP42D).
        secData = self._wvd.getRawSector(self._platter, start + 1)
        if (secData[0] & 0xC0) != 0x00:
            return 'unknown'  # header or data block flags
        if (secData[0] & 0x10) != 0x10:
            # this says unprotected, but the header block said the file was
            return 'unknown'
        if (secData[1] & 0x80) == 0x80:
            return 'protected'
        return 'scrambled'

    # this combines the index state with the file type
    def getFileStatus(self):
        # type: () -> Optional[str]
        idxState = self.getIndexState()
        if idxState == 'empty':
            return  None # this index is unused
        if idxState == 'invalid':
            return  None # this index was used, but is now invalid
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
            valid_prefix = (0xA0,)
        else:
            valid_prefix = (0x20, 0xA0)  # be generous and allow either
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
            return self.getIndexEntryByName(n)
        if isinstance(n, int):
            return self.getIndexEntryByNumber(n)
        raise ProgramError

    # return an index object for the n-th catalog slot.
    # numbering starts at 0
    # return None if the index is too large.
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
# X2: bytearray
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

def padName(name):
    # type: (bytearray) -> bytearray
    '''Takes a bytearray filename string and pads it to 8 spaces.'''
    assert len(name) < 9
    name += bytearray([ord(' ')]) * (8 - len(name))
    return name

############################ checkPlatter ############################
# ad-hoc check of disk integrity
# if report = 1, report warnings on stdout
# return False if things are OK; return True if errors

def checkPlatter(wvd, p, report):
    # type: (WangVirtualDisk, int, bool) -> bool
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
def checkCatalogIndexes(wvd, p, report):
    # type: (WangVirtualDisk, int, bool) -> bool
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
    extent_list = []  # type: List[Dict[str, Union[str,int]]]

    # keep track of the valid and scratched file names
    live_names = {}  # type: Dict[str, bool]

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
                                    % (secnum, slot, indexEntry.geFileTypeNumber()))
                found_problems = True
                continue

            # -- (3c) check that the filename consists of reasonable characters
            suspect_name = False
            for ch in fname.get():
                suspect_name = suspect_name or ch < 0x20 or ch >= 0x80
            if suspect_name:
                if report: print("(warning) suspect fileame '%s'" % str_fname)

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
# X3: str
            hsh = platter.getFilenameHash(fname) % platter.numIndexSectors()
            if hsh != secnum:
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
def checkCatalog(wvd, p, report):
    # type: (WangVirtualDisk, int, bool) -> bool

    platter = wvd.catalog[p]
    failures = False

    # step through each catalog entry
    for name in platter.allFilenames():
        indexEntry = platter.getIndexEntry(name)
        if indexEntry:
            bad = checkFile(wvd, p, name, report)
            if bad:
                failures = True

    return failures

# check the structure of an individual file.
# returns True if there are errors.
# This is used both by the "check" command, and it gets called
# by the "catalog" command to indicate after each entry whether
# it appears to be valid.
# pylint: disable=too-many-return-statements
def checkFile(wvd, p, cat_fname, report):
    # type: (WangVirtualDisk, int, WvdFilename, bool) -> bool

    indexEntry = wvd.catalog[p].getIndexEntry(cat_fname)
    if indexEntry is None:
        print("Platter %d: %s: not found" % (p, cat_fname))
        return True

    str_fname = cat_fname.asStr()

    if not indexEntry.fileControlRecordIsPlausible():
        if report: print("%s doesn't have a plausible file control record" % str_fname)
        return True

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
        return True

    # do program or data file specific checks
    fileType = indexEntry.getFileType()
    if fileType == 'P':
        x = checkProgramFile(wvd, p, cat_fname, indexEntry, report)
        if x: return x
    elif fileType == 'D':
        x = checkDataFile(wvd, p, cat_fname, indexEntry, report)
        if x: return x
    else:
        if report: print("%s has unknown file type" % str_fname)
        return True

    return False

# inspect the sectors of a program file to make sure they are consistent
# and look like a program.  return True on error, False if no errors.
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
def checkProgramFile(wvd, p, cat_fname, indexEntry, report):
    # type: (WangVirtualDisk, int, WvdFilename, WvdIndex, bool) -> bool
    # TODO: really, assert? rather than crashing, report an error
    assert indexEntry.fileControlRecordIsPlausible()  # because we use "used"
    start = indexEntry.getFileStart()
    used  = indexEntry.getFileUsedSectors()

    str_fname = cat_fname.asStr()
    protected = 0x00  # until proven otherwise

    # the used value includes the last sector of the allocated file,
    # so it shouldn't be counted (the -1 below)
    for s in range(used-1):
        sec = start + s
        secData = wvd.getRawSector(p, sec)

        if (s > 0) and protected:
            # if it is scrambled and not just SAVE"P",
            # unscramble it so we can verify the contents
            if (secData[1] & 0x80) == 0x00:
                secData = unscramble_one_sector(secData)

        if s == 0:
            # first sector
            first_sector = True
            terminator_byte = 0xFD
            protected = (secData[0] & 0x10)  # 0x10 if "SAVE P" file
            expected = 0x40 | protected
        elif s < (used-2):
            # middle sector
            first_sector = False
            terminator_byte = 0xFD
            expected = 0x00 | protected
        else:
            # last sector
            first_sector = False
            terminator_byte = 0xFE
            expected = 0x20 | protected

        # the first byte of each sector identifies the type of data it contains
        if (secData[0] & 0xf0) != expected:
            if report: print("%s sector %d has control byte of 0x%02x; 0x%02x expected" % \
                (str_fname, sec, secData[0] & 0xf0, expected))
            return True

        if first_sector:
            # header record
            if checkProgramHeaderRecord(cat_fname, sec, secData, report):
                return True
        else:
            # middle or trailer program record; these contain the actual program
            if checkProgramBodyRecord(cat_fname, sec, secData, terminator_byte, report):
                return True
            # TODO: check that line numbers should only increase when advancing
            #       through the file

    return False

# check the first record of a program file.
# return True on error, False if no errors.
def checkProgramHeaderRecord(cat_fname, sec, secData, report):
    # type: (WvdFilename, int, bytearray, bool) -> bool
    # check the filename embedded in the first sector matches the catalog filename
    name = secData[1:9]
    str_cat_fname = cat_fname.asStr()
    str_name = WvdFilename(name).asStr()
    if str_name != str_cat_fname:
        # this is just a warning, not an error
        if report: print("(warning) %s header block contains a different file name, '%s'" \
                            % (str_cat_fname, str_name))
    # the next byte after the filename should be 0xFD
    if secData[9] != 0xFD:
        if report: print("%s's header record (sector %d) must have a 0xFD terminator in the 10th byte" \
                            % (str_cat_fname, sec))
        return True
    return False

# check the body record of a program file.
# return True on error, False if no errors.
# pylint: disable=too-many-return-statements, too-many-branches
def checkProgramBodyRecord(cat_fname, sec, secData, terminator_byte, report):
    # type: (WvdFilename, int, bytearray, int, bool) -> bool
    cat_str_fname = cat_fname.asStr()

    # each record must have a terminator byte (0xFD or 0xFE)
    pos = secData.find(terminator_byte)
    if pos < 0:
        if report: print("%s (sector %d) does not contain a 0x%02X terminator byte" \
                            % (cat_str_fname, sec, terminator_byte))
        return True

    # the terminator shouldn't be too soon -- it must follow the SOB,
    # 0xFF <2 line number bytes) 0x0D 0x00 0x00.
    if pos < 7:
        if report: print("%s (sector %d) contains a 0x%02X terminator byte, but it is too soon" \
                            % (cat_str_fname, sec, terminator_byte))
        return True

    # now scan the active part of the record.  make sure it starts
    # with a line number, that lines are terminated with 0D 00 00,
    # that non-final line endings are followed by another line number,
    # and that the final line ending is followed by the terminator
    (expect_line_num, seek_cr) = (0, 1)
    state = expect_line_num
    cp = 1
    while cp < pos:
        #print("Inspecting offset=%d, state=%d" % (cp, state))
        if state == expect_line_num:
            # look for line number token
            if secData[cp] != 0xFF:
                if report: print("%s (sector %d) didn't find a line number token, 0xFF, where expected; 0x%02X found" \
                                    % (cat_str_fname, sec, secData[cp]))
                return True
            # the next two bytes contain the line number in bcd
            if (not valid_bcd_byte(secData[cp+1])) or \
               (not valid_bcd_byte(secData[cp+2])):
                if report: print("%s (sector %d) should begin with a bcd line number, but found 0x%02X%02X" \
                                    % (cat_str_fname, sec, secData[cp+1], secData[cp+2]))
                return True
            # found a valid line number sequence
            cp = cp+3
            state = seek_cr
            continue

        elif state == seek_cr:
            # looking for the end of line
            if secData[cp] != 0x0D:
                cp = cp+1
                continue
            # found the end of line; make sure it is followed by 00 00
            if (secData[cp+1] != 0x00) or \
               (secData[cp+2] != 0x00):
                if report: print("%s (sector %d) contains an EOL (0x0D) which isn't followed by 0x0000; found 0x%02X%02X" \
                                    % (cat_str_fname, sec, secData[cp+1], secData[cp+2]))
                return True
            cp = cp+3
            state = expect_line_num
            continue
        # end while cp<pos

    # check that an end of line immediately precedes the termination byte
    if state == seek_cr:
        if report: print("%s (sector %d) terminated with a partial program line at offset %d" \
                            % (cat_str_fname, sec, cp))
        return True

    return False

# return True if the two nibbles are bcd
def valid_bcd_byte(ch):
    # type: (int) -> bool
    low  = ch & 0x0F
    high = ch & 0xF0
    return (low <= 0x09) and (high <= 0x90)

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
#  last sector of active file: 0xa0  (written by DATASAVE DC END)
#
#  last sector of allocated file: 0xa0 nnnn (nnnn = # of sectors in use)
#
# TODO: it shouldn't be too hard to parse the data fields to validate the contents
# pylint: disable=too-many-return-statements, too-many-branches
def checkDataFile(wvd, p, cat_fname, indexEntry, report):
    # type: (WangVirtualDisk, int, WvdFilename, WvdIndex, bool) -> bool
    # TODO: really, assert? rather than crashing, report an error
    assert indexEntry.fileControlRecordIsPlausible()
    start     = indexEntry.getFileStart()
    used      = indexEntry.getFileUsedSectors()

    str_fname = cat_fname.asStr()

    # the used value includes the last sector of the allocated file,
    # so it shouldn't be counted (the -1 below)
    state = 0
    for s in range(0, used-1):
        secData = wvd.getRawSector(p, start + s)

        if (s == 0) and (secData[0] == 0x6a):
            # print("%s is some kind of raw binary file; assuming OK" % str_fname)
            return False

        if state == 0:
            # looking for either end of data or start of record
            if (secData[0] == 0xa0) or (secData[0] == 0xa1):
                state = 2
            elif (secData[0] == 0x81) and (secData[1] == 0x01):
                # this is a single sector record
                state = 0
            elif (secData[0] == 0x82) and (secData[1] == 0x01):
                # this starts a multisector record; expect another sector
                expect = 0x02
                state = 1
            else:
                if report: print("%s, sector %d has bad control bytes 0x%02x%02x" % \
                    (str_fname, start+s, secData[0], secData[1]))
                return True

        elif state == 1:
            # expect to see next (and possibly last) sector of record
            if secData[1] != expect:
                if report: print("%s, sector %d expected to have sequence number 0x%02x, got 0x%02x" % \
                    (str_fname, start+s, expect, secData[1]))
                return True
            if secData[0] == 0x82:
                # expect more sectors in this logical record
                expect += 1
                state = 1  # that is, no change
            elif secData[0] == 0x81:
                # this is the last one of this record
                state = 0  # expect either end of data or new record
            else:
                if report: print("%s, sector %d has bad control bytes 0x%02x%02x" % \
                    (str_fname, start+s, secData[0], secData[1]))
                return True

        elif state == 2:
            # we've seen the end of data marker, so there shouldn't be more
            if report: print("%s, end-of-data marker seen before final used sector" % str_fname)
            return True

        # the control bytes are OK; now check the contents
        if checkDataRecord(cat_fname, start+s, secData, report):
            return True

    return False


# check if the record contents, ignoring the control bytes, appear to
# contain a valid data record.  specifically, check that
#    1) it contains a sequence of valid SOV bytes,
#    2) the data associated with the SOV byte is consistent
#    3) there is a terminator byte immediately after some value
# returns True on error, False if it is OK
# pylint: disable=too-many-branches
def checkDataRecord(cat_fname, secnum, secData, report):
    # type: (WvdFilename, int, bytearray, bool) -> bool
    str_fname = cat_fname.asStr()

    cp = 2  # skip control bytes
    while (cp < 255) and (secData[cp] < 0xFC):
        c = secData[cp]

        if c == 0x08:  # numeric
            if cp+1+8 > 254:  # 254 because we still need a terminator
                if report: print("%s, sector %d, SOV indicates a number, but there aren't enough bytes left in the sector" \
                                    % (str_fname, secnum))
                return True
            fp = secData[cp+1 : cp+9]
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
                if report: print("%s, sector %d, fp value has non-BCD digits" \
                                    % (str_fname, secnum))
                return True
            # TODO: check for denorms, negative zero, unexpected flag bits?
            #expSign  = (fp[0] & 0x80) != 0
            #mantSign = (fp[0] & 0x10) != 0
            cp += 9

        elif c < 128:
            if report: print("%s, sector %d, unexpected SOV value of 0x%02X" % (str_fname, secnum, c))
            return True

        else:
            strlen = c-128
            if cp+1+strlen > 254:  # 254 because we still need a terminator
                if report: print("%s, sector %d; SOV indicates string length that doesn't fit in sector" % (str_fname, secnum))
                return True
            cp += 1 + strlen

    # check that we ended with a terminator
    if secData[cp] != 0xFD:
        if report: print("%s, sector %d doesn't contain 0xFD terminator" % (str_fname, secnum))
        return True

    return False

########################################################################
# given a stream of sectors, try to identify coherent files, or even
# fragments of files.   return a list of hashes containing these entries:
#    ( "first_record" : int,
#       "last_record" : int,
#       "file_type" : str,
#       "filename" : str  )
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

# pylint: disable=too-many-locals, too-many-branches, too-many-statements
def guessFiles(blocks, abs_sector):
    # type: (List[bytearray], int) -> List[Dict[str, Any]]

    func_dbg = False

    files = [] # holds the list of file bits

    relsector = -1    # sector number, relative to first block

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
    fileinfo = {}   # type: Dict[str, Any]

    # logical records can span multiple physical records.  we track the sequence
    # number across records to make sure things are ocnsistent
    logical_data_record = 0
    prev_data_phys_seq = -1

    for secData in blocks:

        relsector = relsector + 1
        secnum = relsector + abs_sector

        if func_dbg: print("# ============== sector %d ==============" % secnum)

        # break out the SOB flag bits
        data_file                = secData[0] & 0x80
        header_record            = secData[0] & 0x40
        trailer_record           = secData[0] & 0x20
        protected_file           = secData[0] & 0x10
        intermediate_phys_record = secData[0] & 0x02
        last_phys_record         = secData[0] & 0x01

        if func_dbg: print("# data=%d, header=%d, trailer=%d, prot=%d, last_phys=%d, middle_phys=%d" \
                            % (data_file, header_record, trailer_record, protected_file, last_phys_record, intermediate_phys_record))

        # these indicate if the sector contents, irrespective of the SOB byte,
        # look like a given type of sector
        dummyname = WvdFilename(bytearray(8))
        possible_pgm_hdr = not checkProgramHeaderRecord(dummyname, secnum, secData, False)
        possible_pgm_mid_body = not checkProgramBodyRecord(dummyname, secnum, secData, 0xFD, False)
        possible_pgm_end_body = not checkProgramBodyRecord(dummyname, secnum, secData, 0xFE, False)
        possible_data_record = not checkDataRecord(dummyname, secnum, secData, False)

        valid_pgm_hdr = not data_file and header_record and possible_pgm_hdr
        valid_pgm_mid = not data_file and not header_record and possible_pgm_mid_body
        valid_pgm_end = not data_file and not header_record and possible_pgm_end_body
        valid_data_start = data_file and \
                           (intermediate_phys_record or last_phys_record) and \
                           possible_data_record and \
                           (secData[1] == 0x01)
        valid_data_mid = data_file and \
                         (intermediate_phys_record or last_phys_record) and \
                         possible_data_record and \
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
                if func_dbg: print("unknown: -> pgm_hdr")
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
