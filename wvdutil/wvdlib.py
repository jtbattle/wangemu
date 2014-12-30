# Program: wvdlib.py, used by wvdutil.py
# Purpose: encapsulates some useful routines for manipulating .wvd files
# Version: 1.2, 2008/08/18
# Author:  Jim Battle

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

import sys
import re

########################################################################
# simple exception class

class ProgramError(Exception):
    def __init__(self, value):
        self.value = value
    def __str__(self):
        return repr(self.value)

########################################################################
# this represents top-level information about a wang virtual disk

class WangVirtualDisk:

    # creation always produces an invalid disk.
    # currently the only way to get a valid disk is from reading one;
    # one can't be constructed from scratch.
    def __init__(self):
        self._valid = 0   # the rest of self.members are meaningless
        self._dirty = 0   # contents modified from on-disk version
        self._filename = None
        self.catalog = [] # one entry per platter

    # read a virtual disk disk from a file into memory
    def read(self,filename):
        fh = open(filename, 'rb')
        self._filename = filename
        self.head_dat = fh.read(256)  # get wvd header
        if self.head_dat[0:5] != 'WANG\0':
            print "File is not a .wvd type"
            raise IOError
        self.sector = []
        for i in xrange(self.numPlatters() * self.numSectors()):
            self.sector.append(fh.read(256))
            i = i+0  # make pychecker happy, otherwise 'i' is reported unused
        fh.close()
        for p in xrange(self.numPlatters()):
            self.catalog.append( Catalog(self, p) )
        self._valid = 1
        self._dirty = 0

    # dump the current disk image to the specified file
    def write(self,filename):
        fh = open(filename, 'wb')
        fh.write(self.head_dat)
        for i in xrange(self.numSectors()):
            fh.write(self.sector[i])
        fh.close()
        self._dirty = 0

    # get metadata about the virtual disk
    def getFilename(self):     return self._filename
    def getWriteFormat(self):  return ord(self.head_dat[5])
    def getReadFormat(self):   return ord(self.head_dat[6])
    def getWriteProtect(self): return ord(self.head_dat[7])
    def numSectors(self):      return ord(self.head_dat[8]) + ord(self.head_dat[9])*256
    def mediaType(self):       return ord(self.head_dat[10])
    def numPlatters(self):     return ord(self.head_dat[11]) + 1
    def label(self):           return (self.head_dat[16:256].split('\0'))[0]

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
        if (self.numPlatters() > 1) or (self.numSectors() > 32768):
            return 0xFFFF
        else:
            return 0x7FFF

    # set metadata
    # unused
    def setWriteProtect(self, prot):
        # self.head_dat[7] = chr(prot != 0)
        self.head_dat = self.head_dat[0:7] + chr(prot != 0) + self.head_dat[8:256]
        self._dirty = 1

    # unused
    def setMediaType(self,type):
        assert (type == 0) or (type == 1) or (type == 2)
        self.head_dat[10] = chr(type)
        self._dirty = 1

    # unused
    def setLabel(self,label):
        label = label[0:256-16]              # chop off excess
        label += ' ' * (256-16 - len(label)) # pad if required
        self.head_dat[16:256] = label
        self._dirty = 1

    def checkSectorAddress(self,p,n):
        if p >= self.numPlatters():
            print "Error: attempted to access invalid platter #",p
            print self.numPlatters()
            raise ProgramError
        if n >= self.numSectors():
            print "Error: attempted to access invalid sector #",n
            print self.numSectors()
            raise ProgramError

    def getRawSector(self,p,n):
        self.checkSectorAddress(p,n)
        nn = p*self.numSectors() + n
        return self.sector[nn]

    # replace a sector
    def setRawSector(self,p,n,data):
        assert len(data) == 256
        self.checkSectorAddress(p,n)
        nn = p*self.numSectors() + n
        self.sector[nn] = data
        self._dirty = 1

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

