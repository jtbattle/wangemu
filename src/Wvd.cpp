// ------------------------------------------------------------------------
//  Wvd class implementation
// ------------------------------------------------------------------------

#include "IoCardDisk.h"
#include "host.h"              // for dbglog()
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
    m_metadata_stale = false;
}


// open up an existing virtual disk file
bool
Wvd::open(const std::string &filename)
{
    assert(m_file == nullptr);
    assert(!m_has_path);
    assert(!filename.empty());

    // set up a file handle
    m_file = std::make_unique<std::fstream>(
                    filename.c_str(),
                    std::fstream::in | std::fstream::out | std::fstream::binary);
    if (!m_file) {
        return false;
    }

    if (!m_file->good()) {
        UI_error("Couldn't open file '%s'", m_path.c_str());
        m_file = nullptr;
        return false;
    }

    m_has_path = true;
    m_path = filename;

    const bool ok = readHeader();
// don't need to raise alarm, since readHeader() already did
//  if (!ok) {
//      UI_info("Error opening file", filename.c_str());
//  }
    m_metadata_stale = !ok;

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

    // reinitialize in case the Wvd object gets recycled
    setPath("");
    setLabel("");
    setDiskType(0);
    setNumPlatters(0);
    setNumSectors(0);
    setWriteProtect(false);
    setModified(false);
}

// -------------------------------------------------------------------------
// metadata access
// -------------------------------------------------------------------------

bool
Wvd::isModified() const noexcept
{
    return m_metadata_modified;
}


void
Wvd::setModified(bool modified) noexcept
{
    m_metadata_modified = modified;
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
        m_has_path = false;
        m_path = "";
    } else if (!m_has_path || m_path != filename) {
        m_has_path = true;
        m_path = filename;
        setModified();
    }
}


int
Wvd::getDiskType()
{
    refreshMetadata();
    return m_disk_type;
}


void
Wvd::setDiskType(int type)
{
    refreshMetadata();
    if (m_disk_type != type) {
        m_disk_type = static_cast<disktype_t>(type);
        setModified();
    }
}


int
Wvd::getNumPlatters()
{
    refreshMetadata();
    return m_num_platters;
}


void
Wvd::setNumPlatters(int num)
{
    refreshMetadata();
    if (m_num_platters != num) {
        m_num_platters = num;
        setModified();
    }
}


int
Wvd::getNumSectors()
{
    refreshMetadata();
    return m_num_platter_sectors;
}


void
Wvd::setNumSectors(int num)
{
    refreshMetadata();
    if (m_num_platter_sectors != num) {
        m_num_platter_sectors = num;
        setModified();
    }
}


bool
Wvd::getWriteProtect()
{
    refreshMetadata();
    return m_write_protect;
}


