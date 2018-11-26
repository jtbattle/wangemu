// ------------------------------------------------------------------------
//  Wvd class implementation
// ------------------------------------------------------------------------

#include "IoCardDisk.h"
#include "System2200.h"
#include "Ui.h"
#include "Wvd.h"

#include <fstream>

#ifdef _DEBUG
    #define DBG  (0)            // turn on some debug logging
#else
    #define DBG  (0)
#endif

#ifdef _MSC_VER
    #pragma warning( disable: 4127 )  // conditional expression is constant
#endif

// address DCT/D1x : x=0 means fixed drive, x>0 means platter x-1 of removable
// therefore the most platters a drive could ever have is 15 platters
#define WVD_MAX_PLATTERS 15

// =====================================================
//   public interface
// =====================================================

// making a valid Wvd is a two step process.  create a container with the
// default constructor, then call either open() or create() to fill it.
Wvd::Wvd() :
    m_file(nullptr),
    m_metadataStale(true),
    m_metaModified(false),
    m_hasPath(false),
    m_path(""),
    m_label(""),
    m_diskType(DISKTYPE_ILLEGAL),
    m_numPlatters(0),
    m_numPlatterSectors(0),
    m_writeProtect(false)
{
    initMembers();
}


// initialize all members to virgin state
void
Wvd::initMembers()
{
    setPath("");
    setLabel("");
    setDiskType(0);
    setNumPlatters(0);
    setNumSectors(0);
    setWriteProtect(false);
    setModified(false);
}


Wvd::~Wvd()
{
    close();
}


// create a new disk image -- a name will be associated with it and the
// file actually created upon the call to save().
void
Wvd::create(int disk_type, int platters, int sectors_per_platter)
{
    assert(m_file == nullptr);
    setDiskType(disk_type);
    setNumPlatters(platters);
    setNumSectors(sectors_per_platter);
    setLabel("Your comment here");
    m_metadataStale = false;
}


// open up an existing virtual disk file
bool
Wvd::open(const std::string &filename)
{
    assert(m_file == nullptr);
    assert(!m_hasPath);
    assert(!filename.empty());

    // set up a file handle
    m_file = std::make_unique<std::fstream>(
                                filename.c_str(),
                                std::fstream::in | std::fstream::out | std::fstream::binary );
    if (!m_file) {
        return false;
    }

    if (!m_file->good()) {
        UI_Error("Couldn't open file '%s'", m_path.c_str());
        m_file = nullptr;
        return false;
    }

    m_hasPath = true;
    m_path = filename;

    bool ok = readHeader();
// don't need to raise alarm, since readHeader() already did
//  if (!ok) {
//      UI_Info("Error opening file", filename.c_str());
//  }
    m_metadataStale = !ok;

    return ok;
}


// forget about current state
void
Wvd::close()
{
    if (m_file != nullptr) {
        if (m_file->is_open()) {
            m_file->close();
        }
        m_file = nullptr;
    }
    initMembers();
}


// -------------------------------------------------------------------------
// metadata access
// -------------------------------------------------------------------------

bool
Wvd::isModified() const
{
    return m_metaModified;
}

void
Wvd::setModified(bool modified)
{
    m_metaModified = modified;
}

std::string
Wvd::getPath() const
{
    return m_path;
}

void
Wvd::setPath(const std::string &filename)
{
    if (filename.empty()) {
        m_hasPath = false;
        m_path = "";
    } else if (!m_hasPath || m_path != filename) {
        m_hasPath = true;
        m_path = filename;
        setModified();
    }
}

int
Wvd::getDiskType()
{
    refreshMetadata();
    return m_diskType;
}

void
Wvd::setDiskType(int type)
{
    refreshMetadata();
    if (m_diskType != type) {
        m_diskType = static_cast<disktype_t>(type);
        setModified();
    }
}

int
Wvd::getNumPlatters()
{
    refreshMetadata();
    return m_numPlatters;
}