class WvdIndex(object):
    def __init__(self, wvd, p, n, record):
        assert len(record) == 16
        assert p < wvd.numPlatters()
        self._wvd      = wvd     # disk the index belongs to
        self._platter  = p       # platter of disk
        self._indexNum = n       # location in index it is from
        self._record   = record  # the index itself
        self._dirty    = 0       # not modified

    # return the record as a 16 byte binary string
    def asString(self):
        return self._record

    def getFilename(self):
        return self._record[8:16]

    # unused
    def setFilename(self,name):
        name = padName(name)
        self._record = self._record[0:8] + name
        self._dirty = 1

    def getIndexState(self):
        val = ord(self._record[0])
        if val == 0x00: return "empty"
        if val == 0x10: return "valid"
        if val == 0x11: return "scratched"
        if val != 0x21:
            print "Unknown index state of 0x%02x" % val
        return "invalid"

    def setIndexState(self,type):
        if   type == "empty":     val = 0x00
        elif type == "valid":     val = 0x10
        elif type == "invalid":   val = 0x21
        elif type == "scratched": val = 0x11
        else: assert 0>1, "Bad type in setIndexState"
        self._record = chr(val) + self._record[1:16]
        self._dirty = 1

    def getFileType(self):
        if self._record[1] == chr(0x80): return "P"
        if self._record[1] == chr(0x00): return "D"
        return "?"

    # unused
    def setFileType(self,type):
        if   type == "P": val = 0x80
        elif type == "D": val = 0x00
        else: assert 0>1, "Bad type in setFileType"
        self._record = self._record[0] + chr(val) + self._record[2:16]
        self._dirty = 1

    # this combines the index state with the file type
    def getFileStatus(self):
        idxState = self.getIndexState()
        if idxState == "empty":
            return  None # this index is unused
        if idxState == "invalid":
            return  None # this index was used, but is now invalid
        if  idxState == "scratched": state = 'S'
        else                       : state = ' '
        return state + self.getFileType()  # append P or D

    def getFileStart(self):
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            return str16ToInt(self._record[2:4]) & self._wvd.getSectorMask()
        else:
            return str24ToInt(self._record[2:5])

    def getFileEnd(self):
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            return str16ToInt(self._record[4:6]) & self._wvd.getSectorMask()
        else:
            return str24ToInt(self._record[5:8])

    def getFileExtent(self):
        return (self.getFileStart(), self.getFileEnd())

    # unused
    def setFileExtent(self,start,end):
        assert start >= 0
        assert start < end
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            assert end < (1 << 16)
            self._record = self._record[0:2] + intToStr16(start) \
                         + intToStr16(end) + self._record[6:16]
        else:
            assert end < (1 << 24)
            self._record = self._record[0:2] + intToStr24(start) \
                         + intToStr24(end) + self._record[8:16]
        self._dirty = 1

    # the last record of a file indicates how many sectors are in use
    def getFileUsedSectors(self):
        (start,end) = self.getFileExtent()
        sectorData = self._wvd.getRawSector(self._platter, end)
        if self._wvd.catalog[self._platter].getIndexType() < 2:
            used = str16ToInt(sectorData[1:3])
        else:
            used = str24ToInt(sectorData[1:4])
        return used

    def getFileFreeSectors(self):
        (start,end) = self.getFileExtent()
        used = self.getFileUsedSectors()
        free = (end - start+1) - used
        return free

    def isDirty(self): return self._dirty

    # write the index back to the disk image it came from
    # unused
    def update(self):
        if self.isDirty():
            self._wvd.catalog[self._platter].setIndexEntry(self._platter, self._indexNum, self._record)
            self._dirty = 0

############################## disk catalog ##############################
# this returns information about the information in the catalog of a
# particular platter of a disk.  it is best for a client to access it
# through the wvd.catalog member, so that the wvd can make sure 'catalog'
# is always up to date, even when the disk data is being modified.