void
Wvd::setWriteProtect(bool wp)
{
    refreshMetadata();
    if (m_write_protect != wp) {
        m_write_protect = wp;
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
Wvd::readSector(int platter, int sector, const uint8 *buffer)
{
    assert(buffer != nullptr);
    refreshMetadata();

    assert(platter >= 0 && platter < m_num_platters);
    assert(sector  >= 0 && sector  < m_num_platter_sectors);
    assert(m_file != nullptr);

    const int abs_sector = m_num_platter_sectors*platter + sector + 1;
    return rawReadSector(abs_sector, buffer);
}


// logical sector write.
// returns true on success, false on failure.
bool
Wvd::writeSector(int platter, int sector, const uint8 *buffer)
{
    assert(buffer != nullptr);
    refreshMetadata();

    assert(platter >= 0 && platter < m_num_platters);
    assert(sector  >= 0 && sector  < m_num_platter_sectors);
    assert(m_file != nullptr);

    const int abs_sector = m_num_platter_sectors*platter + sector + 1;
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
        m_metadata_stale = true;
    }
}


// if the state was initialized by a call to open(), we already have the
// filename we need to save the modified state back to that same file.
void
Wvd::save()
{
    assert(m_has_path);
    assert(!m_path.empty());

    if (isModified()) {
        if (writeHeader()) {
            setModified(false);
        } else {
            UI_error("Error: operation failed");
        }
    }
}

// if the state was constructed following a call to create(), we need
// to create the entire disk file from scratch.
void
Wvd::save(const std::string &filename)
{
    assert(!filename.empty());
    assert(!m_has_path);

    if (createFile(filename)) {
        setModified(false);
    } else {
        UI_error("Error: operation failed");
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
    assert(m_has_path);
    assert(sector >= 0 && sector < m_num_platters*m_num_platter_sectors+1);
    assert(data != nullptr);
    assert(m_file->is_open());

    if (DBG > 0) {
        dbglog("========== writing absolute sector %d ==========\n", sector);
        for (int i=0; i<256; i+=16) {
            char str[200];
            sprintf(&str[0], "%02X:", i);
            for (int ii=0; ii<16; ii++) {
                sprintf(&str[3+3*ii], " %02X", data[i+ii]);
            }
            dbglog("%s\n", &str[0]);
        }
    }

    // go to the start of the Nth sector
    m_file->seekp(256LL*sector);
    if (!m_file->good()) {
        UI_error("Error seeking to write sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    // write them all
    m_file->write(reinterpret_cast<const char*>(data), 256);
    if (!m_file->good()) {
        UI_error("Error writing to sector %d of '%s'",
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
    assert(m_has_path);
    assert(sector >= 0 && sector < m_num_platters*m_num_platter_sectors+1);
    assert(data != nullptr);
    assert(m_file->is_open());

    // go to the start of the Nth sector
    m_file->seekg(256LL * sector);
    if (!m_file->good()) {
        UI_error("Error seeking to read sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    m_file->read((char*)data, 256);
    if (!m_file->good()) {
        UI_error("Error reading from sector %d of '%s'",
                 sector, m_path.c_str());
        m_file->close();
        return false;
    }

    if (DBG > 0) {
        dbglog("========== reading absolute sector %d ==========\n", sector);
        for (int i=0; i<256; i+=16) {
            char str[200];
            sprintf(&str[0], "%02X:", i);
            for (int ii=0; ii<16; ii++) {
                sprintf(&str[3+3*ii], " %02X", data[i+ii]);
            }
            dbglog("%s\n", &str[0]);
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
    assert(m_num_platters > 0 && m_num_platters <= 15);
    assert(m_num_platter_sectors > 0 && m_num_platter_sectors <= WVD_MAX_SECTORS);

    // header block -- zap it to zeros
    uint8 data[256];
    memset(&data[0], static_cast<uint8>(0x00), 256);

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
    data[5] = static_cast<uint8>(0x00);  // write format version
    data[6] = static_cast<uint8>(0x00);  // read format version

    data[7] = static_cast<uint8>(m_write_protect ? 1 : 0);

    // number of sectors per platter
    data[8] = static_cast<uint8>((m_num_platter_sectors >> 0) & 0xFF);
    data[9] = static_cast<uint8>((m_num_platter_sectors >> 8) & 0xFF);

    data[10] = static_cast<uint8>(m_disk_type);

    data[11] = static_cast<uint8>(m_num_platters-1);

    strncpy(reinterpret_cast<char*>(&data[16]), m_label.c_str(), WVD_MAX_LABEL_LEN);

    // write the header block -- first absolute sector of disk image
    return rawWriteSector(0, &data[0]);
}


// make sure metadata is up to date
void
Wvd::reopen()
{
    assert(m_file != nullptr);

    if (m_metadata_stale) {
        assert(!m_file->is_open()); // make sure we cached it properly
        // set up a file handle
        m_file->open(m_path.c_str(), std::fstream::in | std::fstream::out | std::fstream::binary);
        if (!m_file->good()) {
            UI_error("Couldn't open file '%s'", m_path.c_str());
            m_file = nullptr;
            return;
        }
        const bool ok = readHeader();
        if (!ok) {
            m_file = nullptr;
            return;
        }
    }
    m_metadata_stale = false;
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
    m_num_platters = 1;
    m_num_platter_sectors = 1;

    uint8 data[256];
    const bool ok = rawReadSector(0, &data[0]);
    if (!ok) {
        return false;
    }

    // check magic
    if (data[0] != 'W' ||
        data[1] != 'A' ||
        data[2] != 'N' ||
        data[3] != 'G' ||
        data[4] != '\0') {
        UI_warn("This isn't a Wang Virtual Disk");
        return false;
    }

    // check read format
    if (data[6] != 0x00) {
        UI_error("This disk is from a more recent version of WangEmu.\n"
                 "Please use a more recent emulator.");
        return false;
    }

    const bool tmp_write_protect = (data[7] != 0x00);

    const int tmp_sectors = static_cast<int>((data[9] << 8) | data[8]);
    if (tmp_sectors > WVD_MAX_SECTORS) {
        UI_error("The disk claims to have more than %d platters", WVD_MAX_SECTORS);
        return false;
    }

    const disktype_t tmp_disktype = static_cast<disktype_t>(data[10]);
    if (tmp_disktype >= DISKTYPE_ILLEGAL) {
        UI_error("The disktype field of the disk image isn't legal");
        return false;
    }

    const int tmp_platters = static_cast<int>(data[11] + 1);
    if (tmp_platters > WVD_MAX_PLATTERS) {
        UI_error("The disk claims to have more than %d platters", WVD_MAX_PLATTERS);
        return false;
    }

    if (strlen(reinterpret_cast<const char*>(&data[16])) > WVD_MAX_LABEL_LEN-1) {
        UI_error("The label appears to be too long.\n"
                 "Is the disk image corrupt?");
        return false;
    }
    std::string tmp_label(reinterpret_cast<const char*>(&data[16]));

    m_metadata_modified   = false;
    m_label               = tmp_label;
    m_disk_type           = tmp_disktype;
    m_num_platters        = tmp_platters;
    m_num_platter_sectors = tmp_sectors;
    m_write_protect       = tmp_write_protect;

    return true;
}


// format the given platter of the virtual disk image.
// returns true if successful.
bool
Wvd::format(const int platter)
{
    assert(platter >= 0 && platter < m_num_platters);

    // fill all non-header sectors with 0x00
    uint8 data[256];
    memset(&data[0], static_cast<uint8>(0x00), 256);

    bool ok = true;
    for (int n=0; ok && n<m_num_platter_sectors; n++) {
        ok = writeSector(platter, n, &data[0]);
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
    assert(!m_has_path);
    assert(!filename.empty());

    m_has_path = true;
    m_path = filename;

    // create the file if it doesn't exist; erase if it does
    m_file = std::make_unique<std::fstream>(
                filename.c_str(),
                std::fstream::in | std::fstream::out | std::fstream::trunc | std::fstream::binary);
    if (!m_file) {
        return false;
    }

    if (!m_file->good()) {
        UI_error("Couldn't create file '%s'", m_path.c_str());
        m_file = nullptr;
        return false;
    }

    bool ok = writeHeader();
    if (ok) {
        for (int p=0; ok && p<m_num_platters; p++) {
            ok = format(p);
        }
    }

    return ok;
}

// vim: ts=8:et:sw=4:smarttab