void
Wvd::setNumPlatters(int num)
{
    refreshMetadata();
    if (m_numPlatters != num) {
        m_numPlatters = num;
        setModified();
    }
}

int
Wvd::getNumSectors()
{
    refreshMetadata();
    return m_numPlatterSectors;
}

void
Wvd::setNumSectors(int num)
{
    refreshMetadata();
    if (m_numPlatterSectors != num) {
        m_numPlatterSectors = num;
        setModified();
    }
}

bool
Wvd::getWriteProtect()
{
    refreshMetadata();
    return m_writeProtect;
}

void
Wvd::setWriteProtect(bool wp)
{
    refreshMetadata();
    if (m_writeProtect != wp) {
        m_writeProtect = wp;
        setModified();
    }
}

std::string
Wvd::getLabel()
{
    refreshMetadata();
    return m_label;
}

void
Wvd::setLabel(const std::string &newlabel)
{
    refreshMetadata();
    if (m_label != newlabel) {
        m_label = newlabel;
        setModified();
    }
}

// -------------------------------------------------------------------------
// logical sector access
// -------------------------------------------------------------------------

// logical sector read.
// returns true on success, false on failure.
bool
Wvd::readSector(int platter, int sector, uint8 *buffer)
{
    refreshMetadata();

    assert(platter >= 0 && platter < m_numPlatters);
    assert(sector  >= 0 && sector  < m_numPlatterSectors);
    assert(m_file != nullptr);

    int abs_sector = m_numPlatterSectors*platter + sector + 1;
    return rawReadSector(abs_sector, buffer);
}


// logical sector write.
// returns true on success, false on failure.
bool
Wvd::writeSector(int platter, int sector, uint8 *buffer)
{
    refreshMetadata();

    assert(platter >= 0 && platter < m_numPlatters);
    assert(sector  >= 0 && sector  < m_numPlatterSectors);
    assert(m_file != nullptr);

    int abs_sector = m_numPlatterSectors*platter + sector + 1;
    return rawWriteSector(abs_sector, buffer);
}


// flush any pending write and close the filehandle,
// but keep the association (unlike close())
// this function is called when another function wants to touch
// a file which we have opened.  this closes the file, and later
// when the Wvd code is called again, reopen() is called to
// refresh the metadata that we cache.
void
Wvd::flush()
{
    if (m_file != nullptr) {
        if (m_file->is_open()) {
            m_file->flush();
        }
        m_file->close();
        m_metadataStale = true;
    }
}


// if the state was initialized by a call to open(), we already have the
// filename we need to save the modified state back to that same file.
void
Wvd::save()
{
    assert(m_hasPath);
    assert(!m_path.empty());

    if (isModified()) {
        if (writeHeader()) {
            setModified(false);
        } else {
            UI_Error("Error: operation failed");
        }
    }
}

// if the state was constructed following a call to create(), we need
// to create the entire disk file from scratch.
void
Wvd::save(const std::string &filename)
{
    assert(!filename.empty());
    assert(!m_hasPath);

    if (createFile(filename)) {
        setModified(false);
    } else {
        UI_Error("Error: operation failed");
    }
}

// -------------------------------------------------------------------------
// private functions: absolute sector access
// -------------------------------------------------------------------------