class Catalog(object):
    # associate the disk and catalog
    def __init__(self, wvd, p):
        assert p < wvd.numPlatters()
        self._wvd = wvd
        self._platter = p

    # return TRUE if the given platter of the disk appears to have a catalog
    def hasCatalog(self):
        idxType = self.getIndexType()
        # FIXME: should we perform some kind of catalog sanity check?
        #        that would be heavyweight and this function might be
        #        called a lot, so we'd want to cache that state.
        return (idxType <= 2)

    # given a list of filenames or wildcards and a disk, return
    # a list of all filenames on disk matching the wildcards
    def expandWildcards(self, wcList):

        assert self.hasCatalog()

        # get a list of all files on the disk
        allFiles = []
        for n in xrange(16*self._wvd.catalog[self._platter].numIndexSectors()-1):
            indexEntry = self.getIndexEntry(n)
            if indexEntry.getIndexState() in ["valid", "scratched"]:
                allFiles.append(indexEntry.getFilename())

        # check each entry in the wildcard list for an exact match,
        # then if that fails, see if it is a wildcard and then
        # perform a pattern match
        nameList = []
        for pat in wcList:
            if (len(pat) > 8):
                print '''Filename "%s" is more than 8 characters; ignoring''' % pat
                continue
            fullname = padName(pat)
            # try exact match first
            if (fullname in allFiles) and (fullname not in nameList):
                nameList.append(fullname)
                continue
            # if it is a wildcard, expand it to re form
            if ('*' in pat) or ('?' in pat):
                rePat = re.sub( r'\*', r'.*', pat)
                rePat = re.sub( r'\?', r'.', rePat)
                rePat = rePat + r' *$' # allow trailing spaces, and match all
                # loop over all filenames and find any that match
                matched = 0
                for f in allFiles:
                    if re.match(rePat, f):
                        matched = 1
                        if f not in nameList:
                            nameList.append(f)
                if not matched:
                    print '''Pattern "%s" doesn't match any filenames on disk; ignoring''' % pat
            else:
                print '''Filename "%s" doesn't match any filenames on disk; ignoring''' % pat

        return nameList

    # return a list containing all filenames on disk
    def allFilenames(self):
        assert self.hasCatalog()
        return self.expandWildcards( ['*'] )

    # there are three types of indices
    def getIndexType(self):
        secData = self._wvd.getRawSector(self._platter, 0)
        idxType = ord(secData[0]) & 0x7F  # sometimes MSB is set -- what is this about?
        #if idxType > 2:
        #    print "Unknown disk index type type 0x%02X; perhaps it is raw sectors?" % idxType
        return idxType

    def setIndexType(self, newType):
        if newType < 0 or newType > 2:
            print "Attempting to set unknown disk index type", newType
            raise ProgramError
        secData = self._wvd.getRawSector(self._platter, 0)
        secData = chr(newType) + secData[1:256]
        self._wvd.setRawSector(self._platter, 0, secData)
        return

    # the first N sectors hold index information.  return N.
    def numIndexSectors(self):
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return str16ToInt(secData[1:3])
        else:
            return ord(secData[1])

    # return which sector is the last in use by the cataloged files
    def currentEnd(self):
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return str24ToInt(secData[3:6]) - 1
        else:
            # msb is set sometimes -- what does it mean?
            return (str16ToInt(secData[2:4]) & self._wvd.getSectorMask()) - 1

    # return which sector is the last for holding cataloged files
    def endCatalogArea(self):
        secData = self._wvd.getRawSector(self._platter, 0)
        if self.getIndexType() == 2:
            return str24ToInt(secData[6:9]) - 1
        else:
            # msb is set sometimes -- what does it mean?
            return (str16ToInt(secData[4:6]) & self._wvd.getSectorMask()) - 1

    # return an index object for the n-th catalog slot.
    # return None if the index is too large.
    def getIndexEntry(self, n):
        assert self.hasCatalog()
        if type(n) == str:
            return self.getIndexEntryByName(n)
        if type(n) == int:
            return self.getIndexEntryByNumber(n)
        raise ProgramError

    # return an index object for the n-th catalog slot.
    # return None if the index is too large.
    def getIndexEntryByNumber(self, n):
        sec  = (n+1) // 16  # which index sector
        slot = (n+1) % 16   # which slot of that sector
        if sec > self.numIndexSectors():
            return None
        secData = self._wvd.getRawSector(self._platter, sec)
        record = secData[16*slot : 16*(slot+1)]
        return WvdIndex(self._wvd, self._platter, n, record)

    # given a filename, return the corresponding slot struct,
    # or return None, if not found
    def getIndexEntryByName(self, name):
        name = padName(name)
        if 0:
            # brute force: don't bother with hash approach
            for n in xrange(0, 16*self.numIndexSectors()-1):
                idx = self.getIndexEntryByNumber(n)
                idxState = idx.getIndexState()
                if (idx.getFilename() == name) and \
                    (idxState in ['valid', 'scratched']):
                    return idx
            return None # not found
        else:
            # use hash to speed up search; important for huge catalogs
            secs = self.numIndexSectors()            # sectors for catalog
            hsh  = self.getFilenameHash(name)        # where to start search
            sdir = [ -1, 1, 1 ][self.getIndexType()] # search direction search
            for delta in xrange(secs):
                secNum = (hsh + delta*sdir) % secs
                # scan all entries of this sector; sector has only 15 entries
                for slot in xrange( 16-(secNum==0) ):
                    absSlot = 16*secNum + slot - (secNum!=0)
                    thisSlot = self.getIndexEntry( absSlot )
                    idxState = thisSlot.getIndexState()
                    if idxState == "empty":
                        return None  # not found
                    if (thisSlot.getFilename() == name) and \
                       (idxState in ['valid', 'scratched']) :
                        return thisSlot
            return None  # searched all entries, and not found

    # return None if the index is too large.
    def setIndexEntry(self, n, record):
        assert len(record) == 16
        sec  = (n+1) // 16  # which index sector
        slot = (n+1) % 16   # which slot of that sector
        if sec > self.numIndexSectors():
            return None
        secData = self._wvd.getRawSector(self._platter, sec)
        secData = secData[0:16*slot] + record + secData[16*(slot+1):256]
        self._wvd.setRawSector(self._platter, sec, secData)

    def getFile(self, name):
        assert self.hasCatalog()
        if self.getIndexEntry(name) == None:
            return None
        return CatalogFile(self, name)

    # this converts the catalog index between new and old formats.
    # the data files aren't touched; the entries are simply moved around
    # to their new hash locations.
    def convertIndex(self, newStyle):
        assert self.hasCatalog()

        oldIndexType = self.getIndexType()
        if oldIndexType == 2:
            print "Can't deal with tri-byte catalogs yet"
            return
        if newStyle and (oldIndexType == 1):
            return  # not changing anything
        if not newStyle and (oldIndexType == 0):
            return  # not changing anything

        # build a list of all files on the disk, along with their attributes
        fileList = []
        for name in self.allFilenames():
            indexEntry = self.getIndexEntry(name)
            assert indexEntry != None
            fileList.append( indexEntry )

        # wipe out the index
        for s in xrange(self.numIndexSectors()):
            if s == 0:
                self.setIndexType(newStyle)
                secData = self._wvd.getRawSector(self._platter, s)
                secData = secData[0:16] + ('\0' * 240)
            else:
                secData = '\0' * 256
            self._wvd.setRawSector(self._platter, s, secData)

        # step through all files and add them back in the right place
        for entry in fileList:
            self.addIndexEntry(entry)

    # given an indexInfo struct (name, start, end, etc), add it to the catalog.
    # only the index is added, not the file it is pointing to.
    def addIndexEntry(self, entry):
        assert self.hasCatalog()

        if self.getIndexType() == 0:
            sdir = -1  # old index overflows to previous bucket
        else:
            sdir =  1  # new index overflows to next bucket

        # get which sector to try first
        hsh = self.getFilenameHash(entry.getFilename())

        # try to insert it into all sectors in the index until it fits in one
        for delta in xrange(self.numIndexSectors()):
            secNum = (hsh + delta*sdir) % self.numIndexSectors()
            # print "Trying to put %s into sector %d" % (entry.getFilename(), secNum)
            # scan all entries of this sector; sector has only 15 entries
            for slot in xrange( 16-(secNum==0) ):
                absSlot = 16*secNum + slot - (secNum!=0)
                thisSlot = self.getIndexEntry( absSlot )
                if thisSlot.getIndexState() == "empty":
                    # print "    fit into slot %d" % slot
                    self.setIndexEntry( absSlot, entry.asString() )
                    return

        print "Error: couldn't find a slot to fit file %s" % entry.getFilename()
        sys.exit()

    # return which sector the filename hashes to for the given disk
    # this is where to start looking; if the bucket is full, the
    # adjacent sectors may need to be searched too
    def getFilenameHash(self, name):
        name = padName(name)
        if self.getIndexType() == 0:
            return self.oldIndexHash(name)
        else:
            return self.newIndexHash(name)

    # this code is based on code from ISS release 5.1, program "ISS.229S"
    def oldIndexHash(self, name):
        hash_val = 0x00
        for c in name:
            hash_val ^= ord(c)
        hash_val *= 3
        hash_val = (hash_val//256) + (hash_val % 256)   # fold over carries
        return hash_val

    # this code is based on information from the OS 2.5 release notes
    def newIndexHash(self, name):
        hash_val = 0x00
        for i in xrange(0,8):
            c = ord(name[i])
            if (i % 2 == 0):
                hash_val += 16*(c & 0xf) + (c >> 4)  # swap nibbles
            else:
                hash_val += c
        return (hash_val % 256)

############################ CatalogFile Object ############################
# these objects return status about a file in a catalog.
# if the disk state is changed, the object may become stale.

class CatalogFile(object):
    # given a filename, return a CatalogFile object
    def __init__(self, cat, filename):
        self._name    = filename
        self._cat     = cat
        self._wvd     = cat._wvd      # this grubbing is pretty ugly
        self._platter = cat._platter  # this grubbing is pretty ugly
        self._index   = cat.getIndexEntry(filename)
        self._valid   = (self._index != None)

    def getFilename(self):
        return self._name
    def getType(self):
        return self._index.getFileType()
    def getStatus(self):
        return self._index.getFileStatus()
    def getStart(self):
        return self._index.getFileStart()
    def getEnd(self):
        return self._index.getFileEnd()
    def getExtent(self):
        return self._index.getFileExtent()
    def getUsed(self):
        return self._index.getFileUsedSectors()
    def getFree(self):
        return self._index.getFileFreeSectors()

    # inspect a program file and determine if it is protected (and what kind)
    # or not returns "normal", "protected", or "scrambled".  if it isn't a
    # program, then None is returned.
    def programSaveMode(self):
        if not self._valid:
            return None
        if self.getType() != 'P':
            return None

        start = self.getStart()
        secData = self._wvd.getRawSector(self._platter, start + 0)
        if ((ord(secData[0]) & 0xF0) == 0x40):
            return "normal"
        if ((ord(secData[0]) & 0xF0) != 0x50):
            return None

        # so either it is protected or scrambled
        # for non-scrambled programs, the first byte must be either 0x20 (space)
        # or 0xFF (line number token).  scrambled programs appear to always be
        # of the form 3x1y.
        secData = self._wvd.getRawSector(self._platter, start + 1)
        if (ord(secData[1]) & 0xF0) == 0x10:
            return "scrambled"
        else:
            return "protected"

    # set or clear the protection mode on the file.
    # the file must be a program file.
    def setProtection(self, protect):
        (start, end) = self.getExtent()
        # we don't touch the final allocated block
        # we only have to touch the used blocks, not all the allocated blocks
        for s in xrange(start, end):
            secData = self._wvd.getRawSector(self._platter, s)
            if protect:
                # set protection bit
                secData = chr( ord(secData[0]) | 0x10 ) + secData[1:256]
            else:
                # clear protection bit
                secData = chr( ord(secData[0]) & 0xEF ) + secData[1:256]
            self._wvd.setRawSector(self._platter, s, secData)

    # extract sectors of named file
    # given a file name (program or data), return a list containing the sector
    # data, but only of the used sectors, not all the allocated sectors.
    # return None if the filename isn't found, or if the file extent
    # information is bogus (in which case we report it)
    def getSectors(self):
        if self._index.getIndexState() != "valid":
            return None
        typ = self.getType()
        if typ == '?':
            return None
        start = self.getStart();
        end   = self.getEnd();
        if end < start:
            print "%s: bad file extent information" % self._name
            return None
        if end >= self._wvd.numSectors():
            print "%s: bad file extent information" % self._name
            return None
        used = self.getUsed();
        sectors = []
        # used-1 because one sector is used for holding the used sector count
        for sec in xrange(start, start+used-1):
            secData = self._wvd.getRawSector(self._platter, sec)
            sectors.append(secData)
        return sectors

############################## helpers ##############################

def str24ToInt(str):
    assert len(str) == 3
    return 65536*ord(str[0]) + 256*ord(str[1]) + ord(str[2])

def str16ToInt(str):
    assert len(str) == 2
    return 256*ord(str[0]) + ord(str[1])

def intToStr16(val):
    return chr(val // 256) + chr(val % 256)

def intToStr24(val):
    return chr(val // 65536) + chr((val // 256) % 256) + chr(val % 256)

def padName(name):
    assert len(name) < 9
    name += " " * (8 - len(name))
    return name

############################ checkPlatter ############################
# ad-hoc check of disk integrity
# if report = 1, report warnings on stdout
# return False if things are OK; return True if errors

def checkPlatter(wvd, p, report):
    x = checkCatalogParams(wvd, p, report)
    if x: return x
    x = checkCatalog(wvd, p, report)
    if x: return x
    return False

# check that the catalog parameter block is sane
def checkCatalogParams(wvd, p, report):
    if wvd.numPlatters() > 1:
        desc = "Platter %d" % (p+1)
    else:
        desc = "The disk"
    if wvd.catalog[p].numIndexSectors() == 0:
        if report: print "%s has no catalog" % desc
        return False
    if wvd.catalog[p].numIndexSectors() >= wvd.numSectors():
        if report: print "%s reports an index that larger than the disk" % desc
        return True
    if wvd.catalog[p].endCatalogArea() >= wvd.numSectors():
        if report: print "%s reports a last catalog sector that larger than the disk size" % desc
        return True
    if wvd.catalog[p].currentEnd() > wvd.catalog[p].endCatalogArea():
        if report: print "%s supposedly has more cataloged sectors in use than available" % desc
        return True
    return False

# step through and verify each entry of a catalog on the indicated platter
def checkCatalog(wvd, p, report):

    # make a map of used sectors
    inUse = [0] * wvd.numSectors()
    for n in xrange(0, wvd.catalog[p].numIndexSectors()):
        inUse[n] = 1

    # step through each catalog entry
    for name in wvd.catalog[p].allFilenames():
        indexEntry = wvd.catalog[p].getIndexEntry(name)
        #hash = wvd.catalog[p].getFilenameHash(name) % wvd.catalog[p].numIndexSectors()
        #print "File '%s' should be in sector %d" % (name, hash)
        if indexEntry != None:
            # report problems
            bad = checkFile(wvd, p, name, report)
            if not bad:
                # mark used sectors
                (start, end) = indexEntry.getFileExtent()
                for s in xrange(start, end+1):
                    if inUse[s]:
                        if report: print "Platter %d: %s uses sector %d, but it is already in use" % (p, indexEntry.getFilename(), s)
                        return True
                    inUse[s] = 1

    return False # everything is OK

# make sure the filename embedded in the first block of the file
# matches the actual file name
def checkFile(wvd, p, name, report):

    indexEntry = wvd.catalog[p].getIndexEntry(name)
    if indexEntry == None:
        print "Platter %d: %s: not found" % (p, name)
        return True

    fname = indexEntry.getFilename()
    start = indexEntry.getFileStart()
    end   = indexEntry.getFileEnd()

    if end < start:
        if report: print "%s ends (%d) before it starts (%d)" % (fname, end, start)
        return True
    if end >= wvd.numSectors():
        if report: print "%s is off the end of the disk (%d > %d)" % (fname, end, wvd.numSectors())
        return True

    # this must be fetched after we've checked that end is reasonable,
    # as it is located in the last sector of the file
    used = indexEntry.getFileUsedSectors()

    # both data and program files use the last sector to indicate
    # how many sectors are used
    if start+used-1 > end:
        if report: print "%s's end block claims %d sectors used, but only %d allocated" % \
            (fname, used, end-start+1)
        return True

    # do program or data file specific checks
    fileType = indexEntry.getFileType()
    if fileType == "P":
        x = checkProgramFile(wvd, p, indexEntry, report)
        if x: return x
    elif fileType == "D":
        x = checkDataFile(wvd, p, indexEntry, report)
        if x: return x
    else:
        if report: print "%s has unknown file type" % fname
        return True

    return False

# check that the control byte of each sector matches what is expected,
# and that if the program is protected, that flag is consistent.
# the first sector is 0x40, middle sectors are 0x00, final sector is 0x20
def checkProgramFile(wvd, p, indexEntry, report):

    fname = indexEntry.getFilename()
    start = indexEntry.getFileStart()
    used  = indexEntry.getFileUsedSectors()

    # check the filename embedded in the first sector matches the catalog filename
    firstSector = wvd.getRawSector(p, start)
    name = firstSector[1:9]
    if name != fname:
        if report: print "%s has in-file name of %s" % (fname, name)
        return True

    # the next byte after the filename should be 0xFD
    if firstSector[9] != chr(0xfd):
        if report: print "%s is missing the filename record terminator" % fname
        return True

    # the used value includes the last sector of the allocated file,
    # so it shouldn't be counted (the -1 below)
    for s in xrange(0, used-1):
        secData = wvd.getRawSector(p, start + s)
        if s == 0:
            # first sector
            protected = (ord(secData[0]) & 0x10)  # 0x10 if "SAVE P" file
            expected = 0x40 | protected
        elif s == (used-2):
            # last sector
            expected = 0x20
        else:
            # middle sector
            expected = 0x00

        if (ord(secData[0]) & 0xf0) != (expected | protected):
            if report: print "%s sector %d has control byte of 0x%02x; 0x%02x expected" % \
                (fname, s, ord(secData[0]) & 0xf0, expected | protected);
            return True

    return False

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
def checkDataFile(wvd, p, indexEntry, report):

    fname = indexEntry.getFilename()
    start = indexEntry.getFileStart()
    used  = indexEntry.getFileUsedSectors()

    # the used value includes the last sector of the allocated file,
    # so it shouldn't be counted (the -1 below)
    state = 0
    for s in xrange(0, used-1):

        secData = wvd.getRawSector(p, start + s)
        if (s == 0) and (secData[0] == chr(0x6a)):
            # print "%s is some kind of raw binary file; assuming OK" % fname
            return False

        if state == 0:
            # looking for either end of data or start of record
            if (secData[0] == chr(0xa0)) or (secData[0] == chr(0xa1)):
                state = 2
            elif (secData[0] == chr(0x81)) and (secData[1] == chr(0x01)):
                # this is a single sector record
                state = 0
            elif (secData[0] == chr(0x82)) and (secData[1] == chr(0x01)):
                # this starts a multisector record; expect another sector
                expect = 0x02
                state = 1
            else:
                if report: print "%s, sector %d has bad control bytes 0x%02x%02x" % \
                    (fname, s, ord(secData[0]), ord(secData[1]))
                return True

        elif state == 1:
            # expect to see next (and possibly last) sector of record
            if ord(secData[1]) != expect:
                if report: print "%s, sector %d expected to have sequence number 0x%02x, got 0x%02x" % \
                    (fname, s, expect, ord(secData[1]))
                return True
            if secData[0] == chr(0x82):
                # expect more sectors in this logical record
                expect += 1
                state = 1  # that is, no change
            elif secData[0] == chr(0x81):
                # this is the last one of this record
                state = 0  # expect either end of data or new record
            else:
                if report: print "%s, sector %d has bad control bytes 0x%02x%02x" % \
                    (fname, s, ord(secData[0]), ord(secData[1]))
                return True

        elif state == 2:
            # we've seen the end of data marker, so there shouldn't be more
            if report: print "%s, end-of-data marker seen before final used sector" % fname
            return True

    return False