// this writes an absolute sector to the virtual disk image.
// sector 0 contains the disk metadata, while logical sector 0
// starts at absolute sector 1.
// return true on success
bool
Wvd::rawWriteSector(const int sector, const uint8 *data)
{
    assert(m_hasPath);
    assert(sector >= 0 && sector < m_numPlatters*m_numPlatterSectors+1);
    assert(data != nullptr);
    assert(m_file->is_open());

    if (DBG > 0) {
        dbglog("========== writing absolute sector %d ==========\n", sector);
        for(int i=0; i<256; i+=16) {
            char str[200];
            sprintf(str, "%02X:", i);
            for(int ii=0; ii<16; ii++) {
                sprintf(str+3+3*ii, " %02X", data[i+ii]);
            }
            dbglog("%s\n", str);
        }
    }

    // go to the start of the Nth sector
    m_file->seekp( 256LL*sector );
    if (!m_file->good()) {
        UI_Error("Error seeking to write sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    // write them all
    m_file->write( (char*)data, 256 );
    if (!m_file->good()) {
        UI_Error("Error writing to sector %d of '%s'",
                  sector, m_path.c_str());
        m_file->close();
        return false;
    }

    // slower, but safer in case of unexpected shutdown
    m_file->flush();

    return true;
}


// read from an absolute sector address on the disk.
// return true on success
bool
Wvd::rawReadSector(const int sector, const uint8 *data)
{
    assert(m_hasPath);
    assert(sector >= 0 && sector < m_numPlatters*m_numPlatterSectors+1);
    assert(data != nullptr);
    assert(m_file->is_open());

    // go to the start of the Nth sector
    m_file->seekg( 256LL*sector);
    if (!m_file->good()) {
        UI_Error("Error seeking to read sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    m_file->read( (char*)data, 256 );
    if (!m_file->good()) {
        UI_Error("Error reading from sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    if (DBG > 0) {
        dbglog("========== reading absolute sector %d ==========\n", sector);
        for(int i=0; i<256; i+=16) {
            char str[200];
            sprintf(str, "%02X:", i);
            for(int ii=0; ii<16; ii++) {
                sprintf(str+3+3*ii, " %02X", data[i+ii]);
            }
            dbglog("%s\n", str);
        }
    }

    return true;
}


// write header block for wang virtual disk
// header format
// bytes  0-  4: "WANG\0"
// bytes  5    : write format version
// bytes  6    : read format version
// bytes  7    : write protect
// bytes  8-  9: number of sectors per platter
// byte  10    : disk type
// bytes 11    : number of platters minus one
// bytes 12- 15: unused (zeros)
// bytes 16-255: disk label
// return true on success
bool
Wvd::writeHeader()
{
    assert(m_numPlatters > 0 && m_numPlatters <= 15);
    assert(m_numPlatterSectors > 0 && m_numPlatterSectors <= WVD_MAX_SECTORS);

    // header block -- zap it to zeros
    uint8 data[256];
    memset(data, (uint8)0x00, 256);

    // magic string
    data[0] = 'W';
    data[1] = 'A';
    data[2] = 'N';
    data[3] = 'G';
    data[4] = 0;

    // the point of having a read format and a write format is that different
    // write formats indicate incompatible versions of the disk format, while
    // read format indicates that the information is a superset of a previous
    // version and that what is read by the old program is still usable.  For
    // example, say the format is rev'd to add a seek time parameter, but
    // everything else is the same.  An older emulator can still read and use
    // the disk, so the read format is left at 0, but the write format number
    // is set to 1 so a new emulator knows if the seek time parameter is
    // usable.
    data[5] = (uint8)0x00;      // write format version
    data[6] = (uint8)0x00;      // read format version

    data[7] = (uint8)(m_writeProtect ? 1 : 0);

    // number of sectors per platter
    data[8] = (uint8)((m_numPlatterSectors >> 0) & 0xFF);
    data[9] = (uint8)((m_numPlatterSectors >> 8) & 0xFF);

    data[10] = static_cast<uint8>(m_diskType);

    data[11] = (uint8)m_numPlatters-1;

    strncpy((char*)&data[16], m_label.c_str(), WVD_MAX_LABEL_LEN);

    // write the header block -- first absolute sector of disk image
    return rawWriteSector(0, data);
}


// make sure metadata is up to date
void
Wvd::reopen()
{
    assert(m_file != nullptr);

    if (m_metadataStale) {
        assert(!m_file->is_open()); // make sure we cached it properly
        // set up a file handle
        m_file->open(m_path.c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
        if (!m_file->good()) {
            UI_Error("Couldn't open file '%s'", m_path.c_str());
            m_file = nullptr;
            return;
        }
        bool ok = readHeader();
        if (!ok) {
            m_file = nullptr;
            return;
        }
    }
    m_metadataStale = false;
}


// retrieve the metadata from the virtual disk image.
// if the file is already open, just read it;
// if the file isn't open, open it, read the metadata, and leave it open.
// return true on success, otherwise complain why and return false.
bool
Wvd::readHeader()
{
    assert(m_file != nullptr);

    // set it so rawReadSector() knows what to operate on
    m_numPlatters = 1;
    m_numPlatterSectors = 1;

    uint8 data[256];
    bool ok = rawReadSector(0, data);
    if (!ok) {
        return false;
    }

    // check magic
    if (data[0] != 'W' ||
        data[1] != 'A' ||
        data[2] != 'N' ||
        data[3] != 'G' ||
        data[4] != '\0') {
        UI_Warn("This isn't a Wang Virtual Disk");
        return false;
    }

    // check read format
    if (data[6] != 0x00) {
        UI_Error("This disk is from a more recent version of WangEmu.\n"
                 "Please use a more recent emulator.");
        return false;
    }

    bool tmp_writeProtect = (data[7] != 0x00);

    int tmp_sectors = (int)((data[9] << 8) | data[8]);
    if (tmp_sectors > WVD_MAX_SECTORS) {
        UI_Error("The disk claims to have more than %d platters", WVD_MAX_SECTORS);
        return false;
    }

    disktype_t tmp_disktype = static_cast<disktype_t>(data[10]);
    if (tmp_disktype >= DISKTYPE_ILLEGAL) {
        UI_Error("The disktype field of the disk image isn't legal");
        return false;
    }

    int tmp_platters = (int)data[11] + 1;
    if (tmp_platters > WVD_MAX_PLATTERS) {
        UI_Error("The disk claims to have more than %d platters", WVD_MAX_PLATTERS);
        return false;
    }

    if (strlen((const char*)&data[16]) > WVD_MAX_LABEL_LEN-1) {
        UI_Error("The label appears to be too long.\n"
                 "Is the disk image corrupt?");
        return false;
    }
    std::string tmp_label( (const char*)&data[16] );

    m_metaModified      = false;
    m_label             = tmp_label;
    m_diskType          = tmp_disktype;
    m_numPlatters       = tmp_platters;
    m_numPlatterSectors = tmp_sectors;
    m_writeProtect      = tmp_writeProtect;

    return true;
}


// format the given platter of the virtual disk image.
// returns true if successful.
bool
Wvd::format(const int platter)
{
    assert(platter >= 0 && platter < m_numPlatters);

    // fill all non-header sectors with 0x00
    uint8 data[256];
    memset(data, (uint8)0x00, 256);

    bool ok = true;
    for(int n=0; ok && n<m_numPlatterSectors; n++) {
        ok = writeSector(platter, n, data);
    }

    return ok;
}


// create a virtual disk file if it doesn't exist, erase it if does.
// write the header and then format all platters.
// returns true on success.
bool
Wvd::createFile(const std::string &filename)
{
    assert(m_file == nullptr);
    assert(!m_hasPath);
    assert(!filename.empty());

    m_hasPath = true;
    m_path = filename;

    // create the file if it doesn't exist; erase if it does
    m_file = std::make_unique<std::fstream>(
                filename.c_str(),
                std::fstream::in | std::fstream::out | std::fstream::trunc | std::fstream::binary);
    if (!m_file) {
        return false;
    }

    if (!m_file->good()) {
        UI_Error("Couldn't create file '%s'", m_path.c_str());
        m_file = nullptr;
        return false;
    }

    bool ok = writeHeader();
    if (ok) {
        for(int p=0; ok && p<m_numPlatters; p++) {
            ok = format(p);
        }
    }

    return ok;
}

// vim: ts=8:et:sw=4:smarttab
